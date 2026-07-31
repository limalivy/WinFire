//
//  DictServer.cpp
//
#include "DictServer.h"

#include "../common/IpcShared.h"
#include "../config/ConfigStore.h"

#include "fire/types.h"

namespace firewin {

namespace {

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// 管道以 FILE_FLAG_OVERLAPPED 创建，读写必须走 overlapped + GetOverlappedResult。
// 连接建立后 server 侧无需超时（阻塞等到客户端发帧或断开）。

// 写出一整帧（overlapped，阻塞至完成）。成功返回 true。
bool OverlappedWriteFrame(HANDLE pipe, fire::ipc::MsgType type, uint32_t requestId,
                          const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame = fire::ipc::build_frame(type, requestId, payload);
    size_t off = 0;
    while (off < frame.size()) {
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return false;
        DWORD written = 0;
        BOOL ok = WriteFile(pipe, frame.data() + off, (DWORD)(frame.size() - off), &written, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            ok = GetOverlappedResult(pipe, &ov, &written, TRUE);
        }
        CloseHandle(ov.hEvent);
        if (!ok || written == 0) return false;
        off += written;
    }
    return true;
}

// 读取一整条消息帧（overlapped，阻塞至完成）。成功填充 hdr+payload 返回 true；断开/出错返回 false。
bool OverlappedReadFrame(HANDLE pipe, fire::ipc::FrameHeader& hdr,
                         std::vector<uint8_t>& payload) {
    std::vector<uint8_t> buf(4096);
    size_t total = 0;
    for (;;) {
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return false;
        DWORD read = 0;
        BOOL ok = ReadFile(pipe, buf.data() + total, (DWORD)(buf.size() - total), &read, &ov);
        DWORD err = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok && err == ERROR_IO_PENDING) {
            ok = GetOverlappedResult(pipe, &ov, &read, TRUE);
            err = ok ? ERROR_SUCCESS : GetLastError();
        }
        CloseHandle(ov.hEvent);
        total += read;
        if (ok) break;
        if (err == ERROR_MORE_DATA) {
            buf.resize(buf.size() * 2);
            continue;
        }
        return false;  // ERROR_BROKEN_PIPE 等
    }
    if (total < fire::ipc::kHeaderSize) return false;
    if (!fire::ipc::decode_header(buf.data(), total, hdr)) return false;
    if (fire::ipc::kHeaderSize + (size_t)hdr.payload_len > total) return false;
    payload.assign(buf.begin() + fire::ipc::kHeaderSize,
                   buf.begin() + fire::ipc::kHeaderSize + hdr.payload_len);
    return true;
}

}  // namespace

DictServer::DictServer() = default;
DictServer::~DictServer() = default;

bool DictServer::Init() {
    // 正常 IL 进程：复用 fire_config 的 ConfigStore 解析 config.json 与数据目录。
    // 注意：本函数在后台线程异步执行（main.cpp），与 ServeConnection 并发，
    // 故 dict_/stats_/config_ 的访问全程持锁（HandleRequest 读取也在同一锁内）。
    // Init 期间 HandleRequest 会阻塞，但客户端有 20ms 超时会返回空结果，可接受
    //（Init 期间本就无候选）。
    std::lock_guard<std::mutex> lock(mu_);
    firecfg::ConfigStore::Load(config_);

    std::wstring dir = firecfg::GetConfigDir();
    if (config_.db_path.empty()) {
        config_.db_path = WideToUtf8(dir + L"\\wb_py_dict.sqlite");
    }
    if (config_.stats_db_path.empty()) {
        config_.stats_db_path = WideToUtf8(dir + L"\\statistics.sqlite");
    }
    if (config_.custom_punctuation_settings.empty()) {
        config_.custom_punctuation_settings = fire::default_punctuation();
    }

    dict_ = std::make_unique<fire::DictManager>(config_);
    // 统计库始终打开（后台负责所有宿主的写入；具体是否写由请求内的开关决定）。
    stats_ = std::make_unique<fire::Statistics>(config_.stats_db_path);
    return dict_ && dict_->is_open();
}

fire::ipc::MsgType DictServer::HandleRequest(fire::ipc::MsgType type,
                                             const std::vector<uint8_t>& payload,
                                             std::vector<uint8_t>& responsePayload,
                                             bool& needResponse) {
    using namespace fire::ipc;
    needResponse = false;
    Reader r(payload);
    std::lock_guard<std::mutex> lock(mu_);

    switch (type) {
        case MsgType::Hello: {
            HelloRequest req = decode_hello_request(r);
            (void)req;
            HelloResponse resp;
            resp.server_version = kProtocolVersion;
            resp.ready = dict_ && dict_->is_open();
            resp.temp_en_trigger = config_.temp_en_trigger;
            responsePayload = encode_hello_response(resp);
            needResponse = true;
            return MsgType::Hello;
        }
        case MsgType::QueryCandidates: {
            QueryRequest req = decode_query_request(r);
            fire::QueryResult result;
            if (dict_) result = dict_->get_candidates(req.query, req.page);
            responsePayload = encode_query_result(result);
            needResponse = true;
            return MsgType::QueryCandidates;
        }
        case MsgType::ReverseLookup: {
            QueryRequest req = decode_query_request(r);
            fire::QueryResult result;
            if (dict_) result = dict_->get_reverse_lookup_candidates(req.query, req.page);
            responsePayload = encode_query_result(result);
            needResponse = true;
            return MsgType::ReverseLookup;
        }
        case MsgType::RememberFreq: {
            FreqRequest req = decode_freq_request(r);
            if (dict_ && r.ok()) dict_->remember_dynamic_frequency(req.query, req.candidate);
            return MsgType::RememberFreq;  // 异步，无响应
        }
        case MsgType::SetCandidateFirst: {
            FreqRequest req = decode_freq_request(r);
            if (dict_ && r.ok()) dict_->set_candidate_to_first(req.query, req.candidate);
            return MsgType::SetCandidateFirst;  // 异步，无响应
        }
        case MsgType::PrependCandidate: {
            fire::Candidate c = decode_candidate(r);
            bool ok = false;
            if (dict_ && r.ok()) ok = dict_->prepend_candidate(c);
            responsePayload = encode_bool(ok);
            needResponse = true;
            return MsgType::PrependCandidate;
        }
        case MsgType::GetUserCandidates: {
            std::vector<fire::Candidate> list;
            if (dict_) list = dict_->get_user_candidates();
            responsePayload = encode_candidate_list(list);
            needResponse = true;
            return MsgType::GetUserCandidates;
        }
        case MsgType::RecordStat: {
            RecordStatRequest req = decode_record_stat(r);
            // 关键：客户端（如 SearchHost.exe 等 AppContainer 沙箱进程）可能读不到用户
            // config.json，导致传来的 enable_stats/enable_hanzi 为 false。后台是正常 IL
            // 进程，能读真实 config.json，故用后台 config 覆盖客户端值，保证统计开关一致。
            bool enableStats = config_.enable_statistics;
            bool enableHanzi = config_.enable_hanzi_frequency_statistics;
            if (stats_ && stats_->is_open() && r.ok()) {
                stats_->record_candidate(req.candidate, req.app_id, req.hanzi_parts,
                                         enableStats, enableHanzi);
            }
            return MsgType::RecordStat;  // 异步，无响应
        }
        case MsgType::Reinit: {
            if (dict_) dict_->reinit();
            return MsgType::Reinit;  // 异步，无响应
        }
        default: {
            ErrorMessage err;
            err.code = -1;
            err.message = "unknown msg_type";
            responsePayload = encode_error(err);
            needResponse = true;
            return MsgType::Error;
        }
    }
}

void DictServer::ServeConnection(HANDLE pipe) {
    using namespace fire::ipc;
    for (;;) {
        FrameHeader hdr;
        std::vector<uint8_t> payload;
        if (!OverlappedReadFrame(pipe, hdr, payload)) break;  // 对端断开

        std::vector<uint8_t> responsePayload;
        bool needResponse = false;
        MsgType respType = HandleRequest((MsgType)hdr.msg_type, payload,
                                         responsePayload, needResponse);
        if (needResponse) {
            if (!OverlappedWriteFrame(pipe, respType, hdr.request_id, responsePayload)) break;
        }
    }
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
}

}  // namespace firewin
