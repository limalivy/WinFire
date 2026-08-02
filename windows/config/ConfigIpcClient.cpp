//
//  ConfigIpcClient.cpp — fire_config.exe 侧的 dictd IPC 客户端实现
//
#include "ConfigIpcClient.h"

#include <windows.h>

#include "../common/IpcShared.h"

namespace firecfg {

namespace {

// 同步超时：config.exe 不是热路径，给 dictd 充裕时间（比 DLL 的 20ms 宽松）。
constexpr DWORD kConfigSyncTimeoutMs = 2000;

// 连接命名管道；连不上则拉起同目录 fire_dictd.exe（用 EXE 自身路径定位，不依赖
// DLL 的 GetModuleAnchor）并重试一次。
HANDLE ConnectWithLaunch() {
    std::wstring name = firewin::MakeDictPipeName();
    HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
        return h;
    }
    // 拉起 fire_dictd.exe（同目录）。
    wchar_t exe[MAX_PATH] = {0};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return INVALID_HANDLE_VALUE;
    std::wstring dir(exe);
    size_t p = dir.find_last_of(L"\\/");
    if (p == std::wstring::npos) return INVALID_HANDLE_VALUE;
    std::wstring dictd = dir.substr(0, p + 1) + firewin::kDictdExeName;
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + dictd + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    if (!CreateProcessW(dictd.c_str(), buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return INVALID_HANDLE_VALUE;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    // dictd 冷启可能需数百 ms（开 sqlite），轮询连接最多 ~3s。
    for (int i = 0; i < 30; ++i) {
        Sleep(100);
        h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                        OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
            return h;
        }
    }
    return INVALID_HANDLE_VALUE;
}

// 同步请求/响应一轮。成功返回 true 并填充 respType + respPayload。
bool SendRequest(fire::ipc::MsgType type, const std::vector<uint8_t>& reqPayload,
                 fire::ipc::MsgType& respType, std::vector<uint8_t>& respPayload) {
    HANDLE h = ConnectWithLaunch();
    if (h == INVALID_HANDLE_VALUE) return false;

    std::vector<uint8_t> frame = fire::ipc::build_frame(type, 1, reqPayload);
    DWORD written = 0;
    // 写请求帧（阻塞，pipe 默认字节模式已切消息模式）。
    if (!WriteFile(h, frame.data(), (DWORD)frame.size(), &written, nullptr) ||
        written != frame.size()) {
        CloseHandle(h);
        return false;
    }

    // 读响应帧：先读 16 字节头，再按 payload_len 读 payload（消息模式 pipe 一次读全帧）。
    uint8_t hdrBuf[fire::ipc::kHeaderSize];
    DWORD got = 0;
    if (!ReadFile(h, hdrBuf, fire::ipc::kHeaderSize, &got, nullptr) ||
        got != fire::ipc::kHeaderSize) {
        CloseHandle(h);
        return false;
    }
    fire::ipc::FrameHeader hdr;
    if (!fire::ipc::decode_header(hdrBuf, got, hdr)) {
        CloseHandle(h);
        return false;
    }
    respPayload.resize(hdr.payload_len);
    if (hdr.payload_len > 0) {
        DWORD gotPayload = 0;
        if (!ReadFile(h, respPayload.data(), hdr.payload_len, &gotPayload, nullptr) ||
            gotPayload != hdr.payload_len) {
            CloseHandle(h);
            return false;
        }
    }
    CloseHandle(h);
    respType = static_cast<fire::ipc::MsgType>(hdr.msg_type);
    return true;
}

}  // namespace

bool IpcGetConfig(fire::ipc::GetConfigResponse& resp, uint64_t client_config_token) {
    fire::ipc::GetConfigRequest req;
    req.client_config_token = client_config_token;
    fire::ipc::MsgType respType;
    std::vector<uint8_t> respPayload;
    if (!SendRequest(fire::ipc::MsgType::GetConfig,
                     fire::ipc::encode_get_config_request(req), respType, respPayload)) {
        return false;
    }
    if (respType != fire::ipc::MsgType::GetConfig) return false;
    fire::ipc::Reader r(respPayload);
    resp = fire::ipc::decode_get_config_response(r);
    return r.ok();
}

bool IpcSetConfig(const std::string& config_json, bool reload_user_dict,
                  bool reinit_dict, fire::ipc::SetConfigResponse& resp) {
    fire::ipc::SetConfigRequest req;
    req.config_json = config_json;
    req.reload_user_dict = reload_user_dict;
    req.reinit_dict = reinit_dict;
    fire::ipc::MsgType respType;
    std::vector<uint8_t> respPayload;
    if (!SendRequest(fire::ipc::MsgType::SetConfig,
                     fire::ipc::encode_set_config_request(req), respType, respPayload)) {
        return false;
    }
    if (respType != fire::ipc::MsgType::SetConfig) return false;
    fire::ipc::Reader r(respPayload);
    resp = fire::ipc::decode_set_config_response(r);
    return r.ok();
}

}  // namespace firecfg
