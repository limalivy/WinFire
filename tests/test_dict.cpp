//
//  词库查询测试，对应 DictManager.swift
//
#include "test_util.h"
#include "fire/dict_manager.h"

using namespace fire;

static std::string test_db_path() { return "test_dict.sqlite"; }

static void seed() {
    build_test_db(test_db_path(), {
        {"a", "工", "wb", "a"},
        {"aa", "式", "wb", "aa"},
        {"aaa", "工", "wb", "aaa"},
        {"aaaa", "工", "wb", "aaaa"},
        {"aaad", "工期", "wb", "aaad"},
        {"ss", "林", "wb", "ss"},
        {"ss", "林", "py", "lin"},
    });
}

TEST_CASE(dict_prefix_glob_query) {
    seed();
    Config cfg;
    cfg.db_path = test_db_path();
    cfg.code_mode = CodeMode::Wubi;
    cfg.candidate_count = 5;
    DictManager dm(cfg);
    auto r = dm.get_candidates("a", 1);
    // 前缀 a* 应命中多个
    CHECK(r.candidates.size() > 0);
    CHECK(r.candidates.front().type != CandidateType::Placeholder);
}

TEST_CASE(dict_no_match_placeholder) {
    seed();
    Config cfg;
    cfg.db_path = test_db_path();
    cfg.code_mode = CodeMode::Wubi;
    DictManager dm(cfg);
    auto r = dm.get_candidates("zzzz", 1);
    CHECK_EQ(r.candidates.size(), (size_t)1);
    CHECK(r.candidates.front().type == CandidateType::Placeholder);
}

TEST_CASE(dict_temp_en_trigger) {
    seed();
    Config cfg;
    cfg.db_path = test_db_path();
    DictManager dm(cfg);
    auto r = dm.get_candidates(";abc", 1);
    CHECK_EQ(r.candidates.size(), (size_t)1);
    CHECK(r.candidates.front().type == CandidateType::Placeholder);
    // 去掉首字符 ; 后为 abc
    CHECK_STR_EQ(r.candidates.front().text, "abc");
}

TEST_CASE(dict_reverse_lookup) {
    seed();
    Config cfg;
    cfg.db_path = test_db_path();
    cfg.candidate_count = 5;
    DictManager dm(cfg);
    auto r = dm.get_reverse_lookup_candidates("lin", 1);
    CHECK(r.candidates.size() >= 1);
    CHECK_STR_EQ(r.candidates.front().text, "林");
    // 展示形码 wbcode = ss
    CHECK_STR_EQ(r.candidates.front().code, "ss");
}

TEST_CASE(dict_pagination_has_next) {
    // 构建 7 条同前缀候选，candidateCount=5 => 第一页 hasNext
    build_test_db(test_db_path(), {
        {"b1", "甲", "wb", "b"},
        {"b2", "乙", "wb", "b"},
        {"b3", "丙", "wb", "b"},
        {"b4", "丁", "wb", "b"},
        {"b5", "戊", "wb", "b"},
        {"b6", "己", "wb", "b"},
        {"b7", "庚", "wb", "b"},
    });
    Config cfg;
    cfg.db_path = test_db_path();
    cfg.code_mode = CodeMode::Wubi;
    cfg.candidate_count = 5;
    DictManager dm(cfg);
    auto r = dm.get_candidates("b", 1);
    CHECK_EQ(r.candidates.size(), (size_t)5);
    CHECK(r.has_next);
    auto r2 = dm.get_candidates("b", 2);
    CHECK(r2.candidates.size() >= 1);
    CHECK(!r2.has_next);
}

// 简码：candidate.code 应为该 text 在当前查询前缀下最短的 wbcode。
// 五笔简码必为全码前缀（"工"=a/aa/aaa/aaaa），故字典序最小 = 最短 = 简码，
// min(wbcode) O(1) 聚合即可取到（与 macOS Fire 实现一致）。
TEST_CASE(dict_shortest_code_as_simple_code) {
    seed();
    Config cfg;
    cfg.db_path = test_db_path();
    cfg.code_mode = CodeMode::Wubi;
    cfg.candidate_count = 5;
    DictManager dm(cfg);
    // 查 "a"：query glob 'a*' 匹配 a/aa/aaa/aaaa，min(wbcode)="a" = 简码
    auto r = dm.get_candidates("a", 1);
    const Candidate* gong = nullptr;
    for (const auto& c : r.candidates) {
        if (c.text == "工") { gong = &c; break; }
    }
    CHECK(gong != nullptr);
    if (gong) CHECK_STR_EQ(gong->code, "a");
    // 查 "aaa"：query glob 'aaa*' 仅匹配 aaa/aaaa，min(wbcode)="aaa"
    // （"a" 不会被该查询命中，因 a 不以 aaa 为前缀）
    auto r2 = dm.get_candidates("aaa", 1);
    const Candidate* gong2 = nullptr;
    for (const auto& c : r2.candidates) {
        if (c.text == "工") { gong2 = &c; break; }
    }
    CHECK(gong2 != nullptr);
    if (gong2) CHECK_STR_EQ(gong2->code, "aaa");
}

// 回归 BUG3：写库后内存指纹必须刷新为 db 文件当前值，否则后续 SnapshotCacheStore
// 会把过期指纹写进快照、下次冷启动 LoadCacheStore 整体丢弃。
// 改库路径（prepend/update_user_dict 等）都经 clear_query_cache 收口，BUG3 修复在该
// 收口点调 refresh_db_fingerprint。这里验证 prepend 用户词后，快照指纹等于文件实际值。
//
// 设计说明：get_candidates 入口会 check_db_changed（mtime 变→reinit→refresh），它本身
// 也会刷新指纹，故 prepend 后再查一次会「间接刷新」，无法单独证伪「clear_query_cache
// 是否刷新」。本测试因此定位为「契约回归」：断言任意改库序列后快照指纹恒等于文件实际
// 值——任何一条刷新路径（ctor/reinit/clear_query_cache）被误删都会使其失败。
TEST_CASE(dict_prepend_refreshes_fingerprint) {
    seed();
    Config cfg;
    cfg.db_path = test_db_path();
    cfg.code_mode = CodeMode::Wubi;
    cfg.candidate_count = 5;
    DictManager dm(cfg);
    dm.SetCacheStorePath(test_db_path() + ".cache.bin");
    dm.get_candidates("a", 1);  // 填一条缓存，使快照非空

    // prepend 一条用户词：sqlite 写库，db 文件 mtime/size 改变。
    CHECK(dm.prepend_candidate(Candidate("aaaa", "测试词XYZ", CandidateType::User)));

    // prepend 清空了内存 LRU，重新查一次填回缓存（正常会话路径）。
    dm.get_candidates("a", 1);

    // 此刻快照指纹必须等于 prepend 后 db 文件的真实值（旧 bug：可能是 ctor 时刻值）。
    std::error_code ec;
    auto mtime_after = std::filesystem::last_write_time(test_db_path(), ec);
    int64_t expect_mtime = static_cast<int64_t>(mtime_after.time_since_epoch().count());
    int64_t expect_size = static_cast<int64_t>(std::filesystem::file_size(test_db_path(), ec));

    fire::CacheStoreSnapshot snap;
    bool has = dm.SnapshotCacheStore(snap);
    CHECK(has);
    CHECK_EQ(snap.db_size, expect_size);
    CHECK_EQ(snap.db_mtime, expect_mtime);

    std::remove((test_db_path() + ".cache.bin").c_str());
}
