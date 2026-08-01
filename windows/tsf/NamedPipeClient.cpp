//
//  NamedPipeClient.cpp
//
#include "NamedPipeClient.h"

#include "DebugLog.h"
#include "../common/IpcShared.h"

namespace {

// 取本 DLL 模块句柄用：用本编译单元静态函数地址定位所在模块。
void GetModuleAnchor() {}

}  // namespace

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
    if (h == INVALID_HANDLE_VALUE) {
        FIRE_LOG(L"[WinFire] TryConnect: FAILED err=%lu (daemon not listening yet?)\n",
                 GetLastError());
        return false;
    }

    // 客户端也用消息读模式，帧边界与 server 一致。
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
    pipe_ = h;
    FIRE_LOG(L"[WinFire] TryConnect: OK pipe=%p\n", (void*)h);
    return true;
}

bool NamedPipeClient::LaunchBackend() {
    // 频繁拉起保护：距上次拉起不足 3s 则跳过（后台可能仍在启动）。
    ULONGLONG now = GetTickCount64();
    if (lastLaunchTick_ != 0 && now - lastLaunchTick_ < 3000) {
        FIRE_LOG(L"[WinFire] LaunchBackend: throttled (last launch %lums ago)\n",
                 (unsigned long)(now - lastLaunchTick_));
        return false;
    }
    lastLaunchTick_ = now;

    // 定位与本 DLL 同目录下的 fire_dictd.exe。
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetModuleAnchor), &self)) {
        FIRE_LOG(L"[WinFire] LaunchBackend: GetModuleHandleExW FAILED\n");
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
    FIRE_LOG(L"[WinFire] LaunchBackend: CreateProcessW('%s') -> %d (err=%lu)\n",
             exe.c_str(), ok ? 1 : 0, ok ? 0 : GetLastError());
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool NamedPipeClient::EnsureConnected() {
    if (IsConnected()) return true;
    if (TryConnect()) return true;

    // 连不上：拉起后台进程，但**不在此等待**（这里运行在宿主 UI 线程上，
    // 任何 Sleep/重试都会卡住宿主，表现为开机后首次按键卡死 Chrome 等）。
    // 立即返回 false 让上层降级透传这一次按键；后台会在数百 ms 内就绪，
    // 下一次按键时 TryConnect 自然成功。
    FIRE_LOG(L"[WinFire] EnsureConnected: not connected, launching backend (no wait)\n");
    LaunchBackend();
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
    ULONGLONG t0 = GetTickCount64();
    if (!EnsureConnected()) {
        FIRE_LOG(L"[WinFire] SendRequest: type=%u FAIL at connect (%lums, daemon down?)\n",
                 (unsigned)type, (unsigned long)(GetTickCount64() - t0));
        return false;
    }

    uint32_t reqId = nextRequestId_++;
    if (!WriteFrameOverlapped(type, reqId, requestPayload, timeoutMs)) {
        FIRE_LOG(L"[WinFire] SendRequest: type=%u FAIL at write (%lums, timeout=%lums)\n",
                 (unsigned)type, (unsigned long)(GetTickCount64() - t0), (unsigned long)timeoutMs);
        Disconnect();  // 标记需重连
        return false;
    }
    fire::ipc::FrameHeader hdr;
    if (!ReadFrameOverlapped(hdr, responsePayload, timeoutMs)) {
        // 读超时是冷启动掉输入的关键信号：连上了、写进去了，但后台还没回（Init 阻塞）。
        FIRE_LOG(L"[WinFire] SendRequest: type=%u FAIL at read/TIMEOUT (%lums, budget=%lums) — daemon slow?\n",
                 (unsigned)type, (unsigned long)(GetTickCount64() - t0), (unsigned long)timeoutMs);
        Disconnect();
        return false;
    }
    responseType = (fire::ipc::MsgType)hdr.msg_type;
    FIRE_LOG(L"[WinFire] SendRequest: type=%u OK resp=%u took=%lums\n",
             (unsigned)type, (unsigned)responseType,
             (unsigned long)(GetTickCount64() - t0));
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
