//
//  dict_manager.h — 对应 Fire/DictManager.swift
//  SQLite glob 前缀查询、分页、LRU 缓存、动态调频、用户词库。
//
#pragma once

#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fire/candidate.h"
#include "fire/config.h"

struct sqlite3;
struct sqlite3_stmt;

namespace fire {

struct QueryResult {
    std::vector<Candidate> candidates;
    bool has_next = false;
};

class DictManager {
public:
    explicit DictManager(Config& config);
    ~DictManager();

    DictManager(const DictManager&) = delete;
    DictManager& operator=(const DictManager&) = delete;

    void reinit();  // 重新打开数据库
    void close();
    bool is_open() const { return db_ != nullptr; }

    // 对应 getCandidates(query:page:)
    QueryResult get_candidates(const std::string& query, int page = 1);

    // 对应 getReverseLookupCandidates(pinyin:page:)：` + 拼音反查形码
    QueryResult get_reverse_lookup_candidates(const std::string& pinyin, int page = 1);

    // 对应 rememberDynamicFrequency
    void remember_dynamic_frequency(const std::string& query, const Candidate& candidate);

    // 对应 setCandidateToFirst（control+数字 调整候选顺序）
    void set_candidate_to_first(const std::string& query, const Candidate& candidate);

    // 用户词库
    bool prepend_candidate(const Candidate& candidate);
    void prepend_candidates(const std::vector<Candidate>& candidates);
    void update_user_dict(const std::string& dict_content);
    std::vector<Candidate> get_user_candidates();
    std::string get_user_dict_content();

    char temp_en_trigger() const { return config_.temp_en_trigger; }

private:
    Config& config_;
    sqlite3* db_ = nullptr;

    // 首屏 1-3 码查询缓存（LRU）
    struct CacheEntry {
        std::vector<Candidate> candidates;
        bool has_next;
    };
    std::unordered_map<std::string, CacheEntry> cache_map_;
    std::list<std::string> cache_lru_;
    static constexpr size_t kCacheLimit = 5000;

    // 热路径查询语句缓存：以完整 SQL 文本为 key 复用已编译的 sqlite3_stmt*，
    // 避免每次按键都 sqlite3_prepare_v2 重新编译 SQL（P0 优化）。
    std::unordered_map<std::string, sqlite3_stmt*> stmt_cache_;
    // 取一条与 sql 对应的已编译语句：命中则 reset + clear_bindings 复用，
    // 未命中则编译并存入缓存。失败或 db_ 为空返回 nullptr。
    sqlite3_stmt* acquire_stmt(const std::string& sql);
    // 销毁所有缓存语句（关闭数据库前必须调用）。
    void finalize_statements();

    void prepare_statement();
    void clear_query_cache();
    void cache_put(const std::string& key, const CacheEntry& entry);
    bool cache_get(const std::string& key, CacheEntry& out);

    std::string cache_key(const std::string& query) const;
    std::string statement_sql(bool use_pagination) const;
    std::string query_like(const std::string& origin) const;
    bool should_apply_dynamic_frequency(const std::string& query) const;
    std::vector<Candidate> reorder_by_dynamic_frequency(
        const std::vector<Candidate>& candidates, const std::string& query) const;
    QueryResult paginate(const std::vector<Candidate>& candidates, int page, int count) const;
    std::vector<Candidate> punctuation_candidates(const std::string& query) const;
    Candidate apply_user_vars_if_needed(const Candidate& c) const;
    std::string replace_text_with_vars(const std::string& text) const;
    int min_id_from_dict_table();
};

}  // namespace fire
