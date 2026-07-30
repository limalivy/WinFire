//
//  NamedPipeServer.h — 带 SDDL ACL 的命名管道 server 封装（后台进程用）
//
//  职责：
//    - 用 §5.2 SDDL 构造 SECURITY_ATTRIBUTES（放开 AppContainer + 低 IL 客户端）；
//    - CreateNamedPipeW 建实例（PIPE_TYPE_MESSAGE，多实例），overlapped ConnectNamedPipe
//      配合超时等待连接（支持空闲超时退出）；
//    - 每个连接返回一个已连接的管道句柄，由调用方起线程处理。
//
#pragma once

#include <windows.h>

#include <string>

namespace firewin {

class NamedPipeServer {
public:
    NamedPipeServer() = default;
    ~NamedPipeServer();

    NamedPipeServer(const NamedPipeServer&) = delete;
    NamedPipeServer& operator=(const NamedPipeServer&) = delete;

    // 建一个新的管道实例并等待客户端连接，最多等待 timeoutMs 毫秒
    // （INFINITE 表示无限等待）。
    //   - 成功连接：返回已连接的管道句柄（调用方负责用完 CloseHandle）；
    //   - 超时：返回 INVALID_HANDLE_VALUE 且 timedOut=true（调用方据此判断空闲退出）；
    //   - 出错：返回 INVALID_HANDLE_VALUE 且 timedOut=false。
    // pipeName 形如 \\.\pipe\WinFire_Dict_<会话>。
    HANDLE WaitForConnection(const std::wstring& pipeName, DWORD timeoutMs, bool& timedOut);

private:
    // 用 SDDL 构造 SECURITY_ATTRIBUTES；成功后 out_sd 需在管道创建后 LocalFree。
    bool BuildSecurityAttributes(SECURITY_ATTRIBUTES& sa, PSECURITY_DESCRIPTOR& out_sd);
};

}  // namespace firewin
