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
    return Handshake();
}

fire::QueryResult DictIpcProxy::GetCandidates(const std::string& query, int page) {
    QueryRequest req;
    req.query = query;
    req.page = page;
    MsgType respType;
    std::vector<uint8_t> respPayload;
    if (!client_.SendRequest(MsgType::QueryCandidates, encode_query_request(req), respType,
                             respPayload, kSyncTimeoutMs)) {
        bool wasAvail = available_;
        available_ = false;
        FIRE_LOG(L"[WinFire] GetCandidates: FAIL q='%hs' available_=%d->false\n",
                 query.c_str(), wasAvail ? 1 : 0);
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

}  // namespace firewin
