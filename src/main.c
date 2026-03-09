#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "NetSpeedCtrl.h"

#include <shellapi.h>

// ── 控件 ID ────────────────────────────────────────────────────
#define ID_CHK_LIMIT   1101
#define ID_CHK_STARTUP 1102
#define ID_EDT_TIME    1103
#define ID_TRK_IN      1104
#define ID_TRK_OUT     1105
#define ID_EDT_IN      1106
#define ID_EDT_OUT     1107
#define ID_RAD_KB      1108
#define ID_RAD_MB      1109
#define ID_TIMER_UI    1200

// ── 托盘相关 ────────────────────────────────────────────────────
#define WM_TRAYICON        (WM_USER + 1)
#define ID_TRAY_ICON       2000
#define ID_TRAY_SHOW       2001
#define ID_TRAY_EXIT       2002

// ── 全局句柄 ───────────────────────────────────────────────────
HWND g_hLogEdit = NULL;
HWND hTrkIn, hTrkOut;
HWND hChkLimit, hChkStartup;
HWND hEdtTime, hEdtIn, hEdtOut;
HWND hRadKb, hRadMb;
HWND hLabelSpeedIn, hLabelSpeedOut;
HWND hLabelLimitIn, hLabelLimitOut;

extern volatile int64_t g_CurrentInboundSpeed;
extern volatile int64_t g_CurrentOutboundSpeed;
extern bool IsStartupRegistered(void);

bool g_isSyncing = false;
static NOTIFYICONDATAW g_nid;   // 托盘图标数据
static HWND            g_hWnd;  // 主窗口句柄（托盘操作用）

// ── 字体句柄 ───────────────────────────────────────────────────
static HFONT hFontNormal = NULL;  // 普通 UI 字体
static HFONT hFontSpeed  = NULL;  // 速度显示（大 + 粗）
static HFONT hFontLabel  = NULL;  // 限额标签（中等粗）

// ── EnumChildWindows 回调：统一设置字体 ────────────────────────
static BOOL CALLBACK SetFontProc(HWND hWnd, LPARAM lParam) {
    SendMessageW(hWnd, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

// ── 更新限额标签 ───────────────────────────────────────────────
void UpdateConfigLabels(void) {
    wchar_t b[160];
    int vIn  = (int)SendMessageW(hTrkIn,  TBM_GETPOS, 0, 0);
    int vOut = (int)SendMessageW(hTrkOut, TBM_GETPOS, 0, 0);

    if (g_UnitMultiplier > 1024) {
        swprintf(b, 160, L"下载限额：%d MB/s  (%d Mbps)",  vIn,  vIn  * 8);
        SetWindowTextW(hLabelLimitIn,  b);
        swprintf(b, 160, L"上传限额：%d MB/s  (%d Mbps)", vOut, vOut * 8);
        SetWindowTextW(hLabelLimitOut, b);
    } else {
        swprintf(b, 160, L"下载限额：%d KB/s  (%.1f Mbps)",  vIn,  (vIn  * 8) / 1024.0);
        SetWindowTextW(hLabelLimitIn,  b);
        swprintf(b, 160, L"上传限额：%d KB/s  (%.1f Mbps)", vOut, (vOut * 8) / 1024.0);
        SetWindowTextW(hLabelLimitOut, b);
    }
}

// ── 提交限速值给引擎 ───────────────────────────────────────────
void CommitNewLimits(void) {
    int vIn  = (int)SendMessageW(hTrkIn,  TBM_GETPOS, 0, 0);
    int vOut = (int)SendMessageW(hTrkOut, TBM_GETPOS, 0, 0);
    g_LimitInboundBytesPerSec  = (int64_t)vIn  * g_UnitMultiplier;
    g_LimitOutboundBytesPerSec = (int64_t)vOut * g_UnitMultiplier;
    // 注意：不再使用 g_InLimitChanged/g_OutLimitChanged 标志
    // 每个限速线程通过对比 lastAppliedLimit 自动检测限速值变化
    UpdateConfigLabels();
}

// ── 滑块 → 文本框同步 ─────────────────────────────────────────
void SyncSlider(void) {
    if (g_isSyncing) return;
    g_isSyncing = true;
    wchar_t b[16];
    swprintf(b, 16, L"%d", (int)SendMessageW(hTrkIn,  TBM_GETPOS, 0, 0));
    SetWindowTextW(hEdtIn,  b);
    swprintf(b, 16, L"%d", (int)SendMessageW(hTrkOut, TBM_GETPOS, 0, 0));
    SetWindowTextW(hEdtOut, b);
    CommitNewLimits();
    g_isSyncing = false;
}

// ── 文本框 → 滑块同步 ─────────────────────────────────────────
void SyncEdit(HWND hE, HWND hT) {
    if (g_isSyncing) return;
    g_isSyncing = true;
    wchar_t b[16];
    GetWindowTextW(hE, b, 16);
    SendMessageW(hT, TBM_SETPOS, TRUE, _wtoi(b));
    CommitNewLimits();
    g_isSyncing = false;
}

// ── 添加托盘图标 ───────────────────────────────────────────────
static void TrayAdd(HWND hwnd, HINSTANCE hInst) {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = ID_TRAY_ICON;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    // 使用系统内置图标（网络相关），无需外部资源文件
    g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, 128, L"网速限制器 - 运行中");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

// ── 移除托盘图标 ───────────────────────────────────────────────
static void TrayRemove(void) {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

// ── 更新托盘提示文字（显示当前速度）──────────────────────────
static void TrayUpdateTip(void) {
    wchar_t tip[128];
    double dl = g_CurrentInboundSpeed  / 1048576.0;
    double ul = g_CurrentOutboundSpeed / 1048576.0;
    if (dl < 1.0 || ul < 1.0)
        swprintf(tip, 128, L"网速限制器\n↓ %.0f KB/s  ↑ %.0f KB/s",
            g_CurrentInboundSpeed / 1024.0, g_CurrentOutboundSpeed / 1024.0);
    else
        swprintf(tip, 128, L"网速限制器\n↓ %.2f MB/s  ↑ %.2f MB/s", dl, ul);
    wcscpy_s(g_nid.szTip, 128, tip);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// ── 显示托盘右键菜单 ───────────────────────────────────────────
static void TrayShowMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"显示主界面");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出程序");

    // 必须先把窗口设为前台，菜单才能正常消失
    SetForegroundWindow(hwnd);
    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

// ══════════════════════════════════════════════════════════════════
//  窗口过程
// ══════════════════════════════════════════════════════════════════
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    // ── 创建所有控件 ───────────────────────────────────────────
    case WM_CREATE: {
        /*
         *  字体：DEFAULT_CHARSET 支持所有语言（中/英/日/阿拉伯/西里尔等）
         *  Segoe UI 是 Windows Vista+ 内置多语言 UI 字体
         *  负高度 = 点数转像素（由 GDI 自动换算）
         */
        hFontNormal = CreateFontW(
            -18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            GB2312_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"微软雅黑");

        hFontSpeed = CreateFontW(
            -24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Consolas");

        hFontLabel = CreateFontW(
            -17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            GB2312_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"微软雅黑");

        // ────────────────────────────────────────────────────────
        //  § 1  网络监控区
        // ────────────────────────────────────────────────────────
        CreateWindowExW(0, L"BUTTON", L" 网络监控 ",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            10, 8, 504, 84, hwnd, NULL, NULL, NULL);

        hChkLimit = CreateWindowExW(0, L"BUTTON", L"启用限速",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            24, 38, 110, 28, hwnd, (HMENU)ID_CHK_LIMIT, NULL, NULL);

        hLabelSpeedIn = CreateWindowExW(0, L"STATIC", L"下载  0.00 MB/s",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            140, 32, 178, 36, hwnd, NULL, NULL, NULL);

        hLabelSpeedOut = CreateWindowExW(0, L"STATIC", L"上传  0.00 MB/s",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            326, 32, 178, 36, hwnd, NULL, NULL, NULL);

        // ────────────────────────────────────────────────────────
        //  § 2  下载限速区
        // ────────────────────────────────────────────────────────
        CreateWindowExW(0, L"BUTTON", L" 下载限速 ",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            10, 102, 504, 92, hwnd, NULL, NULL, NULL);

        hLabelLimitIn = CreateWindowExW(0, L"STATIC", L"下载限额：17 MB/s  (136 Mbps)",
            WS_CHILD | WS_VISIBLE,
            24, 122, 480, 24, hwnd, NULL, NULL, NULL);

        hEdtIn = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"17",
            WS_CHILD | WS_VISIBLE | ES_NUMBER,
            24, 152, 88, 30, hwnd, (HMENU)ID_EDT_IN, NULL, NULL);

        hTrkIn = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_TOOLTIPS,
            120, 150, 385, 34, hwnd, (HMENU)ID_TRK_IN, NULL, NULL);
        SendMessageW(hTrkIn, TBM_SETRANGE,   TRUE, MAKELONG(0, 100));
        SendMessageW(hTrkIn, TBM_SETTICFREQ, 10,   0);
        SendMessageW(hTrkIn, TBM_SETPOS,     TRUE, 17);

        // ────────────────────────────────────────────────────────
        //  § 3  上传限速区
        // ────────────────────────────────────────────────────────
        CreateWindowExW(0, L"BUTTON", L" 上传限速 ",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            10, 204, 504, 92, hwnd, NULL, NULL, NULL);

        hLabelLimitOut = CreateWindowExW(0, L"STATIC", L"上传限额：17 MB/s  (136 Mbps)",
            WS_CHILD | WS_VISIBLE,
            24, 224, 480, 24, hwnd, NULL, NULL, NULL);

        hEdtOut = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"17",
            WS_CHILD | WS_VISIBLE | ES_NUMBER,
            24, 254, 88, 30, hwnd, (HMENU)ID_EDT_OUT, NULL, NULL);

        hTrkOut = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_TOOLTIPS,
            120, 252, 385, 34, hwnd, (HMENU)ID_TRK_OUT, NULL, NULL);
        SendMessageW(hTrkOut, TBM_SETRANGE,   TRUE, MAKELONG(0, 100));
        SendMessageW(hTrkOut, TBM_SETTICFREQ, 10,   0);
        SendMessageW(hTrkOut, TBM_SETPOS,     TRUE, 17);

        // ────────────────────────────────────────────────────────
        //  § 4  设置区
        // ────────────────────────────────────────────────────────
        CreateWindowExW(0, L"BUTTON", L" 设置 ",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            10, 306, 504, 58, hwnd, NULL, NULL, NULL);

        hChkStartup = CreateWindowExW(0, L"BUTTON", L"开机自启",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            24, 328, 100, 28, hwnd, (HMENU)ID_CHK_STARTUP, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"生效时段：",
            WS_CHILD | WS_VISIBLE,
            140, 332, 80, 20, hwnd, NULL, NULL, NULL);

        hEdtTime = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"00:00-23:59",
            WS_CHILD | WS_VISIBLE,
            224, 328, 112, 28, hwnd, (HMENU)ID_EDT_TIME, NULL, NULL);

        hRadKb = CreateWindowExW(0, L"BUTTON", L"KB/s",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            352, 328, 68, 28, hwnd, (HMENU)ID_RAD_KB, NULL, NULL);

        hRadMb = CreateWindowExW(0, L"BUTTON", L"MB/s",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            426, 328, 68, 28, hwnd, (HMENU)ID_RAD_MB, NULL, NULL);

        // 默认 MB/s，与 17 MB/s 默认值对应
        SendMessageW(hRadMb, BM_SETCHECK, BST_CHECKED, 0);
        g_UnitMultiplier = 1048576;

        // ────────────────────────────────────────────────────────
        //  § 5  日志区
        // ────────────────────────────────────────────────────────
        g_hLogEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            10, 374, 504, 82, hwnd, NULL, NULL, NULL);

        // ── 统一字体（使用 EnumChildWindows + 正确回调）────────
        EnumChildWindows(hwnd, SetFontProc, (LPARAM)hFontNormal);

        // 速度标签和限额标签单独覆盖为对应字体
        SendMessageW(hLabelSpeedIn,  WM_SETFONT, (WPARAM)hFontSpeed, TRUE);
        SendMessageW(hLabelSpeedOut, WM_SETFONT, (WPARAM)hFontSpeed, TRUE);
        SendMessageW(hLabelLimitIn,  WM_SETFONT, (WPARAM)hFontLabel, TRUE);
        SendMessageW(hLabelLimitOut, WM_SETFONT, (WPARAM)hFontLabel, TRUE);

        SetTimer(hwnd, ID_TIMER_UI, 500, NULL);
        UpdateConfigLabels();

        // 默认勾选"启用限速"并立即生效
        SendMessageW(hChkLimit, BM_SETCHECK, BST_CHECKED, 0);
        g_EnableLimit = true;

        // 开机自启处理：
        // 若任务计划已存在 → 静默刷新路径（处理 exe 被移动的情况）+ 勾选复选框
        // 若不存在 → 默认勾选，等用户确认（首次注册需要一次 UAC）
        if (IsStartupRegistered()) {
            AddRegistryStartup(true);  // 刷新为当前路径
            SendMessageW(hChkStartup, BM_SETCHECK, BST_CHECKED, 0);
        } else {
            // 默认勾选并注册（首次会弹一次 UAC 确认）
            SendMessageW(hChkStartup, BM_SETCHECK, BST_CHECKED, 0);
            AddRegistryStartup(true);
        }

        // 添加托盘图标
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
        TrayAdd(hwnd, hInst);
        break;
    }

    // ── 定时刷新速度 ───────────────────────────────────────────
    case WM_TIMER: {
        wchar_t b[128];
        double dlMB = g_CurrentInboundSpeed  / 1048576.0;
        double ulMB = g_CurrentOutboundSpeed / 1048576.0;

        // 自动量程：< 1 MB/s 显示 KB/s，提高可读性
        if (dlMB < 1.0)
            swprintf(b, 128, L"下载  %.0f KB/s", g_CurrentInboundSpeed / 1024.0);
        else
            swprintf(b, 128, L"下载  %.2f MB/s", dlMB);
        SetWindowTextW(hLabelSpeedIn, b);

        if (ulMB < 1.0)
            swprintf(b, 128, L"上传  %.0f KB/s", g_CurrentOutboundSpeed / 1024.0);
        else
            swprintf(b, 128, L"上传  %.2f MB/s", ulMB);
        SetWindowTextW(hLabelSpeedOut, b);
        TrayUpdateTip();  // 同步更新托盘提示
        break;
    }

    // ── 滑块拖动 ───────────────────────────────────────────────
    case WM_HSCROLL:
        SyncSlider();
        break;

    // ── 控件事件 ───────────────────────────────────────────────
    // ── 点 X：最小化到托盘而非退出 ────────────────────────────
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;  // 阻止默认的销毁行为

    // ── 托盘图标消息 ───────────────────────────────────────────
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK) {
            // 双击：恢复主窗口
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        } else if (lParam == WM_RBUTTONUP) {
            // 右键：弹出菜单
            TrayShowMenu(hwnd);
        }
        break;

    case WM_COMMAND: {
        int id   = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == ID_TRAY_SHOW) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        else if (id == ID_TRAY_EXIT) {
            // 真正退出：移除托盘图标，停止线程，销毁窗口
            TrayRemove();
            g_IsRunning = false;
            DestroyWindow(hwnd);
        }
        else if (id == ID_CHK_LIMIT) {
            g_EnableLimit = (IsDlgButtonChecked(hwnd, id) == BST_CHECKED);
        }
        else if (id == ID_CHK_STARTUP) {
            AddRegistryStartup(IsDlgButtonChecked(hwnd, id) == BST_CHECKED);
        }
        else if (id == ID_RAD_KB || id == ID_RAD_MB) {
            g_UnitMultiplier = (id == ID_RAD_MB) ? 1048576 : 1024;
            int maxVal = (id == ID_RAD_MB) ? 100 : 10000;
            SendMessageW(hTrkIn,  TBM_SETRANGE, TRUE, MAKELONG(0, maxVal));
            SendMessageW(hTrkOut, TBM_SETRANGE, TRUE, MAKELONG(0, maxVal));
            CommitNewLimits();
        }
        else if (code == EN_CHANGE && !g_isSyncing) {
            if      (id == ID_EDT_IN)   SyncEdit(hEdtIn,  hTrkIn);
            else if (id == ID_EDT_OUT)  SyncEdit(hEdtOut, hTrkOut);
            else if (id == ID_EDT_TIME) {
                wchar_t b[64];
                GetWindowTextW(hEdtTime, b, 64);
                int sh, sm, eh, em;
                if (swscanf(b, L"%d:%d-%d:%d", &sh, &sm, &eh, &em) == 4) {
                    g_StartH = sh; g_StartM = sm;
                    g_EndH   = eh; g_EndM   = em;
                }
            }
        }
        break;
    }

    case WM_DESTROY:
        TrayRemove();
        g_IsRunning = false;
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// ══════════════════════════════════════════════════════════════════
//  程序入口
// ══════════════════════════════════════════════════════════════════
int WINAPI WinMain(HINSTANCE h, HINSTANCE hp, LPSTR lp, int n) {
    // 高 DPI 感知，防止在 125%/150% 缩放时模糊
    SetProcessDPIAware();

    // 初始化公共控件（TrackBar 必须）
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {
        sizeof(WNDCLASSEXW),
        CS_HREDRAW | CS_VREDRAW,
        WndProc, 0, 0, h,
        NULL,
        LoadCursor(NULL, IDC_ARROW),
        (HBRUSH)(COLOR_BTNFACE + 1),
        NULL,
        L"NetSpeedCtrlClass",
        NULL
    };
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        L"NetSpeedCtrlClass",
        L"WinDivert 网速限制器",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        540, 510,
        NULL, NULL, h, NULL);

    ShowWindow(hwnd, SW_HIDE);  // 启动时直接隐藏到托盘，不弹窗口
    UpdateWindow(hwnd);

    // 初始化默认限速值为 17 MB/s
    g_LimitInboundBytesPerSec  = (int64_t)17 * 1048576;
    g_LimitOutboundBytesPerSec = (int64_t)17 * 1048576;

    g_IsRunning = true;
    CreateThread(NULL, 0, InboundWorkerThread,  NULL, 0, NULL);
    CreateThread(NULL, 0, OutboundWorkerThread, NULL, 0, NULL);
    CreateThread(NULL, 0, TimeMonitorThread,    NULL, 0, NULL);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
