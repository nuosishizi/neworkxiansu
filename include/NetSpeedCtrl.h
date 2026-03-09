#ifndef NET_SPEED_CTRL_H
#define NET_SPEED_CTRL_H

#include <stdint.h>
#include <stdbool.h>
#include <windows.h>

// 全局配置 (volatile 保证多线程可见)
extern volatile int64_t g_LimitInboundBytesPerSec;
extern volatile int64_t g_LimitOutboundBytesPerSec;
extern volatile bool g_EnableLimit;
extern volatile bool g_IsRunning;
extern volatile bool g_TimePeriodMatch;

// 独立的变更标志，确保两个线程都能正确捕获并重置
extern volatile bool g_InLimitChanged;
extern volatile bool g_OutLimitChanged;

// 实时网速反馈 (字节/秒)
extern volatile int64_t g_CurrentInboundSpeed;
extern volatile int64_t g_CurrentOutboundSpeed;

// 配置变量
extern int g_StartH, g_StartM, g_EndH, g_EndM;
extern volatile int g_UnitMultiplier;

// 导出函数
void LogMessage(const wchar_t *format, ...);
bool IsCurrentTimeInPeriod();
void AddRegistryStartup(bool enable);

DWORD WINAPI InboundWorkerThread(LPVOID lpParam);
DWORD WINAPI OutboundWorkerThread(LPVOID lpParam);
DWORD WINAPI TimeMonitorThread(LPVOID lpParam);

#endif
