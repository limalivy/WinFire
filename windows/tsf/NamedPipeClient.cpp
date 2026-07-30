//
//  NamedPipeClient.cpp
//
#include "NamedPipeClient.h"

#include "../common/IpcShared.h"

namespace firewin {

NamedPipeClient::~NamedPipeClient() { Disconnect(); }

void NamedPipeClient::Disconnect() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool NamedPipeClient::TryConnect() {
    const std::wstring name = MakeDictPipeName();
    HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    // 客户端也用消息读模式，帧边界与 server 一致。
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
    pipe_ = h;
    return true;
}

bool NamedPipeClient::LaunchBackend() {
    // 频繁拉起保护：距上次拉起不足 3s 则跳过（后台可能仍在启动）。
    ULONGLONG now = GetTickCount64();
    if (lastLaunchTick_ != 0 && now - lastLaunchTick_ < 3000) return false;
    lastLaunchTick_ = now;

    // 定位与本 DLL 同目录下的 fire_dictd.exe。
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&LaunchBackend), &self)) {
        return false;
    }
    wchar_t path[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    std::wstring dir(path);
    size_t p = dir.find_last_of(L"\\/");
    if (p == std::wstring::npos) return false;
    std::wstring exe = dir.substr(0, p + 1) + kDictdExeName;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');
    BOOL ok = CreateProcessW(exe.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool NamedPipeClient::EnsureConnected() {
    if (IsConnected()) return true;
    if (TryConnect()) return true;

    // 连不上：拉起后台，短暂等待其就绪后重试若干次。
    LaunchBackend();
    for (int i = 0; i < 20; ++i) {  // 最多约 1s
        Sleep(50);
        if (TryConnect()) return true;
    }
    return false;
}

bool NamedPipeClient::WriteFrameOverlapped(fire::ipc::MsgType type, uint32_t requestId,
                                           const std::vector<uint8_t>& payload,
                                           DWORD timeoutMs) {
    std::vector<uint8_t> frame = fire::ipc::build_frame(type, requestId, payload);
    size_t off = 0;
    while (off < frame.size()) {
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return false;
        DWORD written = 0;
        BOOL ok = WriteFile(pipe_, frame.data() + off, (DWORD)(frame.size() - off), &written, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, timeoutMs);
            if (wait != WAIT_OBJECT_0) {
                CancelIoEx(pipe_, &ov);
                CloseHandle(ov.hEvent);
                return false;
            }
            ok = GetOverlappedResult(pipe_, &ov, &written, FALSE);
        }
        CloseHandle(ov.hEvent);
        if (!ok || written == 0) return false;
        off += written;
    }
    return true;
}

bool NamedPipeClient::ReadFrameOverlapped(fire::ipc::FrameHeader& hdr,
                                          std::vector<uint8_t>& payload, DWORD timeoutMs) {
    std::vector<uint8_t> buf(4096);
    size_t total = 0;
    for (;;) {
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return false;
        DWORD read = 0;
        BOOL ok = ReadFile(pipe_, buf.data() + total, (DWORD)(buf.size() - total), &read, &ov);
        DWORD err = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok && err == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, timeoutMs);
            if (wait != WAIT_OBJECT_0) {
                CancelIoEx(pipe_, &ov);
                CloseHandle(ov.hEvent);
                return false;  // 超时
            }
            ok = GetOverlappedResult(pipe_, &ov, &read, FALSE);
            err = ok ? ERROR_SUCCESS : GetLastError();
        }
        CloseHandle(ov.hEvent);
        total += read;
        if (ok) break;
        if (err == ERROR_MORE_DATA) {
            buf.resize(buf.size() * 2);
            continue;
        }
        return false;
    }
    if (total < fire::ipc::kHeaderSize) return false;
    if (!fire::ipc::decode_header(buf.data(), total, hdr)) return false;
    if (fire::ipc::kHeaderSize + (size_t)hdr.payload_len > total) return false;
    payload.assign(buf.begin() + fire::ipc::kHeaderSize,
                   buf.begin() + fire::ipc::kHeaderSize + hdr.payload_len);
    return true;
}

bool NamedPipeClient::SendRequest(fire::ipc::MsgType type,
                                  const std::vector<uint8_t>& requestPayload,
                                  fire::ipc::MsgType& responseType,
                                  std::vector<uint8_t>& responsePayload, DWORD timeoutMs) {
    if (!EnsureConnected()) return false;

    uint32_t reqId = nextRequestId_++;
    if (!WriteFrameOverlapped(type, reqId, requestPayload, timeoutMs)) {
        Disconnect();  // 标记需重连
        return false;
    }
    fire::ipc::FrameHeader hdr;
    if (!ReadFrameOverlapped(hdr, responsePayload, timeoutMs)) {
        Disconnect();
        return false;
    }
    responseType = (fire::ipc::MsgType)hdr.msg_type;
    return true;
}

bool NamedPipeClient::SendAsync(fire::ipc::MsgType type, const std::vector<uint8_t>& payload) {
    if (!EnsureConnected()) return false;
    uint32_t reqId = nextRequestId_++;
    // 异步写也给一个较短超时，避免管道满时阻塞按键线程。
    if (!WriteFrameOverlapped(type, reqId, payload, 20)) {
        Disconnect();
        return false;
    }
    return true;
}

}  // namespace firewin
