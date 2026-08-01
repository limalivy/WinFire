//
//  DictIpcProxy.cpp
//
#include "DictIpcProxy.h"
#include "DebugLog.h"

namespace firewin {

using namespace fire::ipc;

DictIpcProxy::DictIpcProxy(const std::string& app_id) : app_id_(app_id) {}

bool DictIpcProxy::Handshake() {
    HelloRequest req;
    req.client_version = kProtocolVersion;
    req.app_id = app_id_;

    ULONGLONG t0 = GetTickCount64();
    MsgType respType;
    std::vector<uint8_t> respPayload;
    if (!client_.SendRequest(MsgType::Hello, encode_hello_request(req), respType, respPayload,
                             kSyncTimeoutMs)) {
        available_ = false;
        FIRE_LOG(L"[WinFire] Handshake: FAIL (send/timeout), available_=false took=%lums\n",
                 (unsigned long)(GetTickCount64() - t0));
        return false;  // 连不上后台
    }
    if (respType != MsgType::Hello) {
        available_ = false;
        FIRE_LOG(L"[WinFire] Handshake: FAIL (resp type=%u != Hello)\n", (unsigned)respType);
        return false;
    }
    Reader r(respPayload);
    HelloResponse resp = decode_hello_response(r);
    if (!r.ok()) {
        available_ = false;
        FIRE_LOG(L"[WinFire] Handshake: FAIL (decode error)\n");
        return false;
    }
    temp_en_trigger_ = resp.temp_en_trigger;
    bool wasAvail = available_;
    available_ = resp.ready;
    FIRE_LOG(L"[WinFire] Handshake: OK server_ready=%d available_=%d->%d took=%lums\n",
             resp.ready ? 1 : 0, wasAvail ? 1 : 0, available_ ? 1 : 0,
             (unsigned long)(GetTickCount64() - t0));
    // 已成功握手（连上后台并拿到响应）；available_ 反映后台是否就绪。
    return true;
}

bool DictIpcProxy::TryRecover() {
    if (available_) return true;
    // 退避：避免每键都尝试重连（每次握手需 20ms 同步等待）。后台启动通常在数百 ms 内。
    ULONGLONG now = GetTickCount64();
    if (lastRecoverTick_ != 0 && now - lastRecoverTick_ < kRecoverBackoffMs) {
        FIRE_LOG(L"[WinFire] TryRecover: throttled (last try %lums ago, backoff=%lums)\n",
                 (unsigned long)(now - lastRecoverTick_), (unsigned long)kRecoverBackoffMs);
        return false;  // 距上次尝试太近，跳过
    }
    lastRecoverTick_ = now;
    FIRE_LOG(L"[WinFire] TryRecover: attempting Handshake (available was false)\n");
    // EnsureConnected 已在 Handshake 内调用，此处直接重新握手（会重连 + 拉后台）。
    if (!Handshake()) return false;
    // 重连成功后必须重新校验缓存：token 可能因后台重启而变化（dictd 重启 = 新 generation）。
    ValidateCache();
    return true;
}

void DictIpcProxy::ValidateCache() {
    CacheValidateRequest req;
    req.client_version = kProtocolVersion;
    req.app_id = app_id_;

    MsgType respType;
    std::vector<uint8_t> respPayload;
    if (!client_.SendRequest(MsgType::CacheValidate, encode_cache_validate_request(req),
                             respType, respPayload, kSyncTimeoutMs)) {
        // 通信失败：保守禁用缓存（清空 + 关闭），不动 available_（缓存有效性与
        // 后台可达性正交；available_ 由 GetCandidates 等热路径单独维护）。
        bool wasEnabled = dll_cache_enabled_;
        dll_cache_clear();
        dll_cache_enabled_ = false;
        if (wasEnabled) {
            FIRE_LOG(L"[WinFire] ValidateCache: FAIL (send/timeout), dll_cache disabled\n");
        }
        return;
    }
    if (respType != MsgType::CacheValidate) {
        dll_cache_clear();
        dll_cache_enabled_ = false;
        FIRE_LOG(L"[WinFire] ValidateCache: FAIL (resp type=%u != CacheValidate)\n",
                 (unsigned)respType);
        return;
    }
    Reader r(respPayload);
    CacheValidateResponse resp = decode_cache_validate_response(r);
    if (!r.ok()) {
        dll_cache_clear();
        dll_cache_enabled_ = false;
        FIRE_LOG(L"[WinFire] ValidateCache: FAIL (decode error)\n");
        return;
    }
    // token 变化 → 码表/配置/用户词变了，整个缓存失效。
    if (resp.token != cache_token_) {
        dll_cache_clear();
        FIRE_LOG(L"[WinFire] ValidateCache: token changed (old=%llu new=%llu), cache cleared\n",
                 (unsigned long long)cache_token_, (unsigned long long)resp.token);
        cache_token_ = resp.token;
    }
    // allow_dll_cache=false（如开启动态调频）→ 禁用并清空。
    bool wasEnabled = dll_cache_enabled_;
    dll_cache_enabled_ = resp.allow_dll_cache;
    if (!dll_cache_enabled_ && wasEnabled) {
        dll_cache_clear();
    }
    FIRE_LOG(L"[WinFire] ValidateCache: OK token=%llu allow_dll_cache=%d enabled=%d\n",
             (unsigned long long)resp.token, resp.allow_dll_cache ? 1 : 0,
             dll_cache_enabled_ ? 1 : 0);
}

fire::QueryResult DictIpcProxy::GetCandidates(const std::string& query, int page) {
#ifdef FIRE_DEBUG
    ULONGLONG t0 = GetTickCount64();  // 耗时统计基准（仅 Debug，Release 零开销）
#endif
    // 本地 LRU 命中：直接返回，0 次 IPC 往返。仅 dll_cache_enabled_ 时启用。
    if (dll_cache_enabled_) {
        DllCacheKey key{query, page};
        fire::QueryResult cached;
        if (dll_cache_get(key, cached)) {
            FIRE_LOG(L"[WinFire] GetCandidates: HIT cache q='%hs' page=%d took=%lums results=%zu\n",
                     query.c_str(), page,
                     (unsigned long)(GetTickCount64() - t0), cached.candidates.size());
            return cached;
        }
    }

    QueryRequest req;
    req.query = query;
    req.page = page;
    MsgType respType;
    std::vector<uint8_t> respPayload;
    if (!client_.SendRequest(MsgType::QueryCandidates, encode_query_request(req), respType,
                             respPayload, kSyncTimeoutMs)) {
        bool wasAvail = available_;
        available_ = false;
        FIRE_LOG(L"[WinFire] GetCandidates: FAIL q='%hs' available_=%d->false took=%lums\n",
                 query.c_str(), wasAvail ? 1 : 0,
                 (unsigned long)(GetTickCount64() - t0));
        return {};
    }
    bool wasAvail = available_;
    available_ = true;
    Reader r(respPayload);
    fire::QueryResult result = (respType == MsgType::QueryCandidates) ? decode_query_result(r) : fire::QueryResult{};
    if (!wasAvail) {
        FIRE_LOG(L"[WinFire] GetCandidates: RECOVERED available_ false->true, q='%hs' results=%zu\n",
                 query.c_str(), result.candidates.size());
    }
    FIRE_LOG(L"[WinFire] GetCandidates: MISS cache q='%hs' page=%d took=%lums results=%zu\n",
             query.c_str(), page,
             (unsigned long)(GetTickCount64() - t0), result.candidates.size());
    // IPC 成功且缓存启用：填入本地 LRU 供下次命中。
    if (dll_cache_enabled_ && respType == MsgType::QueryCandidates) {
        dll_cache_put(DllCacheKey{query, page}, result);
    }
    return result;
}

fire::QueryResult DictIpcProxy::GetReverseLookup(const std::string& pinyin, int page) {
    QueryRequest req;
    req.query = pinyin;
    req.page = page;
    MsgType respType;
    std::vector<uint8_t> respPayload;
    if (!client_.SendRequest(MsgType::ReverseLookup, encode_query_request(req), respType,
                             respPayload, kSyncTimeoutMs)) {
        available_ = false;
        return {};
    }
    available_ = true;
    if (respType != MsgType::ReverseLookup) return {};
    Reader r(respPayload);
    return decode_query_result(r);
}

void DictIpcProxy::RememberDynamicFrequency(const std::string& query,
                                            const fire::Candidate& candidate) {
    FreqRequest req;
    req.query = query;
    req.candidate = candidate;
    client_.SendAsync(MsgType::RememberFreq, encode_freq_request(req));
}

void DictIpcProxy::SetCandidateToFirst(const std::string& query, const fire::Candidate& candidate) {
    FreqRequest req;
    req.query = query;
    req.candidate = candidate;
    client_.SendAsync(MsgType::SetCandidateFirst, encode_freq_request(req));
}

bool DictIpcProxy::PrependCandidate(const fire::Candidate& candidate) {
    MsgType respType;
    std::vector<uint8_t> respPayload;
    if (!client_.SendRequest(MsgType::PrependCandidate, encode_candidate(candidate), respType,
                             respPayload, kSyncTimeoutMs)) {
        available_ = false;
        return false;
    }
    available_ = true;
    if (respType != MsgType::PrependCandidate) return false;
    Reader r(respPayload);
    return decode_bool(r);
}

std::vector<fire::Candidate> DictIpcProxy::GetUserCandidates() {
    MsgType respType;
    std::vector<uint8_t> respPayload;
    if (!client_.SendRequest(MsgType::GetUserCandidates, {}, respType, respPayload,
                             kSyncTimeoutMs)) {
        available_ = false;
        return {};
    }
    available_ = true;
    if (respType != MsgType::GetUserCandidates) return {};
    Reader r(respPayload);
    return decode_candidate_list(r);
}

void DictIpcProxy::Reinit() {
    client_.SendAsync(MsgType::Reinit, {});
}

void DictIpcProxy::RecordCandidate(const fire::Candidate& candidate, const std::string& app_id,
                                   const std::vector<std::string>& hanzi_parts, bool enable_stats,
                                   bool enable_hanzi) {
    RecordStatRequest req;
    req.candidate = candidate;
    req.app_id = app_id;
    req.hanzi_parts = hanzi_parts;
    req.enable_stats = enable_stats;
    req.enable_hanzi = enable_hanzi;
    client_.SendAsync(MsgType::RecordStat, encode_record_stat(req));
}

void DictIpcProxy::SaveCache(const std::string& app_id) {
    // Deactivate 时触发：fire-and-forget，不等回复。daemon 收到后带 1 分钟节流 +
    // 子线程落盘。即使这次没真存（daemon 不可用/被强杀），DLL 不在乎——不保证成功。
    SaveCacheRequest req;
    req.app_id = app_id;
    client_.SendAsync(MsgType::SaveCache, encode_save_cache_request(req));
}

// ---- DLL 本地候选 LRU（O(1) 提升/淘汰，与 DictManager 内存 LRU 同模式）----
bool DictIpcProxy::dll_cache_get(const DllCacheKey& key, fire::QueryResult& out) {
    auto it = dll_cache_map_.find(key);
    if (it == dll_cache_map_.end()) return false;
    out = it->second->second;
    // 提升到 front（splice O(1)）。
    dll_cache_lru_.splice(dll_cache_lru_.begin(), dll_cache_lru_, it->second);
    return true;
}

void DictIpcProxy::dll_cache_put(const DllCacheKey& key, const fire::QueryResult& result) {
    auto it = dll_cache_map_.find(key);
    if (it != dll_cache_map_.end()) {
        it->second->second = result;
        dll_cache_lru_.splice(dll_cache_lru_.begin(), dll_cache_lru_, it->second);
        return;
    }
    if (dll_cache_map_.size() >= kDllCacheLimit && !dll_cache_lru_.empty()) {
        auto victim = std::prev(dll_cache_lru_.end());
        dll_cache_map_.erase(victim->first);
        dll_cache_lru_.pop_back();
    }
    dll_cache_lru_.push_front({key, result});
    dll_cache_map_[key] = dll_cache_lru_.begin();
}

void DictIpcProxy::dll_cache_clear() {
    dll_cache_map_.clear();
    dll_cache_lru_.clear();
}

}  // namespace firewin
