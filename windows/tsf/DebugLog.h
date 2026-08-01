//
//  DebugLog.h — 受 FIRE_DEBUG 宏开关控制的日志工具（文件落盘版）
//
//  用法：
//    FIRE_LOG(L"[WinFire] Activate: enter\n");
//    FIRE_LOG_ENTER();                  // 自动打印函数名
//    FIRE_LOG_HR(hr, L"AdviseSink"); // 打印 HRESULT
//
//  开启日志：在 Debug 配置编译选项中定义 FIRE_DEBUG 宏（已在 fire_tsf.vcxproj 中配置）。
//  Release 配置下所有日志调用编译为空指令，零开销。
//
//  日志文件路径（固定）：
//    %LOCALAPPDATA%\WinFire\logs\fire_tsf_<pid>.log
//  按进程 ID 分文件，避免多进程同时写入冲突；每次写后立即 fflush+fclose，
//  确保进程崩溃/桌面黑屏前的日志已落盘，不会丢失。
//
//  重要：DllMain 阶段（DLL_PROCESS_ATTACH）持有加载器锁（loader lock），
//  此时调用 CRT 文件 I/O（_wfopen_s/fflush/fclose）会导致死锁或崩溃。
//  因此用 g_fireLogFsReady 标志区分：DllMain 阶段只做 OutputDebugStringW，
//  Activate 之后才启用文件 I/O。
//
#pragma once

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "Version.h"  // FIRE_VER_STRING（构建期从 VERSION 生成）

namespace firewin {

#ifdef FIRE_DEBUG

// 文件 I/O 是否就绪标志。DllMain 阶段为 false，Activate 时设为 true。
// false 时 FireLogV 只做 OutputDebugStringW（加载器锁下安全），不做文件 I/O。
inline bool g_fireLogFsReady = false;
inline void FireLogSetFsReady(bool v) { g_fireLogFsReady = v; }

// 计算日志文件路径： %LOCALAPPDATA%\WinFire\logs\fire_tsf_<pid>.log
// 用 GetEnvironmentVariableW 取 LOCALAPPDATA，避免在 DllMain 早期依赖 shlobj.h。
inline std::wstring FireLogFilePath() {
    wchar_t base[MAX_PATH] = {0};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    std::wstring dir;
    if (n > 0 && n < MAX_PATH) {
        dir = std::wstring(base) + L"\\WinFire\\logs";
    } else {
        // 回退到临时目录
        wchar_t temp[MAX_PATH] = {0};
        GetTempPathW(MAX_PATH, temp);
        dir = std::wstring(temp) + L"WinFire_logs";
    }
    CreateDirectoryW(dir.c_str(), nullptr);  // 已存在则忽略错误
    return dir + L"\\fire_tsf_" + std::to_wstring(GetCurrentProcessId()) + L".log";
}

// 实际输出函数（仅在 FIRE_DEBUG 下编译）
// DllMain 阶段（g_fireLogFsReady=false）：只做 OutputDebugStringW，不做文件 I/O。
// Activate 之后（g_fireLogFsReady=true）：每次写后立即 fflush+fclose，确保崩溃前数据落盘。
inline void FireLogV(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
    va_end(args);

    // 始终输出到调试器（DebugView 可观察，加载器锁下安全）
    OutputDebugStringW(buf);

    // DllMain 阶段不做文件 I/O，避免加载器锁死锁/崩溃
    if (!g_fireLogFsReady) return;

    // 写入文件（追加模式，每次写完关闭，确保落盘）
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, FireLogFilePath().c_str(), L"a, ccs=UTF-8") == 0 && fp) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fwprintf(fp, L"[%02d:%02d:%02d.%03d][pid=%lu] %s",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                 (unsigned long)GetCurrentProcessId(), buf);
        fflush(fp);
        fclose(fp);
    }
}

// 取当前线程 ID（便于排查线程问题）
inline DWORD FireLogTid() {
    return GetCurrentThreadId();
}

// 诊断横幅：打印 WinFire 版本号 + 宿主 exe 路径与文件版本 + PID。
// 供 DbgView 在会话开头识别运行环境（哪个宿主、哪个版本）。
inline void FireLogDiagBanner() {
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    // 取宿主 exe 文件版本（FileVersion / ProductVersion）。
    // 先查 Translation 表拿语言码，再拼 StringFileInfo 子块。
    std::wstring fileVer = L"(n/a)";
    std::wstring prodVer = L"(n/a)";
    DWORD dummy = 0;
    DWORD sz = GetFileVersionInfoSizeW(exePath, &dummy);
    if (sz > 0) {
        std::vector<BYTE> vi(sz);
        if (GetFileVersionInfoW(exePath, 0, sz, vi.data())) {
            UINT len = 0;
            LPWCH buf = nullptr;
            if (VerQueryValueW(vi.data(), L"\\VarFileInfo\\Translation",
                               (LPVOID*)&buf, &len) && len >= sizeof(WORD) * 2) {
                WORD* p = (WORD*)buf;
                wchar_t sub[64];
                LPWCH val = nullptr; UINT vlen = 0;
                _snwprintf_s(sub, _countof(sub), _TRUNCATE,
                             L"\\StringFileInfo\\%04x%04x\\FileVersion", p[0], p[1]);
                if (VerQueryValueW(vi.data(), sub, (LPVOID*)&val, &vlen) && val && vlen > 0) {
                    fileVer = val;
                }
                _snwprintf_s(sub, _countof(sub), _TRUNCATE,
                             L"\\StringFileInfo\\%04x%04x\\ProductVersion", p[0], p[1]);
                if (VerQueryValueW(vi.data(), sub, (LPVOID*)&val, &vlen) && val && vlen > 0) {
                    prodVer = val;
                }
            }
        }
    }

    // _CRT_WIDE 把 FIRE_VER_STRING 字面量 widening 成宽字符串嵌入格式串。
    FireLogV(L"[WinFire] ===== DIAG BANNER ===== WinFire v" _CRT_WIDE(FIRE_VER_STRING)
             L"  host=\"%s\"  FileVersion=%s  ProductVersion=%s  pid=%lu =====\n",
             exePath, fileVer.c_str(), prodVer.c_str(),
             (unsigned long)GetCurrentProcessId());
}

// 每进程只打印一次横幅（由 Activate 调用）。不同宿主进程各自独立打印。
inline void FireLogDiagBannerOnce() {
    static bool printed = false;
    if (printed) return;
    printed = true;
    FireLogDiagBanner();
}

#endif // FIRE_DEBUG

// Release 模式下 FireLogSetFsReady / FireLogDiagBannerOnce 为空操作，便于
// TextService::Activate/Deactivate 无条件调用，避免业务代码用 #ifdef 包裹
#ifndef FIRE_DEBUG
inline void FireLogSetFsReady(bool) {}
inline void FireLogDiagBannerOnce() {}
#endif

}  // namespace firewin

// ---- 日志宏 ----
#ifdef FIRE_DEBUG
// 基本日志：FIRE_LOG(L"[WinFire] xxx: %d\n", value)
#define FIRE_LOG(...) ::firewin::FireLogV(__VA_ARGS__)
// 函数入口日志：自动打印 [WinFire] >>> FunctionName [tid=xxx]
#define FIRE_LOG_ENTER() \
    ::firewin::FireLogV(L"[WinFire] >>> %hs [tid=%lu]\n", __FUNCTION__, ::firewin::FireLogTid())
// 函数出口日志
#define FIRE_LOG_EXIT() \
    ::firewin::FireLogV(L"[WinFire] <<< %hs [tid=%lu]\n", __FUNCTION__, ::firewin::FireLogTid())
// HRESULT 日志：FIRE_LOG_HR(hr, L"AdviseSink")
#define FIRE_LOG_HR(hr, what) \
    ::firewin::FireLogV(L"[WinFire] %hs hr=0x%08lX %s\n", __FUNCTION__, (unsigned long)(hr), (what))
// 带前缀的日志：FIRE_LOG_TAG(L"InitEngine", L"step: dict created\n")
#define FIRE_LOG_TAG(tag, ...) \
    ::firewin::FireLogV(L"[WinFire] [%s] " __VA_ARGS__, L##tag)
#else
#define FIRE_LOG(...)   ((void)0)
#define FIRE_LOG_ENTER() ((void)0)
#define FIRE_LOG_EXIT()  ((void)0)
#define FIRE_LOG_HR(hr, what) ((void)0)
#define FIRE_LOG_TAG(tag, ...) ((void)0)
#endif
