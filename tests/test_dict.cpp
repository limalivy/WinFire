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
