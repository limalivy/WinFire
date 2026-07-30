//
//  DictIpcProxy.cpp
//
#include "DictIpcProxy.h"

namespace firewin {

using namespace fire::ipc;

DictIpcProxy::DictIpcProxy(const std::string& app_id) : app_id_(app_id) {}

bool DictIpcProxy::Handshake() {
    HelloRequest req;
    req.client_version = kProtocolVersion;
    req.app_id = app_id_;

    MsgType respType;
    std::vector<uint8_t> respPayload;
    if (!client_.SendRequest(MsgType::Hello, encode_hello_request(req), respType, respPayload,
                             kSyncTimeoutMs)) {
        available_ = false;
        return false;  // 连不上后台
    }
    if (respType != MsgType::Hello) {
        available_ = false;
        return false;
    }
    Reader r(respPayload);
    HelloResponse resp = decode_hello_response(r);
    if (!r.ok()) {
        available_ = false;
        return false;
    }
    temp_en_trigger_ = resp.temp_en_trigger;
    available_ = resp.ready;
    // 已成功握手（连上后台并拿到响应）；available_ 反映后台是否就绪。
    return true;
}

fire::QueryResult DictIpcProxy::GetCandidates(const std::string& query, int page) {
    QueryRequest req;
    req.query = query;
    req.page = page;
    MsgType respType;
    std::vector<uint8_t> respPayload;
    if (!client_.SendRequest(MsgType::QueryCandidates, encode_query_request(req), respType,
                             respPayload, kSyncTimeoutMs)) {
        available_ = false;
        return {};
    }
    available_ = true;
    if (respType != MsgType::QueryCandidates) return {};
    Reader r(respPayload);
    return decode_query_result(r);
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
