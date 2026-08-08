//
//  IpcShared.h — DLL 侧与后台进程共用的 IPC 常量与帧读写（仅 Win32，header-only）
//
//  内容：
//    - 命名管道 / 单实例 mutex 名（按登录会话隔离）
//    - §5.2 跨完整性级别 SDDL
//    - 帧读写辅助（WritePipeFrame / ReadPipeFrameBlocking，PIPE_TYPE_MESSAGE 语义）
//
//  被 windows/dictd（server）与 windows/tsf（client）共同 include。
//
#pragma once

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "fire/ipc/protocol.h"

namespace firewin {

// 跨完整性级别 ACL（设计文档 §5.2）：
//   D:(A;;GRGW;;;WD)  Everyone 读写
//   (A;;GRGW;;;AC)    ALL APPLICATION PACKAGES（AppContainer，SID S-1-15-2-1）
//   S:(ML;;NW;;;LW)   低完整性标签 No-Write-Up 关闭，允许 Low IL 客户端连接
inline const wchar_t* kDictPipeSddl =
    L"D:(A;;GRGW;;;WD)(A;;GRGW;;;AC)S:(ML;;NW;;;LW)";

// 当前进程所属登录会话 id（server 与被加载到各宿主进程的 DLL 同会话，取值一致）。
inline DWORD CurrentSessionId() {
    DWORD sid = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sid)) sid = 0;
    return sid;
}

// 命名管道名：\\.\pipe\WinFire_Dict_<会话>
inline std::wstring MakeDictPipeName() {
    return L"\\\\.\\pipe\\WinFire_Dict_" + std::to_wstring(CurrentSessionId());
}

// 单实例 mutex 名：Global\WinFire_Dictd_<会话>
inline std::wstring MakeDictdMutexName() {
    return L"Global\\WinFire_Dictd_" + std::to_wstring(CurrentSessionId());
}

// 后台进程 EXE 文件名（与 DLL / installer 同目录部署）。
inline const wchar_t* kDictdExeName = L"fire_dictd.exe";

// 写出一整帧（头 + payload），可能分多次 WriteFile 写完。成功返回 true。
inline bool WritePipeFrame(HANDLE pipe, fire::ipc::MsgType type, uint32_t requestId,
                           const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame = fire::ipc::build_frame(type, requestId, payload);
    size_t off = 0;
    while (off < frame.size()) {
        DWORD written = 0;
        if (!WriteFile(pipe, frame.data() + off, (DWORD)(frame.size() - off), &written, nullptr))
            return false;
        if (written == 0) return false;
        off += written;
    }
    return true;
}

// 阻塞读取一整帧（消息模式）。成功填充 hdr + payload 并返回 true；对端断开/出错返回 false。
// 消息可能超过初始缓冲，遇 ERROR_MORE_DATA 时扩容续读同一条消息。
inline bool ReadPipeFrameBlocking(HANDLE pipe, fire::ipc::FrameHeader& hdr,
                                  std::vector<uint8_t>& payload) {
    std::vector<uint8_t> buf(4096);
    size_t total = 0;
    for (;;) {
        DWORD read = 0;
        BOOL ok = ReadFile(pipe, buf.data() + total, (DWORD)(buf.size() - total), &read, nullptr);
        total += read;
        if (ok) break;
        DWORD err = GetLastError();
        if (err == ERROR_MORE_DATA) {
            // 同一条消息尚未读完：扩容后继续读剩余部分。
            // 设上限防止恶意/畸形帧触发无限倍增导致 OOM（纵深防御，配合 decode_header
            // 对 payload_len 的上限校验双重约束）。
            if (buf.size() >= fire::ipc::kMaxFrameLen) return false;
            buf.resize((std::min)(buf.size() * 2, fire::ipc::kMaxFrameLen));
            continue;
        }
        return false;  // ERROR_BROKEN_PIPE 等
    }
    if (total < fire::ipc::kHeaderSize) return false;
    if (!fire::ipc::decode_header(buf.data(), total, hdr)) return false;
    if (fire::ipc::kHeaderSize + (size_t)hdr.payload_len > total) return false;
    payload.assign(buf.begin() + fire::ipc::kHeaderSize,
                   buf.begin() + fire::ipc::kHeaderSize + hdr.payload_len);
    return true;
}

}  // namespace firewin
