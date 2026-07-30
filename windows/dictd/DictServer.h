//
//  DictServer.h — 请求分发：解码 IPC 帧 → 调 DictManager/Statistics → 编码响应
//
//  持有真正碰 SQLite 的 DictManager + Statistics（正常 IL 进程）。
//  对底层的访问以互斥锁串行化（查询微秒级，串行不影响体感）。
//
#pragma once

#include <windows.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "fire/config.h"
#include "fire/dict_manager.h"
#include "fire/statistics.h"
#include "fire/ipc/protocol.h"

namespace firewin {

class DictServer {
public:
    DictServer();
    ~DictServer();

    DictServer(const DictServer&) = delete;
    DictServer& operator=(const DictServer&) = delete;

    // 用正常 IL 解析数据目录并打开词库/统计库。返回词库是否成功打开。
    bool Init();

    // 处理一条已连接客户端管道：读帧 → 分发 → 回帧，直到对端断开。
    // 每个连接一个线程调用本函数；返回后调用方 CloseHandle(pipe)。
    void ServeConnection(HANDLE pipe);

private:
    // 处理单条请求；needResponse 输出该消息是否需要回响应帧，
    // responsePayload 为响应 payload（仅在 needResponse 时有意义）。
    // 返回响应的 msg_type。
    fire::ipc::MsgType HandleRequest(fire::ipc::MsgType type,
                                     const std::vector<uint8_t>& payload,
                                     std::vector<uint8_t>& responsePayload,
                                     bool& needResponse);

    fire::Config config_;
    std::unique_ptr<fire::DictManager> dict_;
    std::unique_ptr<fire::Statistics> stats_;
    std::mutex mu_;  // 串行化对 dict_/stats_ 的访问
};

}  // namespace firewin
