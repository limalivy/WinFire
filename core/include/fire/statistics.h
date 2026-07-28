//
//  statistics.h — 对应 Fire/Utils/Statistics.swift
//  输入统计库（平台无关）：SQLite 存储每次上屏字词，按日期/应用统计打字量与字词频。
//  与 macOS 版差异：去掉 SQLCipher 加密与异步回填，改为同步写入。
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fire/candidate.h"

struct sqlite3;

namespace fire {

// 某一天的输入字数
struct DateCount {
    std::string date;   // "yyyy-MM-dd"
    int64_t count = 0;
};

// 词条频次
struct WordCount {
    std::string word;
    int64_t count = 0;
};

class Statistics {
public:
    explicit Statistics(const std::string& db_path);
    ~Statistics();

    Statistics(const Statistics&) = delete;
    Statistics& operator=(const Statistics&) = delete;

    bool is_open() const { return db_ != nullptr; }

    // 记录一次上屏（对应 Statistics.listener）。
    //   enable_stats     打字量统计（写 data 表）
    //   enable_hanzi     字词频统计（写 word_freq 表）
    //   hanzi_parts      顶字组合拆分后的词条；为空则按 candidate.text 计数
    void record_candidate(const Candidate& candidate,
                          const std::string& app_id,
                          const std::vector<std::string>& hanzi_parts,
                          bool enable_stats,
                          bool enable_hanzi);

    // 累计输入字数（sum(length(text))）
    int64_t query_total_count();

    // 按日期区间统计每日字数（date 闭区间，格式 "yyyy-MM-dd"），按日期降序
    std::vector<DateCount> query_count_by_date(const std::string& start_date,
                                               const std::string& end_date);

    // 字词频列表：limit<=0 表示全部；app_id 为空表示跨应用聚合
    std::vector<WordCount> query_hanzi_frequency(int limit = 0, const std::string& app_id = "");

    // 已统计的不同词条数
    int64_t query_hanzi_frequency_unique_count(const std::string& app_id = "");

    // 出现过的应用列表
    std::vector<std::string> query_word_frequency_app_ids();

    // 清除全部统计
    void clear();
    // 仅清除字词频统计
    void clear_hanzi_frequency();

    // 导出字词频为 CSV（含 BOM，列：应用ID,词,次数）
    bool export_hanzi_frequency_csv(const std::string& path);

private:
    void init_db(const std::string& db_path);
    bool migrate();
    int32_t get_version();
    bool set_version(int32_t version);

    int64_t insert_data_row(const Candidate& candidate, const std::string& app_id);
    bool update_word_frequency(const std::string& word, const std::string& app_id);

    static std::string normalize_app_id(const std::string& raw);
    static bool is_chinese_word(const std::string& text);

    sqlite3* db_ = nullptr;
};

}  // namespace fire
