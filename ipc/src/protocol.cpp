//
//  protocol.cpp — IPC 帧协议编解码实现
//
#include "fire/ipc/protocol.h"

namespace fire {
namespace ipc {

void encode_header(const FrameHeader& h, uint8_t out[kHeaderSize]) {
    Writer w;
    w.put_u32(h.magic);
    w.put_u16(h.version);
    w.put_u16(h.msg_type);
    w.put_u32(h.request_id);
    w.put_u32(h.payload_len);
    const auto& b = w.buffer();
    for (size_t i = 0; i < kHeaderSize; ++i) out[i] = b[i];
}

bool decode_header(const uint8_t* data, size_t len, FrameHeader& out) {
    if (len < kHeaderSize) return false;
    Reader r(data, len);
    out.magic = r.get_u32();
    out.version = r.get_u16();
    out.msg_type = r.get_u16();
    out.request_id = r.get_u32();
    out.payload_len = r.get_u32();
    if (!r.ok()) return false;
    return out.magic == kMagic;
}

std::vector<uint8_t> build_frame(MsgType type, uint32_t request_id,
                                 const std::vector<uint8_t>& payload) {
    FrameHeader h;
    h.msg_type = static_cast<uint16_t>(type);
    h.request_id = request_id;
    h.payload_len = static_cast<uint32_t>(payload.size());

    uint8_t hdr[kHeaderSize];
    encode_header(h, hdr);

    std::vector<uint8_t> frame;
    frame.reserve(kHeaderSize + payload.size());
    frame.insert(frame.end(), hdr, hdr + kHeaderSize);
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

// ---- Hello ----
std::vector<uint8_t> encode_hello_request(const HelloRequest& req) {
    Writer w;
    w.put_u16(req.client_version);
    w.put_string(req.app_id);
    return w.take();
}

HelloRequest decode_hello_request(Reader& r) {
    HelloRequest req;
    req.client_version = r.get_u16();
    req.app_id = r.get_string();
    return req;
}

std::vector<uint8_t> encode_hello_response(const HelloResponse& resp) {
    Writer w;
    w.put_u16(resp.server_version);
    w.put_u8(resp.ready ? 1 : 0);
    w.put_u8(static_cast<uint8_t>(resp.temp_en_trigger));
    return w.take();
}

HelloResponse decode_hello_response(Reader& r) {
    HelloResponse resp;
    resp.server_version = r.get_u16();
    resp.ready = r.get_u8() != 0;
    resp.temp_en_trigger = static_cast<char>(r.get_u8());
    return resp;
}

// ---- Query ----
std::vector<uint8_t> encode_query_request(const QueryRequest& req) {
    Writer w;
    w.put_string(req.query);
    w.put_i32(req.page);
    return w.take();
}

QueryRequest decode_query_request(Reader& r) {
    QueryRequest req;
    req.query = r.get_string();
    req.page = r.get_i32();
    return req;
}

std::vector<uint8_t> encode_query_result(const QueryResult& result) {
    Writer w;
    w.put_query_result(result);
    return w.take();
}

QueryResult decode_query_result(Reader& r) { return r.get_query_result(); }

// ---- Freq ----
std::vector<uint8_t> encode_freq_request(const FreqRequest& req) {
    Writer w;
    w.put_string(req.query);
    w.put_candidate(req.candidate);
    return w.take();
}

FreqRequest decode_freq_request(Reader& r) {
    FreqRequest req;
    req.query = r.get_string();
    req.candidate = r.get_candidate();
    return req;
}

// ---- Candidate / bool / list ----
std::vector<uint8_t> encode_candidate(const Candidate& c) {
    Writer w;
    w.put_candidate(c);
    return w.take();
}

Candidate decode_candidate(Reader& r) { return r.get_candidate(); }

std::vector<uint8_t> encode_bool(bool ok) {
    Writer w;
    w.put_u8(ok ? 1 : 0);
    return w.take();
}

bool decode_bool(Reader& r) { return r.get_u8() != 0; }

std::vector<uint8_t> encode_candidate_list(const std::vector<Candidate>& list) {
    Writer w;
    w.put_candidate_list(list);
    return w.take();
}

std::vector<Candidate> decode_candidate_list(Reader& r) { return r.get_candidate_list(); }

// ---- RecordStat ----
std::vector<uint8_t> encode_record_stat(const RecordStatRequest& req) {
    Writer w;
    w.put_candidate(req.candidate);
    w.put_string(req.app_id);
    w.put_string_list(req.hanzi_parts);
    w.put_u8(req.enable_stats ? 1 : 0);
    w.put_u8(req.enable_hanzi ? 1 : 0);
    return w.take();
}

RecordStatRequest decode_record_stat(Reader& r) {
    RecordStatRequest req;
    req.candidate = r.get_candidate();
    req.app_id = r.get_string();
    req.hanzi_parts = r.get_string_list();
    req.enable_stats = r.get_u8() != 0;
    req.enable_hanzi = r.get_u8() != 0;
    return req;
}

// ---- Error ----
std::vector<uint8_t> encode_error(const ErrorMessage& err) {
    Writer w;
    w.put_i32(err.code);
    w.put_string(err.message);
    return w.take();
}

ErrorMessage decode_error(Reader& r) {
    ErrorMessage err;
    err.code = r.get_i32();
    err.message = r.get_string();
    return err;
}

}  // namespace ipc
}  // namespace fire
