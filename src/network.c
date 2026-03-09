#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <windows.h>
#include <mmsystem.h>
#include "windivert.h"
#include "NetSpeedCtrl.h"

#pragma comment(lib, "winmm.lib")

#define BUFFER_SIZE 65535
#define MAX_DEBT_SEC 0.5 // 允许 0.5s 的负债缓冲区

volatile int64_t g_LimitInboundBytesPerSec = 0;
volatile int64_t g_LimitOutboundBytesPerSec = 0;
volatile int64_t g_CurrentInboundSpeed = 0;
volatile int64_t g_CurrentOutboundSpeed = 0;
volatile bool g_EnableLimit = false;
volatile bool g_IsRunning = false;
volatile bool g_TimePeriodMatch = true;
// g_InLimitChanged / g_OutLimitChanged 已废弃：
// 动态限速检测改为每个线程独立对比 lastAppliedLimit，不再需要共享标志

volatile int g_UnitMultiplier = 1024;
int g_StartH = 0, g_StartM = 0, g_EndH = 23, g_EndM = 59;
extern HWND g_hLogEdit;

static double GetQPC() {
    LARGE_INTEGER q, f;
    QueryPerformanceCounter(&q);
    QueryPerformanceFrequency(&f);
    return (double)q.QuadPart / (double)f.QuadPart;
}

static void PreciseSleep(double seconds) {
    if (seconds <= 0) return;
    if (seconds > 0.015) Sleep((DWORD)(seconds * 1000) - 10);
    double start = GetQPC();
    while ((GetQPC() - start) < seconds);
}

void LogMessage(const wchar_t *format, ...) {
    if (!g_hLogEdit) return;
    wchar_t b[1024]; va_list a; va_start(a, format); vswprintf(b, 1024, format, a); va_end(a);
    int l = GetWindowTextLengthW(g_hLogEdit);
    SendMessageW(g_hLogEdit, EM_SETSEL, l, l);
    SendMessageW(g_hLogEdit, EM_REPLACESEL, 0, (LPARAM)b);
    SendMessageW(g_hLogEdit, EM_REPLACESEL, 0, (LPARAM)L"\r\n");
}

bool IsCurrentTimeInPeriod() {
    SYSTEMTIME s; GetLocalTime(&s);
    int c = s.wHour * 60 + s.wMinute, st = g_StartH * 60 + g_StartM, et = g_EndH * 60 + g_EndM;
    return (st <= et) ? (c >= st && c <= et) : (c >= st || c <= et);
}

// ── 开机自启：注册表 Run（exe 本身通过 manifest 要求管理员权限）──
// 因为 exe 已嵌入 requireAdministrator manifest，
// 注册表 Run 启动时 Windows 会自动以管理员身份运行，无需额外处理
void AddRegistryStartup(bool e) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (e) {
        wchar_t p[MAX_PATH];
        GetModuleFileNameW(NULL, p, MAX_PATH);
        RegSetValueExW(k, L"NetSpeedCtrl", 0, REG_SZ,
            (BYTE*)p, (DWORD)((wcslen(p) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, L"NetSpeedCtrl");
    }
    RegCloseKey(k);
}

// 检查注册表是否已有自启项
bool IsStartupRegistered(void) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD size = 0;
    bool found = (RegQueryValueExW(k, L"NetSpeedCtrl", NULL, NULL, NULL, &size) == ERROR_SUCCESS);
    RegCloseKey(k);
    return found;
}

DWORD WINAPI TimeMonitorThread(LPVOID p) {
    while (g_IsRunning) { g_TimePeriodMatch = IsCurrentTimeInPeriod(); Sleep(1000); }
    return 0;
}

static DWORD RateLimiterCore(bool isInbound) {
    HANDLE h;
    WINDIVERT_ADDRESS addr;
    uint8_t *pkt = (uint8_t *)malloc(BUFFER_SIZE);
    uint32_t pktLen;
    const char *f = isInbound ? "inbound and (ip or ipv6) and not loopback" : "outbound and (ip or ipv6) and not loopback";
    
    // 打开驱动
    h = WinDivertOpen(f, WINDIVERT_LAYER_NETWORK, 0, 0);
    if (h == INVALID_HANDLE_VALUE) { free(pkt); return 1; }

    // 【关键优化】提升内核队列上限，防止限速时导致的丢包重置错误
    WinDivertSetParam(h, WINDIVERT_PARAM_QUEUE_LENGTH, 8192);   // 队列深度
    WinDivertSetParam(h, WINDIVERT_PARAM_QUEUE_SIZE, 16 * 1024 * 1024); // 16MB 内存缓冲
    WinDivertSetParam(h, WINDIVERT_PARAM_QUEUE_TIME, 2000);  // 允许包在内核积压 2秒

    LogMessage(L"[系统] %s 驱动加速引擎已启动。", isInbound ? L"下载" : L"上传");

    double  lastTime        = GetQPC();
    double  lastStatTime    = lastTime;
    int64_t bytesAcc        = 0;
    double  tokens          = 0.0;
    // 每个线程独立记录上次生效的限速值，用于检测动态变更
    // 初始值 -1 表示"从未生效"，确保首次进入限速时触发初始化
    int64_t lastAppliedLimit = -1;

    timeBeginPeriod(1);

    while (g_IsRunning) {
        if (!WinDivertRecv(h, pkt, BUFFER_SIZE, &pktLen, &addr)) continue;

        double now     = GetQPC();
        double elapsed = now - lastTime;
        // elapsed 限制在 0.2s：防止线程长时间挂起后 tokens 被暴冲
        if (elapsed > 0.2) elapsed = 0.2;
        lastTime = now;

        // ── 流量统计（始终运行）──────────────────────────────────
        bytesAcc += pktLen;
        if (now - lastStatTime >= 1.0) {
            if (isInbound) g_CurrentInboundSpeed  = bytesAcc;
            else           g_CurrentOutboundSpeed = bytesAcc;
            bytesAcc     = 0;
            lastStatTime = now;
        }

        // ── 限速逻辑 ─────────────────────────────────────────────
        if (g_EnableLimit && g_TimePeriodMatch) {
            int64_t limit = isInbound
                ? g_LimitInboundBytesPerSec
                : g_LimitOutboundBytesPerSec;

            // 动态调整检测：每个线程独立对比自己上次用的限速值
            if (limit != lastAppliedLimit) {
                // 新限速生效时：给半秒初始令牌，避免首批包因 tokens=0 全部积压
                tokens = (limit > 0) ? (double)limit * 0.5 : 0.0;
                lastAppliedLimit = limit;
                LogMessage(L"[限速] %s 已更新: %lld KB/s",
                    isInbound ? L"下载" : L"上传", limit / 1024);
            }

            if (limit > 0) {
                // 上传 ACK/控制小包直接放行，防止 TCP 窗口卡死
                if (!isInbound && pktLen < 100) {
                    WinDivertSend(h, pkt, pktLen, NULL, &addr);
                    continue;
                }

                // 按时间补充令牌
                tokens += elapsed * (double)limit;

                // 突发上限：最多积累 0.5s 的配额
                double maxTokens = (double)limit * 0.5;
                if (tokens > maxTokens) tokens = maxTokens;

                if (tokens >= (double)pktLen) {
                    // 令牌充足：直接扣除，立即放行
                    tokens -= (double)pktLen;
                } else {
                    // 令牌不足：精确等待到令牌够用
                    double need = (double)pktLen - tokens;
                    double wait = need / (double)limit;

                    if (wait > 1.0) {
                        // 等待超过 1s 说明严重积压，直接丢包并重置令牌
                        tokens = 0.0;
                        continue;
                    }

                    PreciseSleep(wait);
                    // 睡眠结束：tokens 恰好够本包，清零继续
                    tokens = 0.0;
                    // 同步 lastTime，睡眠期间不应补充令牌
                    lastTime = GetQPC();
                }
            }
        } else {
            tokens           = 0.0;
            lastAppliedLimit = -1;
        }

        WinDivertSend(h, pkt, pktLen, NULL, &addr);
    }

    timeEndPeriod(1);
    WinDivertClose(h);
    free(pkt);
    return 0;
}

DWORD WINAPI InboundWorkerThread(LPVOID p) { return RateLimiterCore(true); }
DWORD WINAPI OutboundWorkerThread(LPVOID p) { return RateLimiterCore(false); }
