//
//  main.cpp — fire_dictd.exe 后台查字进程入口
//
//  职责（详见 docs/dict-ipc-design.md §6）：
//    - 全局命名 mutex 保证单实例（同一登录会话只跑一个后台）；
//    - 构造 DictServer 并 Init()（正常 IL 打开词库/统计库）；
//    - accept 循环：等待客户端连接，每连接起线程调 ServeConnection；
//    - 常驻服务（不再空闲退出，保证系统进程拉起场景下后台始终可用）。
//
//  命令行 --reload-config：单实例已运行时，向其发 ReloadConfig IPC（异步 fire-and-forget）
//  触发从磁盘重读 config.json，随后退出。供 install.ps1 在外部改了 config.json 后调用，
//  使配置立即生效（无需重启 dictd）。这是 config 收敛方案下唯一的外部触发入口。
//
//  Windows GUI 子系统入口（wWinMain）：经 HKCU\Run / HKLM\Run 自启动或安装脚本拉起时，
//  不弹控制台窗口。诊断日志走 OutputDebugStringW（DLOG），不依赖控制台。
//
#include <windows.h>

#include <cstring>
#include <thread>
#include <vector>

#include "DictLog.h"
#include "DictServer.h"
#include "NamedPipeServer.h"
#include "../common/IpcShared.h"
#include "fire/ipc/protocol.h"

namespace {
// 向已运行的 dictd 实例发 ReloadConfig（异步 fire-and-forget）。
// 复用命名管道客户端的极简实现：连管道 → 写一帧 → 关闭，不等响应。
bool SendReloadConfigToRunning() {
    std::wstring name = firewin::MakeDictPipeName();
    HANDLE h = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    // 构造 ReloadConfig 帧（header + payload[source]）。
    fire::ipc::ReloadConfigRequest req;
    req.source = "cmdline";
    std::vector<uint8_t> payload = fire::ipc::encode_reload_config_request(req);
    std::vector<uint8_t> frame = fire::ipc::build_frame(fire::ipc::MsgType::ReloadConfig, 1, payload);
    DWORD written = 0;
    BOOL ok = WriteFile(h, frame.data(), (DWORD)frame.size(), &written, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    return ok && written == frame.size();
}

bool HasReloadConfigArg() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    bool found = false;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--reload-config") == 0) { found = true; break; }
    }
    LocalFree(argv);
    return found;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    firewin::DictLogBannerOnce();  // 打印版本横幅（每进程一次）

    // --reload-config：向已运行实例发 ReloadConfig IPC 后退出（自身不常驻）。
    if (HasReloadConfigArg()) {
        bool ok = SendReloadConfigToRunning();
        DLOG(L"wWinMain: --reload-config sent to running instance -> %hs\n",
             ok ? "OK" : "FAIL (no instance running?)");
        return ok ? 0 : 1;
    }

    // 单实例：同一会话若已有后台在跑则直接退出。
    HANDLE mutex = CreateMutexW(nullptr, TRUE, firewin::MakeDictdMutexName().c_str());
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        DLOG(L"wWinMain: another instance already running (mutex exists), exit\n");
        CloseHandle(mutex);
        return 0;
    }
    DLOG(L"wWinMain: singleton mutex acquired, starting\n");

    firewin::DictServer server;
    // 异步初始化词库：冷启动时打开 sqlite（磁盘冷缓存）可能 >1s，
    // 同步 Init 会让管道创建延后，导致首个客户端 EnsureConnected 1s 重试窗口超时。
    // 改为后台 Init，主线程立即创建管道 accept；HandleRequest 中 if(dict_) 保护
    // 未就绪时返回空结果，客户端 available_=true 不降级，几秒后词库就绪即可出候选。
    std::thread initThread([&server]() { server.Init(); });

    firewin::NamedPipeServer pipeServer;
    const std::wstring pipeName = firewin::MakeDictPipeName();

    for (;;) {
        bool timedOut = false;
        // 常驻：永久等待连接，不因空闲退出。系统进程（SearchHost.exe 等）拉起输入法时
        // 无权 CreateProcess，必须保证 dictd 始终在听管道，否则沙箱场景永远拉不起后台。
        HANDLE pipe = pipeServer.WaitForConnection(pipeName, INFINITE, timedOut);

        if (pipe != INVALID_HANDLE_VALUE) {
            DLOG(L"accept: client connected, spawning ServeConnection\n");
            std::thread([&server, pipe]() {
                // detached 线程未捕获异常会 std::terminate 整个进程，连累 accept 主循环与
                // 其他在服务的连接。这里捕获后仅结束本连接线程，主进程继续存活。
                try {
                    server.ServeConnection(pipe);
                } catch (...) {
                    DLOG(L"ServeConnection: exception caught, isolating this connection\n");
                }
                DLOG(L"ServeConnection: finished, disconnecting pipe\n");
                CloseHandle(pipe);
            }).detach();
            continue;
        }

        // 创建管道失败等异常：短暂退避后重试，避免忙循环。
        Sleep(1000);
    }

    // 退出前确保 Init 线程不再访问 server（避免析构 UAF）。
    initThread.join();

    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
