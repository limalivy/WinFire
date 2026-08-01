//
//  DictServer.cpp
//
#include "DictServer.h"

#include "DictLog.h"
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

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
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
    //
    // 关键：所有耗时构造（ConfigStore::Load、sqlite 打开）都在「锁外」完成，
    // 构造好之后才拿锁把 dict_/stats_ 指针 swap 进成员。这样 Init 的秒级 sqlite
    // 打开不再阻塞 HandleRequest —— 后者只会在最后 swap 的那「微秒」等一下，
    // 而不是等整个 Init。Init 完成前 HandleRequest 看到 dict_==null 即返回空，
    // 客户端 available_ 保持探测、后台一就绪下一键即恢复。
    //
    // 注意：config_ 必须直接载入「成员」（而非临时局部对象）。因为 DictManager
    // 持有 Config& 引用（dict_manager.h config_ 是引用成员），若用局部 tmpConfig
    // 构造再 move 进成员，Init 返回时局部对象析构 → DictManager 的引用悬垂 →
    // use-after-free。成员 config_ 生命周期与 DictServer（=进程）一致，引用稳定。
    ULONGLONG t0 = GetTickCount64();
    DLOG(L"Init: begin (constructing WITHOUT lock)\n");

    // ---- 第 1 段锁：载入成员 config_（DictManager 将引用此稳定成员）。
    // 短暂持锁仅为与 HandleRequest 对 config_ 的读建立 happens-before（避免
    // Init 写 config_ 与 HandleRequest 读 config_ 的数据竞争）。ConfigStore::Load
    // 实测 <1ms（小文件读），持锁可接受；真正慢的 sqlite 打开在第 2 段锁外。----
    ULONGLONG tA = GetTickCount64();
    {
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
    }
    DLOG(L"Init: config load+dir took %lu ms\n", (unsigned long)(GetTickCount64() - tA));

    // 打印 DB 路径与文件大小（关联冷缓存代价：大文件首次打开更慢）。
    auto logFileSize = [](const char* tag, const std::string& utf8Path) {
        std::wstring p = Utf8ToWide(utf8Path);
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) {
            ULONGLONG sz = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
            DLOG(L"Init: %hs path='%s' size=%llu bytes\n", tag, p.c_str(), sz);
        } else {
            DLOG(L"Init: %hs path='%s' (file not found)\n", tag, p.c_str());
        }
    };
    logFileSize("dict", config_.db_path);
    logFileSize("stats", config_.stats_db_path);

    // ---- 锁外：构造新 dict/stats（sqlite 打开是冷启动主耗时）。
    // DictManager 绑定成员 config_ 引用，生命周期与 DictServer 一致，安全。
    // 此时 config_ 已稳定（第 1 段锁已释放但写入完成），DictManager 构造读取
    // config_ 不与任何 HandleRequest 竞争（HandleRequest 看到 dict_==null 即返回）。----
    ULONGLONG tC = GetTickCount64();
    auto newDict = std::make_unique<fire::DictManager>(config_);
    DLOG(L"Init: DictManager ctor (sqlite open) took %lu ms, is_open=%d\n",
         (unsigned long)(GetTickCount64() - tC), (newDict && newDict->is_open()) ? 1 : 0);

    // 统计库始终打开（后台负责所有宿主的写入；具体是否写由请求内的开关决定）。
    ULONGLONG tD = GetTickCount64();
    auto newStats = std::make_unique<fire::Statistics>(config_.stats_db_path);
    DLOG(L"Init: Statistics ctor took %lu ms\n", (unsigned long)(GetTickCount64() - tD));

    // ---- 第 2 段锁（微秒级）：仅 swap dict_/stats_ 指针。----
    ULONGLONG tSwap = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(mu_);
        dict_ = std::move(newDict);
        stats_ = std::move(newStats);
    }
    DLOG(L"Init: swap took %lu ms\n", (unsigned long)(GetTickCount64() - tSwap));

    bool ok = dict_ && dict_->is_open();
    DLOG(L"Init: DONE total=%lu ms dict_open=%d\n",
         (unsigned long)(GetTickCount64() - t0), ok ? 1 : 0);
    return ok;
}

fire::ipc::MsgType DictServer::HandleRequest(fire::ipc::MsgType type,
                                             const std::vector<uint8_t>& payload,
                                             std::vector<uint8_t>& responsePayload,
                                             bool& needResponse) {
    using namespace fire::ipc;
    needResponse = false;
    Reader r(payload);
    // 测锁等待：正常应为 0ms（查询微秒级，串行无体感）。Init 已改为锁外构造 +
    // 锁内 swap，故此处不再会因为 Init 的秒级 sqlite 打开而长时间阻塞；
    // 仅在 Init 收尾 swap 的微秒、或 Reinit 重建 db 句柄时短暂等待。
    ULONGLONG tReq = GetTickCount64();
    std::lock_guard<std::mutex> lock(mu_);
    ULONGLONG lockWait = GetTickCount64() - tReq;
    bool dictReady = dict_ && dict_->is_open();
    // 只对热路径（Hello/Query/ReverseLookup）打详细日志，避免异步消息刷屏。
    bool hotPath = (type == MsgType::Hello || type == MsgType::QueryCandidates ||
                    type == MsgType::ReverseLookup);
    if (hotPath) {
        DLOG(L"HandleRequest: type=%u lock_wait=%lums dict_ready=%d\n",
             (unsigned)type, (unsigned long)lockWait, dictReady ? 1 : 0);
    }

    switch (type) {
        case MsgType::Hello: {
            HelloRequest req = decode_hello_request(r);
            (void)req;
            HelloResponse resp;
            resp.server_version = kProtocolVersion;
            resp.ready = dictReady;
            resp.temp_en_trigger = config_.temp_en_trigger;
            responsePayload = encode_hello_response(resp);
            needResponse = true;
            DLOG(L"HandleRequest: Hello ready=%d (took %lums)\n",
                 dictReady ? 1 : 0, (unsigned long)(GetTickCount64() - tReq));
            return MsgType::Hello;
        }
        case MsgType::QueryCandidates: {
            QueryRequest req = decode_query_request(r);
            ULONGLONG tQ = GetTickCount64();
            fire::QueryResult result;
            if (dict_) result = dict_->get_candidates(req.query, req.page);
            DLOG(L"HandleRequest: Query q='%hs' page=%d results=%zu get_candidates=%lums total=%lums\n",
                 req.query.c_str(), req.page, result.candidates.size(),
                 (unsigned long)(GetTickCount64() - tQ),
                 (unsigned long)(GetTickCount64() - tReq));
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
    DLOG(L"ServeConnection: begin pipe=%p\n", (void*)pipe);
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
    DLOG(L"ServeConnection: loop ended, flushing+disconnecting pipe=%p\n", (void*)pipe);
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
}

}  // namespace firewin
