//
//  DictServer.cpp
//
#include "DictServer.h"

#include "DictLog.h"
#include "../common/IpcShared.h"
#include "../config/ConfigStore.h"

#include "fire/query_cache_store.h"
#include "fire/types.h"

#include <fstream>

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
            // 设上限防止恶意/畸形帧触发无限倍增导致 OOM（纵深防御）。
            if (buf.size() >= fire::ipc::kMaxFrameLen) return false;
            buf.resize((std::min)(buf.size() * 2, fire::ipc::kMaxFrameLen));
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
        // 计算初始 config token（canonical json 的 FNV-1a64）。后续仅在 SetConfig /
        // ReloadConfig 时刷新——不再 stat config.json，消除定时轮询。
        RefreshConfigToken();
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

    // 持久化 LRU 缓存快照：从 <userDataDir>/query_cache.bin 恢复上次会话的 1-3 码
    // 首屏缓存。不依赖 sqlite 打开，加载后内存 LRU 立即可用，使前几次查询可在
    // sqlite 尚未热起来时也命中。指纹不符（db 已变/配置已变）则整体丢弃。
    if (newDict && newDict->is_open() && !config_.db_path.empty()) {
        std::wstring cachePathW = firecfg::GetConfigDir() + L"\\query_cache.bin";
        std::string cachePath = WideToUtf8(cachePathW);
        ULONGLONG tLoad = GetTickCount64();
        newDict->SetCacheStorePath(cachePath);
        DLOG(L"Init: cache store load took %lu ms (path=%s)\n",
             (unsigned long)(GetTickCount64() - tLoad), cachePathW.c_str());
    }

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

void DictServer::RefreshConfigToken() {
    // 调用方已持 mu_。重算 canonical json + token，供 CacheValidate / GetConfig 回传。
    // config_token 与 json 内容强一致（同一 json 必同 token），不可能出现 token 没变但
    // json 漂了的情况。仅在 SetConfig / ReloadConfig / Init 调用——零定时轮询。
    cached_config_json_ = firecfg::ConfigStore::Serialize(config_);
    config_token_ = fire::query_cache_store::Fnv1a64(
        reinterpret_cast<const uint8_t*>(cached_config_json_.data()),
        cached_config_json_.size());
}

uint64_t DictServer::ComputeDictToken() {
    // 调用方已持 mu_。实时 stat db mtime/size（不依赖 DictManager 内部 db_fingerprint_*）
    // 以捕获「外部刚改库、还没人查询」窗口。dictd 未就绪返回 0。
    if (!dict_ || !dict_->is_open()) return 0;
    std::error_code ec;
    uint64_t mtime = 0, size = 0;
    if (!config_.db_path.empty()) {
        auto mt = std::filesystem::last_write_time(
            std::filesystem::u8path(config_.db_path), ec);
        if (!ec) mtime = static_cast<uint64_t>(mt.time_since_epoch().count());
        auto sz = std::filesystem::file_size(
            std::filesystem::u8path(config_.db_path), ec);
        if (!ec) size = static_cast<uint64_t>(sz);
    }
    uint32_t cdigest = fire::query_cache_store::ConfigDigest(
        static_cast<int>(config_.code_mode),
        config_.candidate_count, config_.enable_word_input);
    uint64_t user_gen = dict_->user_cache_generation();
    // 小端字节拼接（与 wire 编码一致），FNV-1a64 压成单 u64。DLL 仅比较相等性。
    auto put_u64_le = [](std::vector<uint8_t>& b, uint64_t v) {
        for (int i = 0; i < 8; ++i) b.push_back(
            static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    };
    auto put_u32_le = [](std::vector<uint8_t>& b, uint32_t v) {
        for (int i = 0; i < 4; ++i) b.push_back(
            static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    };
    std::vector<uint8_t> buf;
    buf.reserve(8 + 8 + 4 + 8);
    put_u64_le(buf, mtime);
    put_u64_le(buf, size);
    put_u32_le(buf, cdigest);
    put_u64_le(buf, user_gen);
    return fire::query_cache_store::Fnv1a64(buf);
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
        case MsgType::SaveCache: {
            SaveCacheRequest req = decode_save_cache_request(r);
            // 异步落盘：1 分钟节流 + 子线程保存（不阻塞 ServeConnection）。
            // Deactivate 触发，不保证保存成功（daemon 可能即将退出）。
            if (dict_) SaveCacheAsync(req.app_id);
            return MsgType::SaveCache;  // 异步，无响应
        }
        case MsgType::CacheValidate: {
            CacheValidateRequest req = decode_cache_validate_request(r);
            CacheValidateResponse resp;
            // 策略位：开启动态调频时禁止 DLL 启用本地缓存（候选顺序会被调频记忆改变，
            // 缓存会返回旧顺序）。裁决权下放给 dictd，DLL 不自行读 config 判断，
            // 避免沙箱进程读不到 config 或多份 config 状态不一致（参见 RecordStat
            // 已用 dictd config 覆盖客户端值的先例）。
            resp.allow_dll_cache = !config_.enable_dynamic_frequency;
            // 候选缓存 token 综合指纹（实时 stat db mtime/size + ConfigDigest +
            // user_cache_generation）。dictd 未就绪时返回 0，DLL 见 0 即禁用缓存。
            resp.token = ComputeDictToken();
            if (resp.token == 0) {
                resp.allow_dll_cache = false;
            }
            // ---- config 部分（config 收敛到 dictd）----
            // config_token 始终回传；config_json 仅在客户端 token 不一致时填全量，
            // 省传输。token 一致即 config 未变，DLL 无需更新。
            resp.config_token = config_token_;
            if (req.client_config_token != config_token_) {
                resp.config_json = cached_config_json_;
            }
            // 数据文件路径（供 config.exe 直接 I/O；DLL 沙箱一般不读但无害回传）。
            resp.db_path = config_.db_path;
            resp.stats_db_path = config_.stats_db_path;
            resp.user_dict_path = WideToUtf8(firecfg::GetUserDictPath());
            resp.cache_store_path =
                WideToUtf8(firecfg::GetConfigDir() + L"\\query_cache.bin");
            responsePayload = encode_cache_validate_response(resp);
            needResponse = true;
            return MsgType::CacheValidate;
        }
        case MsgType::GetConfig: {
            // config.exe 打开时拉全量 config + 数据文件路径（同步）。
            // config_json 在 token 一致时留空省传输。
            GetConfigRequest req = decode_get_config_request(r);
            GetConfigResponse resp;
            resp.config_token = config_token_;
            if (req.client_config_token != config_token_) {
                resp.config_json = cached_config_json_;
            }
            resp.db_path = config_.db_path;
            resp.stats_db_path = config_.stats_db_path;
            resp.user_dict_path = WideToUtf8(firecfg::GetUserDictPath());
            resp.cache_store_path =
                WideToUtf8(firecfg::GetConfigDir() + L"\\query_cache.bin");
            responsePayload = encode_get_config_response(resp);
            needResponse = true;
            return MsgType::GetConfig;
        }
        case MsgType::SetConfig: {
            // config.exe 委托 dictd 写 config.json + 热重载（同步）。
            // dictd 是 config.json 唯一写者（原子 temp+rename），避免多进程写竞态。
            SetConfigRequest req = decode_set_config_request(r);
            SetConfigResponse resp;
            if (!r.ok() || req.config_json.empty()) {
                responsePayload = encode_set_config_response(resp);
                needResponse = true;
                return MsgType::SetConfig;
            }
            // 1) 原地解析填 config_（引擎/DictManager 持引用，即见）。
            firecfg::ConfigStore::LoadFromString(config_, req.config_json);
            if (config_.custom_punctuation_settings.empty()) {
                config_.custom_punctuation_settings = fire::default_punctuation();
            }
            // 2) 原子写盘（规范化 canonical json）。
            std::string canonical = firecfg::ConfigStore::Serialize(config_);
            bool wrote = firecfg::ConfigStore::SaveAtomicFromString(canonical);
            DLOG(L"SetConfig: atomic write %hs (json %zu bytes)\n",
                 wrote ? L"OK" : L"FAIL", canonical.size());
            // 3) 刷新 config token（canonical json 的 FNV-1a64）。
            RefreshConfigToken();
            // 4) 可选连带重载：user-dict 改了 / db 文件被替换。
            if (req.reload_user_dict && dict_) {
                // 从 user-dict.txt 重读内容导入 db（config.exe 已直接写文件）。
                std::ifstream f(firecfg::GetUserDictPath(), std::ios::binary);
                std::string content((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
                dict_->update_user_dict(content);  // 内部 clear_query_cache → generation++
            }
            if (req.reinit_dict && dict_) {
                dict_->reinit();  // 重新打开 db 句柄；内部 clear_query_cache → generation++
            }
            resp.ok = wrote;
            resp.new_config_token = config_token_;
            resp.new_dict_token = ComputeDictToken();
            responsePayload = encode_set_config_response(resp);
            needResponse = true;
            return MsgType::SetConfig;
        }
        case MsgType::ReloadConfig: {
            // 通知 dictd 从磁盘重读 config.json（异步 fire-and-forget）。
            // 供 fire_dictd.exe --reload-config 命令行 / install.ps1：外部改了 config.json
            // 后触发，使 dictd 立即生效。规范化写回（保证后续 Serialize 一致）+ reinit。
            ReloadConfigRequest req = decode_reload_config_request(r);
            firecfg::ConfigStore::Load(config_);
            if (config_.custom_punctuation_settings.empty()) {
                config_.custom_punctuation_settings = fire::default_punctuation();
            }
            // 规范化写回（解析后再序列化，消除手写 json 的格式差异，保证 token 稳定）。
            firecfg::ConfigStore::SaveAtomicFromString(
                firecfg::ConfigStore::Serialize(config_));
            RefreshConfigToken();
            if (dict_) dict_->reinit();  // 配置可能影响候选，重建缓存
            DLOG(L"ReloadConfig: reloaded from disk (source='%hs') cfg_token=%llu\n",
                 req.source.c_str(), (unsigned long long)config_token_);
            return MsgType::ReloadConfig;  // 异步，无响应
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

void DictServer::SaveCacheAsync(const std::string& source_app_id) {
    // 注意：本函数由 HandleRequest 调用，调用方已持有 mu_（std::mutex 不可重入），
    // 故此处直接在当前线程（已持锁）取快照，不再加锁。取快照是微秒级拷贝 LRU + 指纹。
    ULONGLONG now = GetTickCount64();
    if (last_cache_save_tick_ != 0 && now - last_cache_save_tick_ < 60000) {
        DLOG(L"SaveCacheAsync: throttled (last save %lums ago, source='%hs')\n",
             (unsigned long)(now - last_cache_save_tick_), source_app_id.c_str());
        return;
    }

    std::string path = dict_ ? dict_->cache_store_path() : std::string{};
    if (path.empty() || !dict_) return;

    // 持锁（当前线程已持）取一致性快照，拷贝出值类型数据。
    fire::CacheStoreSnapshot snap;
    if (!dict_->SnapshotCacheStore(snap)) return;  // LRU 为空/未启用持久化
    last_cache_save_tick_ = now;

    // detached 子线程只做文件 IO（按值捕获 path + snap，不捕获 this，避免 UAF）。
    std::thread([path = std::move(path), snap = std::move(snap), source_app_id]() {
        ULONGLONG t = GetTickCount64();
        fire::query_cache_store::Save(path, snap);
        DLOG(L"SaveCacheAsync: saved %zu entries in %lums (source='%hs')\n",
             snap.entries.size(), (unsigned long)(GetTickCount64() - t),
             source_app_id.c_str());
    }).detach();
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
