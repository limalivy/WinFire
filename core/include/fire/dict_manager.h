//
//  dict_manager.h — 对应 Fire/DictManager.swift
//  SQLite glob 前缀查询、分页、LRU 缓存、动态调频、用户词库。
//
#pragma once

#include <filesystem>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fire/candidate.h"
#include "fire/config.h"
#include "fire/query_cache_store.h"  // CacheStoreSnapshot（SnapshotCacheStore 用）

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

    // 设置持久化缓存文件路径（如 <userDataDir>/query_cache.bin）。
    // 非空时：构造时从该文件加载上次会话的 1-3 码首屏缓存；析构时全量快照写回；
    // 数据库变更（reinit/prepend/update_user_dict）时整体删除失效。
    // 必须在构造后、首次查询前调用（构造函数已尝试加载，路径若此时为空则跳过）。
    // 注意：构造时路径未知，故采用「构造时不加载 + 本方法触发加载」的顺序。
    void SetCacheStorePath(const std::string& path);

    // 把当前内存 LRU + 指纹快照到 out（供外部在持锁后调用，分离「取快照」与「写文件」
    // 两步：取快照在锁内微秒级，写文件在锁外避免阻塞 ServeConnection）。
    // 返回是否有数据可存（path 非空且 LRU 非空）。
    bool SnapshotCacheStore(fire::CacheStoreSnapshot& out);

    // 持久化缓存文件路径（供 daemon 持有以便子线程写文件）。
    const std::string& cache_store_path() const { return cache_store_path_; }

private:
    Config& config_;
    sqlite3* db_ = nullptr;

    // 持久化 LRU 缓存快照。空字符串=不持久化（如内核测试）。
    std::string cache_store_path_;
    // 加载快照时记录的 db 指纹（mtime 计数 + 文件大小），用于运行中校验是否仍一致。
    int64_t db_fingerprint_mtime_ = 0;
    int64_t db_fingerprint_size_ = 0;
    // 加载持久快照：校验指纹（db mtime/size/config 摘要）后把条目填进内存 LRU。
    void LoadCacheStore();
    // 把当前内存 LRU 全量快照写文件（析构时调用）。
    void SaveCacheStore();

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

    // 检测 db 文件是否被外部进程（fire_config.exe 词库重建）修改：
    // mtime 变化时调用 reinit() 重新打开数据库并清空查询缓存，
    // 避免输入时仍命中旧词库的 LRU 缓存导致"改了码表不生效"。
    std::filesystem::file_time_type last_db_mtime_{};
    void check_db_changed();
    // 读取 db 文件当前 mtime/size，刷新 last_db_mtime_ 与持久化指纹
    // (db_fingerprint_mtime_/db_fingerprint_size_)。所有改库路径（ctor/reinit/
    // prepend/update_user_dict，经 clear_query_cache 收口）写库后必须调用，
    // 否则 SaveCacheStore 会写入过期指纹，下次冷启动整体丢弃缓存。
    void refresh_db_fingerprint();

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
