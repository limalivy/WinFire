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

#include <list>
#include <string>
#include <unordered_map>
#include <utility>
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

    // 向 dictd 校验本地候选缓存是否仍有效（Activate / 配置变更 / 重连后调用）。
    // 据 dictd 返回的 token 决定清空/保留本地 LRU；据 allow_dll_cache 决定是否启用。
    // 通信失败时保守禁用缓存（清空 + 关闭），但不改动 available_（缓存有效性与
    // 后台可达性正交）。
    void ValidateCache() override;

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
    void SaveCache(const std::string& app_id) override;
    bool IsAvailable() const override { return available_; }

    // 可用性恢复：当 available_ 失效（连接超时/握手失败）后，由按键热路径周期性
    // 调用，尝试重连并握手。带 1s 退避，避免每键都 CreateFile + 握手（每次握手有
    // 20ms 同步等待）。后台就绪后下一次按键即恢复中文输入。
    bool TryRecover() override;

    // 握手拿到的 config 摘要。
    char temp_en_trigger() const { return temp_en_trigger_; }

private:
    NamedPipeClient client_;
    std::string app_id_;
    bool available_ = false;
    char temp_en_trigger_ = ';';

    // 退避：记录上次尝试恢复的时刻，距上次不足 kRecoverBackoffMs 则跳过。
    ULONGLONG lastRecoverTick_ = 0;
    static constexpr DWORD kRecoverBackoffMs = 1000;

    static constexpr DWORD kSyncTimeoutMs = 20;  // 设计 §5.5：同步 20ms 超时

    // ---- DLL 本地候选缓存（仅 allow_dll_cache=true 时启用）----
    // dictd 在 CacheValidate 响应里裁决是否允许（开启动态调频时禁止，因候选顺序会变）。
    // token 由 dictd 综合计算（db mtime/size + ConfigDigest + user_cache_generation），
    // 变化即说明码表/配置/用户词变了，整个缓存失效。
    struct DllCacheKey {
        std::string query;
        int page = 1;
        bool operator==(const DllCacheKey& o) const {
            return page == o.page && query == o.query;
        }
    };
    struct DllCacheKeyHash {
        size_t operator()(const DllCacheKey& k) const noexcept {
            size_t h = std::hash<int>{}(k.page);
            h ^= std::hash<std::string>{}(k.query) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    using DllCacheList = std::list<std::pair<DllCacheKey, fire::QueryResult>>;
    DllCacheList dll_cache_lru_;  // front=MRU，back=LRU
    std::unordered_map<DllCacheKey, DllCacheList::iterator, DllCacheKeyHash> dll_cache_map_;
    static constexpr size_t kDllCacheLimit = 1000;
    bool dll_cache_enabled_ = false;  // dictd 裁决：是否允许缓存
    uint64_t cache_token_ = 0;        // 上次 ValidateCache 拿到的 token（0=未校验/无效）

    bool dll_cache_get(const DllCacheKey& key, fire::QueryResult& out);
    void dll_cache_put(const DllCacheKey& key, const fire::QueryResult& result);
    void dll_cache_clear();
};

}  // namespace firewin
