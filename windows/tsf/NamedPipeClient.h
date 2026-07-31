//
//  NamedPipeClient.h — DLL 侧命名管道客户端（连接 fire_dictd.exe）
//
//  职责（详见 docs/dict-ipc-design.md §5.5 / §6.3）：
//    - 连接后台管道；连接失败时拉起 fire_dictd.exe 但不在 UI 线程等待；
//    - 异步请求：只发帧，不等响应（fire-and-forget）；
//    - 断连检测与重连（退避），供上层降级。
//
#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

#include "fire/ipc/protocol.h"

namespace firewin {

class NamedPipeClient {
public:
    NamedPipeClient() = default;
    ~NamedPipeClient();

    NamedPipeClient(const NamedPipeClient&) = delete;
    NamedPipeClient& operator=(const NamedPipeClient&) = delete;

    // 是否已连接（管道句柄有效）。
    bool IsConnected() const { return pipe_ != INVALID_HANDLE_VALUE; }

    // 确保已连接：未连接则尝试一次连接，连不上则拉起后台进程并立即返回 false。
    // **不在 UI 线程上等待**后台就绪（会卡死宿主）；下次调用会自然重试。
    // 返回是否连接成功。
    bool EnsureConnected();

    // 同步请求：发送 (type, requestPayload)，等待同 request_id 的响应。
    // 成功时 responseType/responsePayload 被填充并返回 true；
    // 超时（timeoutMs）/断连/出错返回 false（调用方据此降级），并标记需重连。
    bool SendRequest(fire::ipc::MsgType type,
                     const std::vector<uint8_t>& requestPayload,
                     fire::ipc::MsgType& responseType,
                     std::vector<uint8_t>& responsePayload,
                     DWORD timeoutMs);

    // 异步请求：只发帧不等响应。发送失败标记需重连并返回 false。
    bool SendAsync(fire::ipc::MsgType type, const std::vector<uint8_t>& payload);

    // 主动断开（下次 EnsureConnected 会重连）。
    void Disconnect();

private:
    bool TryConnect();          // 单次 CreateFile 连接
    bool LaunchBackend();       // 定位并 CreateProcess 拉起 fire_dictd.exe
    bool WriteFrameOverlapped(fire::ipc::MsgType type, uint32_t requestId,
                              const std::vector<uint8_t>& payload, DWORD timeoutMs);
    bool ReadFrameOverlapped(fire::ipc::FrameHeader& hdr, std::vector<uint8_t>& payload,
                             DWORD timeoutMs);

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    uint32_t nextRequestId_ = 1;
    ULONGLONG lastLaunchTick_ = 0;  // 上次拉起后台的时刻，避免频繁拉起
};

}  // namespace firewin
