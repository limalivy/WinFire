//
//  DictIpcProxy.h — IDictService 的 IPC 代理实现（转发到 fire_dictd.exe）
//
//  把 InputEngine 对查字/统计的调用编码为 IPC 请求，经命名管道发给后台进程。
//  - 同步方法（查询/PrependCandidate/GetUserCandidates）：等响应；失败/超时返回空
//    并置 available_=false（引擎降级透传）。
//  - 异步方法（调频/统计/Reinit）：fire-and-forget，只发帧。
//  - 构造时做一次 Hello 握手，取回后台就绪标志与 config 摘要（temp_en_trigger）。
//  详见 docs/dict-ipc-design.md §4.3 / §5。
//
#pragma once

#include <string>
#include <vector>

#include "fire/dict_service.h"
#include "NamedPipeClient.h"

namespace firewin {

class DictIpcProxy : public fire::IDictService {
public:
    // app_id：宿主进程名，握手时下发给后台。
    explicit DictIpcProxy(const std::string& app_id);
    ~DictIpcProxy() override = default;

    DictIpcProxy(const DictIpcProxy&) = delete;
    DictIpcProxy& operator=(const DictIpcProxy&) = delete;

    // 首次握手：连接后台并发 Hello。返回后台是否就绪（词库已打开）。
    // 失败（连不上/超时）时置 available_=false。
    bool Handshake();

    fire::QueryResult GetCandidates(const std::string& query, int page) override;
    fire::QueryResult GetReverseLookup(const std::string& pinyin, int page) override;
    void RememberDynamicFrequency(const std::string& query, const fire::Candidate& candidate) override;
    void SetCandidateToFirst(const std::string& query, const fire::Candidate& candidate) override;
    bool PrependCandidate(const fire::Candidate& candidate) override;
    std::vector<fire::Candidate> GetUserCandidates() override;
    void Reinit() override;
    void RecordCandidate(const fire::Candidate& candidate,
                         const std::string& app_id,
                         const std::vector<std::string>& hanzi_parts,
                         bool enable_stats,
                         bool enable_hanzi) override;
    bool IsAvailable() const override { return available_; }

    // 握手拿到的 config 摘要。
    char temp_en_trigger() const { return temp_en_trigger_; }

private:
    NamedPipeClient client_;
    std::string app_id_;
    bool available_ = false;
    char temp_en_trigger_ = ';';

    static constexpr DWORD kSyncTimeoutMs = 20;  // 设计 §5.5：同步 20ms 超时
};

}  // namespace firewin
