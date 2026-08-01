//
//  DictLog.h — fire_dictd.exe 后台查字进程诊断日志（OutputDebugStringW，DbgView 可见）
//
//  后台进程独立于 fire_tsf.dll，原本无日志。此头提供与 TSF 层 FIRE_LOG 对等的
//  轻量日志：仅 OutputDebugStringW（无文件 I/O，对后台进程足够），FIRE_DEBUG 下生效。
//
//  用法：
//    DLOG(L"Init: dict open took %lu ms\n", ms);
//    DLOG_ENTER();
//
//  开启：Debug 配置（fire_dictd.vcxproj 已定义 FIRE_DEBUG）。Release 下编译为空指令。
//  DbgView：管理员运行，勾选 Capture Win32 + Capture Global Win32，过滤 "[dictd]"。
//
#pragma once

#include <windows.h>
#include <cstdarg>
#include <cstdio>

#include "Version.h"  // FIRE_VER_STRING（构建期从 VERSION 生成）

namespace firewin {

#ifdef FIRE_DEBUG

// 毫秒时间戳（相对进程启动），便于看各阶段耗时与请求间隔。
inline ULONGLONG DictLogTick() {
    static ULONGLONG start = GetTickCount64();
    return GetTickCount64() - start;
}

inline DWORD DictLogTid() { return GetCurrentThreadId(); }

// 实际输出：[dictd][+Tms][pid=P tid=T] <msg>
inline void DictLogV(const wchar_t* fmt, ...) {
    wchar_t body[2048];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(body, _countof(body), _TRUNCATE, fmt, args);
    va_end(args);

    wchar_t line[2300];
    _snwprintf_s(line, _countof(line), _TRUNCATE,
                 L"[dictd][+%lums][pid=%lu tid=%lu] %s",
                 (unsigned long)DictLogTick(),
                 (unsigned long)GetCurrentProcessId(),
                 (unsigned long)DictLogTid(), body);
    OutputDebugStringW(line);
}

// 版本横幅：每进程一次。打印 WinFire 版本号 + pid，便于 DbgView 识别。
inline void DictLogBannerOnce() {
    static bool printed = false;
    if (printed) return;
    printed = true;
    DictLogV(L"===== dictd DIAG BANNER ===== WinFire v" _CRT_WIDE(FIRE_VER_STRING)
             L"  pid=%lu =====\n", (unsigned long)GetCurrentProcessId());
}

#endif // FIRE_DEBUG

}  // namespace firewin

// ---- 日志宏 ----
#ifdef FIRE_DEBUG
#define DLOG(...) ::firewin::DictLogV(__VA_ARGS__)
#define DLOG_ENTER() ::firewin::DictLogV(L">>> %hs\n", __FUNCTION__)
#else
#define DLOG(...)   ((void)0)
#define DLOG_ENTER() ((void)0)
#endif
