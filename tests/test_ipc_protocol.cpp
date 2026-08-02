//
//  test_ipc_protocol.cpp — IPC 协议编解码往返测试
//
#include "test_util.h"

#include "fire/ipc/protocol.h"
#include "fire/ipc/wire.h"

using namespace fire;
using namespace fire::ipc;

TEST_CASE(ipc_wire_integers_roundtrip) {
    Writer w;
    w.put_u8(0xAB);
    w.put_u16(0x1234);
    w.put_u32(0xDEADBEEF);
    w.put_i32(-12345);
    w.put_i64(-9223372036854775807LL);
    Reader r(w.buffer());
    CHECK_EQ((int)r.get_u8(), 0xAB);
    CHECK_EQ((int)r.get_u16(), 0x1234);
    CHECK_EQ(r.get_u32(), 0xDEADBEEFu);
    CHECK_EQ(r.get_i32(), -12345);
    CHECK_EQ(r.get_i64(), -9223372036854775807LL);
    CHECK(r.ok());
    CHECK(r.at_end());
}

TEST_CASE(ipc_wire_string_roundtrip) {
    Writer w;
    w.put_string("");
    w.put_string("hello");
    w.put_string("五笔输入法");  // UTF-8 多字节
    Reader r(w.buffer());
    CHECK_STR_EQ(r.get_string(), "");
    CHECK_STR_EQ(r.get_string(), "hello");
    CHECK_STR_EQ(r.get_string(), "五笔输入法");
    CHECK(r.ok());
}

TEST_CASE(ipc_wire_candidate_roundtrip) {
    Candidate c("aaaa", "工", CandidateType::Wb, "工");
    Writer w;
    w.put_candidate(c);
    Reader r(w.buffer());
    Candidate got = r.get_candidate();
    CHECK(r.ok());
    CHECK(got == c);
}

TEST_CASE(ipc_wire_query_result_roundtrip) {
    QueryResult qr;
    qr.has_next = true;
    qr.candidates.push_back(Candidate("a", "工", CandidateType::Wb));
    qr.candidates.push_back(Candidate("lin", "林", CandidateType::Py, "林"));
    qr.candidates.push_back(Candidate("u", "自定义", CandidateType::User));
    Writer w;
    w.put_query_result(qr);
    Reader r(w.buffer());
    QueryResult got = r.get_query_result();
    CHECK(r.ok());
    CHECK_EQ(got.has_next, true);
    CHECK_EQ(got.candidates.size(), (size_t)3);
    CHECK(got.candidates[0] == qr.candidates[0]);
    CHECK(got.candidates[1] == qr.candidates[1]);
    CHECK(got.candidates[2] == qr.candidates[2]);
}

TEST_CASE(ipc_wire_empty_query_result) {
    QueryResult qr;  // 空候选，has_next=false
    Writer w;
    w.put_query_result(qr);
    Reader r(w.buffer());
    QueryResult got = r.get_query_result();
    CHECK(r.ok());
    CHECK_EQ(got.has_next, false);
    CHECK_EQ(got.candidates.size(), (size_t)0);
}

TEST_CASE(ipc_header_roundtrip) {
    FrameHeader h;
    h.msg_type = (uint16_t)MsgType::QueryCandidates;
    h.request_id = 42;
    h.payload_len = 100;
    uint8_t buf[kHeaderSize];
    encode_header(h, buf);
    FrameHeader got;
    CHECK(decode_header(buf, kHeaderSize, got));
    CHECK_EQ(got.magic, kMagic);
    CHECK_EQ(got.version, kProtocolVersion);
    CHECK_EQ(got.msg_type, (uint16_t)MsgType::QueryCandidates);
    CHECK_EQ(got.request_id, 42u);
    CHECK_EQ(got.payload_len, 100u);
}

TEST_CASE(ipc_header_bad_magic_rejected) {
    uint8_t buf[kHeaderSize] = {0};  // magic = 0
    FrameHeader got;
    CHECK(!decode_header(buf, kHeaderSize, got));
    // 太短也拒绝
    CHECK(!decode_header(buf, 4, got));
}

TEST_CASE(ipc_build_frame_layout) {
    Writer p;
    p.put_string("abc");
    std::vector<uint8_t> payload = p.take();
    std::vector<uint8_t> frame = build_frame(MsgType::QueryCandidates, 7, payload);
    CHECK_EQ(frame.size(), kHeaderSize + payload.size());
    FrameHeader h;
    CHECK(decode_header(frame.data(), frame.size(), h));
    CHECK_EQ(h.request_id, 7u);
    CHECK_EQ(h.payload_len, (uint32_t)payload.size());
    // payload 部分可解出原字符串
    Reader r(frame.data() + kHeaderSize, h.payload_len);
    CHECK_STR_EQ(r.get_string(), "abc");
}

TEST_CASE(ipc_query_request_roundtrip) {
    QueryRequest req;
    req.query = "aaad";
    req.page = 3;
    auto payload = encode_query_request(req);
    Reader r(payload);
    QueryRequest got = decode_query_request(r);
    CHECK(r.ok());
    CHECK_STR_EQ(got.query, "aaad");
    CHECK_EQ(got.page, 3);
}

TEST_CASE(ipc_hello_roundtrip) {
    HelloRequest req;
    req.client_version = 1;
    req.app_id = "SearchHost.exe";
    auto rp = encode_hello_request(req);
    Reader r1(rp);
    HelloRequest gotReq = decode_hello_request(r1);
    CHECK(r1.ok());
    CHECK_STR_EQ(gotReq.app_id, "SearchHost.exe");

    HelloResponse resp;
    resp.server_version = 1;
    resp.ready = true;
    resp.temp_en_trigger = ';';
    auto sp = encode_hello_response(resp);
    Reader r2(sp);
    HelloResponse gotResp = decode_hello_response(r2);
    CHECK(r2.ok());
    CHECK_EQ(gotResp.ready, true);
    CHECK_EQ((int)gotResp.temp_en_trigger, (int)';');
}

TEST_CASE(ipc_cache_validate_roundtrip) {
    // 请求往返
    CacheValidateRequest req;
    req.client_version = 1;
    req.app_id = "notepad.exe";
    req.client_config_token = 0x0123456789ABCDEFULL;
    auto rp = encode_cache_validate_request(req);
    Reader r1(rp);
    CacheValidateRequest gotReq = decode_cache_validate_request(r1);
    CHECK(r1.ok());
    CHECK_EQ(gotReq.client_version, 1);
    CHECK_STR_EQ(gotReq.app_id, "notepad.exe");
    CHECK_EQ(gotReq.client_config_token, 0x0123456789ABCDEFULL);

    // 响应往返：全字段（含 config_token / config_json / 4 个 path）
    CacheValidateResponse resp;
    resp.token = 0x0123456789ABCDEFULL;
    resp.allow_dll_cache = true;
    resp.config_token = 0xFEDCBA9876543210ULL;
    resp.config_json = "{\"candidateCount\":9}";
    resp.db_path = "C:\\data\\wb_py_dict.sqlite";
    resp.stats_db_path = "C:\\data\\statistics.sqlite";
    resp.user_dict_path = "C:\\data\\user-dict.txt";
    resp.cache_store_path = "C:\\data\\query_cache.bin";
    auto sp = encode_cache_validate_response(resp);
    Reader r2(sp);
    CacheValidateResponse gotResp = decode_cache_validate_response(r2);
    CHECK(r2.ok());
    CHECK_EQ(gotResp.token, 0x0123456789ABCDEFULL);
    CHECK_EQ(gotResp.allow_dll_cache, true);
    CHECK_EQ(gotResp.config_token, 0xFEDCBA9876543210ULL);
    CHECK_STR_EQ(gotResp.config_json, "{\"candidateCount\":9}");
    CHECK_STR_EQ(gotResp.db_path, "C:\\data\\wb_py_dict.sqlite");
    CHECK_STR_EQ(gotResp.stats_db_path, "C:\\data\\statistics.sqlite");
    CHECK_STR_EQ(gotResp.user_dict_path, "C:\\data\\user-dict.txt");
    CHECK_STR_EQ(gotResp.cache_store_path, "C:\\data\\query_cache.bin");

    // 边界：token=0、allow_dll_cache=false、config_json 空（token 一致 / dictd 未就绪）
    CacheValidateResponse resp2;
    resp2.token = 0;
    resp2.allow_dll_cache = false;
    resp2.config_token = 0;
    auto sp2 = encode_cache_validate_response(resp2);
    Reader r3(sp2);
    CacheValidateResponse got2 = decode_cache_validate_response(r3);
    CHECK(r3.ok());
    CHECK_EQ(got2.token, 0ULL);
    CHECK_EQ(got2.allow_dll_cache, false);
    CHECK_EQ(got2.config_token, 0ULL);
    CHECK_STR_EQ(got2.config_json, "");
}

TEST_CASE(ipc_get_config_roundtrip) {
    // token 一致 → config_json 空
    GetConfigRequest req;
    req.client_config_token = 42;
    auto rp = encode_get_config_request(req);
    Reader r1(rp);
    GetConfigRequest gotReq = decode_get_config_request(r1);
    CHECK(r1.ok());
    CHECK_EQ(gotReq.client_config_token, 42u);

    GetConfigResponse resp;
    resp.config_token = 42;
    // config_json 空（token 一致）
    resp.db_path = "/db.sqlite";
    resp.stats_db_path = "/stats.sqlite";
    resp.user_dict_path = "/user-dict.txt";
    resp.cache_store_path = "/cache.bin";
    auto sp = encode_get_config_response(resp);
    Reader r2(sp);
    GetConfigResponse gotResp = decode_get_config_response(r2);
    CHECK(r2.ok());
    CHECK_EQ(gotResp.config_token, 42u);
    CHECK_STR_EQ(gotResp.config_json, "");
    CHECK_STR_EQ(gotResp.db_path, "/db.sqlite");
    CHECK_STR_EQ(gotResp.stats_db_path, "/stats.sqlite");
    CHECK_STR_EQ(gotResp.user_dict_path, "/user-dict.txt");
    CHECK_STR_EQ(gotResp.cache_store_path, "/cache.bin");

    // 全量 config_json（UTF-8 多字节）
    GetConfigResponse resp2;
    resp2.config_token = 99;
    resp2.config_json = "{\"codeMode\":1,\"候选\":9}";
    auto sp2 = encode_get_config_response(resp2);
    Reader r3(sp2);
    GetConfigResponse got2 = decode_get_config_response(r3);
    CHECK(r3.ok());
    CHECK_EQ(got2.config_token, 99u);
    CHECK_STR_EQ(got2.config_json, "{\"codeMode\":1,\"候选\":9}");
}

TEST_CASE(ipc_set_config_roundtrip) {
    SetConfigRequest req;
    req.config_json = "{\"candidateCount\":7}";
    req.reload_user_dict = true;
    req.reinit_dict = false;
    auto payload = encode_set_config_request(req);
    Reader r(payload);
    SetConfigRequest got = decode_set_config_request(r);
    CHECK(r.ok());
    CHECK_STR_EQ(got.config_json, "{\"candidateCount\":7}");
    CHECK_EQ(got.reload_user_dict, true);
    CHECK_EQ(got.reinit_dict, false);

    SetConfigResponse resp;
    resp.ok = true;
    resp.new_config_token = 0xABCDEF0123456789ULL;
    resp.new_dict_token = 0x1112223334445556ULL;
    auto sp = encode_set_config_response(resp);
    Reader r2(sp);
    SetConfigResponse gotResp = decode_set_config_response(r2);
    CHECK(r2.ok());
    CHECK_EQ(gotResp.ok, true);
    CHECK_EQ(gotResp.new_config_token, 0xABCDEF0123456789ULL);
    CHECK_EQ(gotResp.new_dict_token, 0x1112223334445556ULL);

    // 边界：ok=false（写盘失败）
    SetConfigResponse resp2;
    auto sp2 = encode_set_config_response(resp2);
    Reader r3(sp2);
    SetConfigResponse got2 = decode_set_config_response(r3);
    CHECK(r3.ok());
    CHECK_EQ(got2.ok, false);
}

TEST_CASE(ipc_reload_config_roundtrip) {
    ReloadConfigRequest req;
    req.source = "install.ps1";
    auto payload = encode_reload_config_request(req);
    Reader r(payload);
    ReloadConfigRequest got = decode_reload_config_request(r);
    CHECK(r.ok());
    CHECK_STR_EQ(got.source, "install.ps1");

    // 空来源
    ReloadConfigRequest req2;
    auto p2 = encode_reload_config_request(req2);
    Reader r2(p2);
    ReloadConfigRequest got2 = decode_reload_config_request(r2);
    CHECK(r2.ok());
    CHECK_STR_EQ(got2.source, "");
}

TEST_CASE(ipc_freq_request_roundtrip) {
    FreqRequest req;
    req.query = "ss";
    req.candidate = Candidate("ss", "林", CandidateType::Wb);
    auto payload = encode_freq_request(req);
    Reader r(payload);
    FreqRequest got = decode_freq_request(r);
    CHECK(r.ok());
    CHECK_STR_EQ(got.query, "ss");
    CHECK(got.candidate == req.candidate);
}

TEST_CASE(ipc_save_cache_request_roundtrip) {
    SaveCacheRequest req;
    req.app_id = "chrome.exe";
    auto payload = encode_save_cache_request(req);
    Reader r(payload);
    SaveCacheRequest got = decode_save_cache_request(r);
    CHECK(r.ok());
    CHECK_STR_EQ(got.app_id, "chrome.exe");
}

TEST_CASE(ipc_record_stat_roundtrip) {
    RecordStatRequest req;
    req.candidate = Candidate("gghh", "五目", CandidateType::Wb);
    req.app_id = "notepad.exe";
    req.hanzi_parts = {"五", "目"};
    req.enable_stats = true;
    req.enable_hanzi = false;
    auto payload = encode_record_stat(req);
    Reader r(payload);
    RecordStatRequest got = decode_record_stat(r);
    CHECK(r.ok());
    CHECK(got.candidate == req.candidate);
    CHECK_STR_EQ(got.app_id, "notepad.exe");
    CHECK_EQ(got.hanzi_parts.size(), (size_t)2);
    CHECK_STR_EQ(got.hanzi_parts[0], "五");
    CHECK_STR_EQ(got.hanzi_parts[1], "目");
    CHECK_EQ(got.enable_stats, true);
    CHECK_EQ(got.enable_hanzi, false);
}

TEST_CASE(ipc_error_roundtrip) {
    ErrorMessage err;
    err.code = -3;
    err.message = "db not open";
    auto payload = encode_error(err);
    Reader r(payload);
    ErrorMessage got = decode_error(r);
    CHECK(r.ok());
    CHECK_EQ(got.code, -3);
    CHECK_STR_EQ(got.message, "db not open");
}

TEST_CASE(ipc_candidate_list_roundtrip) {
    std::vector<Candidate> list = {
        Candidate("a", "工", CandidateType::User),
        Candidate("b", "本", CandidateType::User),
    };
    auto payload = encode_candidate_list(list);
    Reader r(payload);
    auto got = decode_candidate_list(r);
    CHECK(r.ok());
    CHECK_EQ(got.size(), (size_t)2);
    CHECK(got[0] == list[0]);
    CHECK(got[1] == list[1]);
}

TEST_CASE(ipc_reader_truncated_string_fails) {
    // 声明长度 100 但实际只有 3 字节 → 越界，ok() 变 false，不崩溃
    Writer w;
    w.put_u32(100);
    w.put_u8('a');
    w.put_u8('b');
    w.put_u8('c');
    Reader r(w.buffer());
    std::string s = r.get_string();
    CHECK(!r.ok());
    CHECK(s.empty());
}

TEST_CASE(ipc_reader_truncated_int_fails) {
    Writer w;
    w.put_u8(0x01);  // 只有 1 字节，get_u32 需要 4 字节
    Reader r(w.buffer());
    uint32_t v = r.get_u32();
    CHECK(!r.ok());
    CHECK_EQ(v, 0u);
}

TEST_CASE(ipc_reader_bad_candidate_type_fails) {
    // type 字节非法（99）→ Reader 置错
    Writer w;
    w.put_string("x");
    w.put_string("y");
    w.put_u8(99);
    w.put_string("z");
    Reader r(w.buffer());
    r.get_candidate();
    CHECK(!r.ok());
}
