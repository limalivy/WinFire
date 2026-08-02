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
    SaveCache          = 0x0A,  // DLL Deactivate 时触发 daemon 落盘 LRU 快照（异步）
    CacheValidate      = 0x0B,  // DLL 校验本地候选缓存 + config 是否仍有效（同步有响应）
    GetConfig          = 0x0C,  // 拉取全量 config + 数据文件路径（同步，config.exe/DLL 用）
    SetConfig          = 0x0D,  // 委托 dictd 写 config.json + 热重载（同步，config.exe 用）
    ReloadConfig       = 0x0E,  // 通知 dictd 从磁盘重读 config.json（异步，--reload-config 用）
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

// SaveCache 请求：触发源 app_id（仅用于日志，不保证保存成功，daemon 带 1 分钟节流）。
// DLL Deactivate 时 fire-and-forget 发送，不等响应。
struct SaveCacheRequest {
    std::string app_id;  // 触发来源宿主进程名
};
std::vector<uint8_t> encode_save_cache_request(const SaveCacheRequest& req);
SaveCacheRequest decode_save_cache_request(Reader& r);

// CacheValidate 请求：DLL 在 Activate / 配置变更 / 重连后校验本地候选缓存 + config 是否仍有效。
// 语义：DLL 把上次拿到的 token 记下，本次响应 token 不变即缓存有效；变化则清空本地缓存。
// 候选缓存 token 由 dictd 综合计算（db mtime/size + ConfigDigest + user_cache_generation），
// 对 DLL 不透明。allow_dll_cache=false（如开启动态调频）时 DLL 必须禁用本地缓存。
//
// config 部分（config_token / config_json / 数据文件路径）：client_config_token 是 DLL
// 已知的 config 版本；dictd 比对，不一致时 config_json 填全量（一致则留空省传输）。
// config_token 由 dictd 内部 RefreshConfigToken 算出（canonical json 的 FNV-1a64），
// 仅在 SetConfig / ReloadConfig 时刷新——不再 stat config.json，消除定时轮询。
struct CacheValidateRequest {
    uint16_t client_version = kProtocolVersion;
    std::string app_id;  // 仅日志用
    uint64_t client_config_token = 0;  // DLL 已知 config 版本；0=首次/强制全量
};
struct CacheValidateResponse {
    uint64_t token = 0;            // 候选缓存指纹；0 = dictd 未就绪，DLL 应禁用缓存
    bool allow_dll_cache = false;  // dictd 裁决：是否允许 DLL 启用本地缓存
    uint64_t config_token = 0;     // 当前 config 版本
    std::string config_json;       // 空=config 未变(client_token 一致)；非空=全量，DLL 需更新
    // 数据文件路径（dictd 拥有，供 config.exe 直接 I/O user-dict/db 等）。
    // DLL 一般不直接用（沙箱无权读），但 config.exe 用。
    std::string db_path;
    std::string stats_db_path;
    std::string user_dict_path;
    std::string cache_store_path;
};
std::vector<uint8_t> encode_cache_validate_request(const CacheValidateRequest& req);
CacheValidateRequest decode_cache_validate_request(Reader& r);
std::vector<uint8_t> encode_cache_validate_response(const CacheValidateResponse& resp);
CacheValidateResponse decode_cache_validate_response(Reader& r);

// GetConfig 请求/响应：config.exe 打开时拉全量 config + 数据文件路径（同步 20ms）。
// DLL 也可用（但通常走 CacheValidate 顺带拿）。config_json 在 client_config_token 一致时留空。
struct GetConfigRequest {
    uint64_t client_config_token = 0;  // 0=强制全量
};
struct GetConfigResponse {
    uint64_t config_token = 0;
    std::string config_json;  // 全量（可能空，若 token 一致）
    std::string db_path;
    std::string stats_db_path;
    std::string user_dict_path;
    std::string cache_store_path;
};
std::vector<uint8_t> encode_get_config_request(const GetConfigRequest& req);
GetConfigRequest decode_get_config_request(Reader& r);
std::vector<uint8_t> encode_get_config_response(const GetConfigResponse& resp);
GetConfigResponse decode_get_config_response(Reader& r);

// SetConfig 请求/响应：config.exe 保存时委托 dictd 写 config.json + 热重载（同步 100ms）。
// dictd 是 config.json 的唯一写者（原子 temp+rename）；reload_user_dict/reinit_dict 控制
// 是否连带重载用户词库 / 重建 sqlite 句柄（generation++ 使候选缓存 token 失效）。
struct SetConfigRequest {
    std::string config_json;        // 全量新 config（canonical）
    bool reload_user_dict = false;  // config.exe 改了 user-dict.txt 时
    bool reinit_dict = false;       // db 文件被替换（重建词库）时
};
struct SetConfigResponse {
    bool ok = false;
    uint64_t new_config_token = 0;  // 写盘+reload 后的新 config 版本
    uint64_t new_dict_token = 0;    // 当前候选缓存 token（供 config.exe 校验失效）
};
std::vector<uint8_t> encode_set_config_request(const SetConfigRequest& req);
SetConfigRequest decode_set_config_request(Reader& r);
std::vector<uint8_t> encode_set_config_response(const SetConfigResponse& resp);
SetConfigResponse decode_set_config_response(Reader& r);

// ReloadConfig 请求：通知 dictd 从磁盘重读 config.json（异步 fire-and-forget）。
// 供 fire_dictd.exe --reload-config 命令行 / install.ps1 在外部改了 config.json 后触发。
// dictd 收到即 ConfigStore::Load(config_) + 原子写回（规范化）+ RefreshConfigToken + reinit。
struct ReloadConfigRequest {
    std::string source;  // 触发来源（仅日志，如 "cmdline" / "install.ps1"）
};
std::vector<uint8_t> encode_reload_config_request(const ReloadConfigRequest& req);
ReloadConfigRequest decode_reload_config_request(Reader& r);

// Error 响应：i32 code, str message
struct ErrorMessage {
    int32_t code = 0;
    std::string message;
};
std::vector<uint8_t> encode_error(const ErrorMessage& err);
ErrorMessage decode_error(Reader& r);

}  // namespace ipc
}  // namespace fire
