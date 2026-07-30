//
//  main.cpp — fire_dictd.exe 后台查字进程入口
//
//  职责（详见 docs/dict-ipc-design.md §6）：
//    - 全局命名 mutex 保证单实例（同一登录会话只跑一个后台）；
//    - 构造 DictServer 并 Init()（正常 IL 打开词库/统计库）；
//    - accept 循环：等待客户端连接，每连接起线程调 ServeConnection；
//    - 空闲超时退出（长时间无连接自动结束，降低常驻开销）。
//
#include <windows.h>

#include <atomic>
#include <thread>

#include "DictServer.h"
#include "NamedPipeServer.h"
#include "../common/IpcShared.h"

namespace {

// 单次等待连接的超时（毫秒）。到点无连接则累计空闲，超过总空闲上限退出。
constexpr DWORD kAcceptTimeoutMs = 30 * 1000;      // 每轮等待 30s
constexpr int   kMaxIdleRounds   = 20;             // 连续空闲 20 轮（约 10 分钟）退出

// 活动连接计数：有连接在服务时不因空闲退出。
std::atomic<int> g_activeConnections{0};

}  // namespace

int wmain() {
    // 单实例：同一会话若已有后台在跑则直接退出。
    HANDLE mutex = CreateMutexW(nullptr, TRUE, firewin::MakeDictdMutexName().c_str());
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    firewin::DictServer server;
    server.Init();  // 词库/统计库打开；即便词库未打开也继续（客户端会据 ready 降级）

    firewin::NamedPipeServer pipeServer;
    const std::wstring pipeName = firewin::MakeDictPipeName();

    int idleRounds = 0;
    for (;;) {
        bool timedOut = false;
        HANDLE pipe = pipeServer.WaitForConnection(pipeName, kAcceptTimeoutMs, timedOut);

        if (pipe != INVALID_HANDLE_VALUE) {
            idleRounds = 0;
            g_activeConnections.fetch_add(1);
            std::thread([&server, pipe]() {
                server.ServeConnection(pipe);
                CloseHandle(pipe);
                g_activeConnections.fetch_sub(1);
            }).detach();
            continue;
        }

        if (timedOut) {
            // 仅在无活动连接时累计空闲；有连接在服务则保持存活。
            if (g_activeConnections.load() == 0) {
                if (++idleRounds >= kMaxIdleRounds) break;
            } else {
                idleRounds = 0;
            }
            continue;
        }

        // 创建管道失败等异常：短暂退避后重试，避免忙循环。
        Sleep(1000);
    }

    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
