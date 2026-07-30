//
//  protocol.h — IPC 帧协议（帧头 + 消息类型 + 请求/响应编解码）
//
//  帧格式（固定头 16 字节，小端）：
//    magic      u32  0x57464452 ("WFDR")
//    version    u16  协议版本（= kProtocolVersion）
//    msg_type   u16  见 MsgType
//    request_id u32  请求序号，响应回填
//    payload_len u32 后续 payload 字节数
//  [payload_len 字节 payload]
//
//  详见 docs/dict-ipc-design.md §5.3 / §5.4。
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fire/candidate.h"
#include "fire/dict_manager.h"  // QueryResult
#include "fire/ipc/wire.h"

namespace fire {
namespace ipc {

constexpr uint32_t kMagic = 0x57464452;   // "WFDR"（小端存储）
constexpr uint16_t kProtocolVersion = 1;
constexpr size_t   kHeaderSize = 16;

enum class MsgType : uint16_t {
    Hello              = 0x01,
    QueryCandidates    = 0x02,
    ReverseLookup      = 0x03,
    RememberFreq       = 0x04,
    SetCandidateFirst  = 0x05,
    PrependCandidate   = 0x06,
    GetUserCandidates  = 0x07,
    RecordStat         = 0x08,
    Reinit             = 0x09,
    Error              = 0xFF,
};

struct FrameHeader {
    uint32_t magic = kMagic;
    uint16_t version = kProtocolVersion;
    uint16_t msg_type = 0;
    uint32_t request_id = 0;
    uint32_t payload_len = 0;
};

// 头编解码。decode 校验 magic，失败返回 false。
void encode_header(const FrameHeader& h, uint8_t out[kHeaderSize]);
bool decode_header(const uint8_t* data, size_t len, FrameHeader& out);

// 组装完整帧（头 + payload）。
std::vector<uint8_t> build_frame(MsgType type, uint32_t request_id,
                                 const std::vector<uint8_t>& payload);

// ---- 各消息 payload 编解码 ----
// 每个 encode_* 返回 payload 字节；decode_* 从 Reader 读取，越界时 Reader.ok()=false。

// Hello 请求：client 协议版本 + app_id + config 摘要（temp_en_trigger）
struct HelloRequest {
    uint16_t client_version = kProtocolVersion;
    std::string app_id;
};
std::vector<uint8_t> encode_hello_request(const HelloRequest& req);
HelloRequest decode_hello_request(Reader& r);

// Hello 响应：server 版本 + 是否就绪 + temp_en_trigger（config 摘要）
struct HelloResponse {
    uint16_t server_version = kProtocolVersion;
    bool ready = false;
    char temp_en_trigger = ';';
};
std::vector<uint8_t> encode_hello_response(const HelloResponse& resp);
HelloResponse decode_hello_response(Reader& r);

// QueryCandidates / ReverseLookup 请求：str query, i32 page
struct QueryRequest {
    std::string query;
    int32_t page = 1;
};
std::vector<uint8_t> encode_query_request(const QueryRequest& req);
QueryRequest decode_query_request(Reader& r);

// QueryResult 响应（QueryCandidates / ReverseLookup 共用）
std::vector<uint8_t> encode_query_result(const QueryResult& result);
QueryResult decode_query_result(Reader& r);

// RememberFreq / SetCandidateFirst 请求：str query, Candidate
struct FreqRequest {
    std::string query;
    Candidate candidate;
};
std::vector<uint8_t> encode_freq_request(const FreqRequest& req);
FreqRequest decode_freq_request(Reader& r);

// PrependCandidate 请求：Candidate
std::vector<uint8_t> encode_candidate(const Candidate& c);
Candidate decode_candidate(Reader& r);

// PrependCandidate 响应：u8 ok
std::vector<uint8_t> encode_bool(bool ok);
bool decode_bool(Reader& r);

// GetUserCandidates 响应：Candidate[]
std::vector<uint8_t> encode_candidate_list(const std::vector<Candidate>& list);
std::vector<Candidate> decode_candidate_list(Reader& r);

// RecordStat 请求：Candidate, str app_id, str[] hanzi_parts, u8 enable_stats, u8 enable_hanzi
struct RecordStatRequest {
    Candidate candidate;
    std::string app_id;
    std::vector<std::string> hanzi_parts;
    bool enable_stats = false;
    bool enable_hanzi = false;
};
std::vector<uint8_t> encode_record_stat(const RecordStatRequest& req);
RecordStatRequest decode_record_stat(Reader& r);

// Error 响应：i32 code, str message
struct ErrorMessage {
    int32_t code = 0;
    std::string message;
};
std::vector<uint8_t> encode_error(const ErrorMessage& err);
ErrorMessage decode_error(Reader& r);

}  // namespace ipc
}  // namespace fire
