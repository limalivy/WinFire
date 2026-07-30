//
//  NamedPipeServer.cpp
//
#include "NamedPipeServer.h"

#include <sddl.h>

#include "../common/IpcShared.h"

namespace firewin {

NamedPipeServer::~NamedPipeServer() = default;

bool NamedPipeServer::BuildSecurityAttributes(SECURITY_ATTRIBUTES& sa,
                                              PSECURITY_DESCRIPTOR& out_sd) {
    out_sd = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            kDictPipeSddl, SDDL_REVISION_1, &sd, nullptr)) {
        return false;
    }
    out_sd = sd;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;
    return true;
}

HANDLE NamedPipeServer::WaitForConnection(const std::wstring& pipeName, DWORD timeoutMs,
                                          bool& timedOut) {
    timedOut = false;

    SECURITY_ATTRIBUTES sa{};
    PSECURITY_DESCRIPTOR sd = nullptr;
    bool haveSa = BuildSecurityAttributes(sa, sd);

    HANDLE pipe = CreateNamedPipeW(
        pipeName.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        /*nOutBufferSize=*/64 * 1024,
        /*nInBufferSize=*/64 * 1024,
        /*nDefaultTimeOut=*/0,
        haveSa ? &sa : nullptr);

    if (sd) LocalFree(sd);
    if (pipe == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) {
        CloseHandle(pipe);
        return INVALID_HANDLE_VALUE;
    }

    BOOL connected = ConnectNamedPipe(pipe, &ov);
    DWORD err = GetLastError();

    if (!connected && err == ERROR_PIPE_CONNECTED) {
        // 客户端在 ConnectNamedPipe 之前已连上：直接可用。
        CloseHandle(ov.hEvent);
        // 该实例转回同步模式便于后续阻塞读写。
        SetNamedPipeHandleState(pipe, nullptr, nullptr, nullptr);
        return pipe;
    }
    if (!connected && err != ERROR_IO_PENDING) {
        CloseHandle(ov.hEvent);
        CloseHandle(pipe);
        return INVALID_HANDLE_VALUE;
    }

    // 等待连接完成或超时。
    DWORD wait = WaitForSingleObject(ov.hEvent, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        CancelIoEx(pipe, &ov);
        CloseHandle(ov.hEvent);
        CloseHandle(pipe);
        timedOut = true;
        return INVALID_HANDLE_VALUE;
    }
    if (wait != WAIT_OBJECT_0) {
        CloseHandle(ov.hEvent);
        CloseHandle(pipe);
        return INVALID_HANDLE_VALUE;
    }

    DWORD transferred = 0;
    BOOL ok = GetOverlappedResult(pipe, &ov, &transferred, FALSE);
    CloseHandle(ov.hEvent);
    if (!ok) {
        CloseHandle(pipe);
        return INVALID_HANDLE_VALUE;
    }

    // 连接已建立。后续读写用阻塞模式（同步）。
    SetNamedPipeHandleState(pipe, nullptr, nullptr, nullptr);
    return pipe;
}

}  // namespace firewin
