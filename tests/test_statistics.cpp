//
//  Statistics 单测，对应 Fire/Utils/Statistics.swift
//
#include <cstdio>

#include "test_util.h"
#include "fire/statistics.h"

using namespace fire;

static std::string stats_db_path() { return "test_statistics.sqlite"; }

static Candidate mk(const std::string& text) {
    return Candidate("code", text, CandidateType::Wb);
}

TEST_CASE(stats_records_typing_count) {
    std::remove(stats_db_path().c_str());
    Statistics st(stats_db_path());
    CHECK(st.is_open());
    st.record_candidate(mk("工地"), "notepad.exe", {}, true, false);
    st.record_candidate(mk("你好"), "notepad.exe", {}, true, false);
    // 2 词各 2 字，累计 4 字
    CHECK_EQ(st.query_total_count(), (int64_t)4);
}

TEST_CASE(stats_placeholder_ignored) {
    std::remove(stats_db_path().c_str());
    Statistics st(stats_db_path());
    Candidate ph("", "abc", CandidateType::Placeholder);
    st.record_candidate(ph, "notepad.exe", {}, true, true);
    CHECK_EQ(st.query_total_count(), (int64_t)0);
    CHECK_EQ(st.query_hanzi_frequency_unique_count(), (int64_t)0);
}

TEST_CASE(stats_word_frequency_counts) {
    std::remove(stats_db_path().c_str());
    Statistics st(stats_db_path());
    st.record_candidate(mk("工地"), "app1.exe", {}, false, true);
    st.record_candidate(mk("工地"), "app1.exe", {}, false, true);
    st.record_candidate(mk("你好"), "app1.exe", {}, false, true);
    auto list = st.query_hanzi_frequency(0, "");
    CHECK(list.size() == 2);
    // 频次降序，工地 应排第一，次数 2
    CHECK_STR_EQ(list.front().word, "工地");
    CHECK_EQ(list.front().count, (int64_t)2);
    CHECK_EQ(st.query_hanzi_frequency_unique_count(), (int64_t)2);
}

TEST_CASE(stats_word_frequency_non_chinese_ignored) {
    std::remove(stats_db_path().c_str());
    Statistics st(stats_db_path());
    st.record_candidate(mk("abc"), "app1.exe", {}, false, true);
    CHECK_EQ(st.query_hanzi_frequency_unique_count(), (int64_t)0);
}

TEST_CASE(stats_hanzi_parts) {
    std::remove(stats_db_path().c_str());
    Statistics st(stats_db_path());
    // 顶字 3+1 组合上屏，按拆分词条分别计数
    st.record_candidate(mk("工地一"), "app1.exe", {"工地", "一"}, false, true);
    auto list = st.query_hanzi_frequency(0, "");
    CHECK_EQ(list.size(), (size_t)2);
}

TEST_CASE(stats_per_app_query) {
    std::remove(stats_db_path().c_str());
    Statistics st(stats_db_path());
    st.record_candidate(mk("工地"), "app1.exe", {}, false, true);
    st.record_candidate(mk("你好"), "app2.exe", {}, false, true);
    auto ids = st.query_word_frequency_app_ids();
    CHECK_EQ(ids.size(), (size_t)2);
    auto app1 = st.query_hanzi_frequency(0, "app1.exe");
    CHECK_EQ(app1.size(), (size_t)1);
    CHECK_STR_EQ(app1.front().word, "工地");
}

TEST_CASE(stats_clear) {
    std::remove(stats_db_path().c_str());
    Statistics st(stats_db_path());
    st.record_candidate(mk("工地"), "app1.exe", {}, true, true);
    st.clear();
    CHECK_EQ(st.query_total_count(), (int64_t)0);
    CHECK_EQ(st.query_hanzi_frequency_unique_count(), (int64_t)0);
}

TEST_CASE(stats_clear_hanzi_only) {
    std::remove(stats_db_path().c_str());
    Statistics st(stats_db_path());
    st.record_candidate(mk("工地"), "app1.exe", {}, true, true);
    st.clear_hanzi_frequency();
    CHECK_EQ(st.query_hanzi_frequency_unique_count(), (int64_t)0);
    // data 表仍在
    CHECK_EQ(st.query_total_count(), (int64_t)2);
}

TEST_CASE(stats_export_csv) {
    std::remove(stats_db_path().c_str());
    Statistics st(stats_db_path());
    st.record_candidate(mk("工地"), "app1.exe", {}, false, true);
    std::string csv = "test_stats_export.csv";
    std::remove(csv.c_str());
    CHECK(st.export_hanzi_frequency_csv(csv));
    FILE* f = std::fopen(csv.c_str(), "rb");
    CHECK(f != nullptr);
    if (f) std::fclose(f);
}
