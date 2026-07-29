//
//  dict_manager.cpp — 对应 Fire/DictManager.swift
//  SQLite glob 前缀查询、分页、LRU 缓存、动态调频、用户词库。
//
#include "fire/dict_manager.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <sstream>

namespace fire {

namespace {
// 单个数字转字符串
std::string int_to_string(int v) {
    return std::to_string(v);
}

// 判断字符串是否全部为 [a-zA-Z]
bool is_all_alpha(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    }
    return true;
}

std::string to_lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return r;
}

// 计算 UTF-8 字符数（用于 length(text)=1 类判断，这里用于 punctuationCandidates suffix）
size_t utf8_count(const std::string& s) {
    size_t count = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) i += 1;
        else if ((c >> 5) == 0x6) i += 2;
        else if ((c >> 4) == 0xE) i += 3;
        else if ((c >> 3) == 0x1E) i += 4;
        else i += 1;
        ++count;
    }
    return count;
}

// 安全读取列文本：列值为 NULL 时 sqlite3_column_text 返回 nullptr，
// 用 nullptr 构造 std::string 是未定义行为，这里统一返回空串。
std::string col_text(sqlite3_stmt* stmt, int col) {
    const unsigned char* p = sqlite3_column_text(stmt, col);
    return p ? reinterpret_cast<const char*>(p) : std::string();
}

// 取 UTF-8 后 n 个字符
std::string utf8_take_suffix(const std::string& s, size_t n) {
    std::vector<size_t> starts;
    for (size_t i = 0; i < s.size();) {
        starts.push_back(i);
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) i += 1;
        else if ((c >> 5) == 0x6) i += 2;
        else if ((c >> 4) == 0xE) i += 3;
        else if ((c >> 3) == 0x1E) i += 4;
        else i += 1;
    }
    if (n >= starts.size()) return s;
    return s.substr(starts[starts.size() - n]);
}
}  // namespace

DictManager::DictManager(Config& config) : config_(config) {
    prepare_statement();
}

DictManager::~DictManager() {
    close();
}

void DictManager::reinit() {
    close();
    prepare_statement();
}

void DictManager::close() {
    if (db_) {
        // 关闭数据库前必须先 finalize 所有缓存语句，否则 sqlite3_close_v2 会因
        // 存在未释放的 prepared statement 而延迟释放句柄。
        finalize_statements();
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
    clear_query_cache();
}

// 取一条与 sql 对应的已编译语句。命中缓存则 reset + clear_bindings 后复用，
// 未命中则编译并存入缓存。避免热路径上重复 prepare/finalize 同一条 SQL。
sqlite3_stmt* DictManager::acquire_stmt(const std::string& sql) {
    if (!db_) return nullptr;
    auto it = stmt_cache_.find(sql);
    if (it != stmt_cache_.end()) {
        sqlite3_reset(it->second);
        sqlite3_clear_bindings(it->second);
        return it->second;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        if (stmt) sqlite3_finalize(stmt);
        return nullptr;
    }
    stmt_cache_[sql] = stmt;
    return stmt;
}

void DictManager::finalize_statements() {
    for (auto& kv : stmt_cache_) {
        if (kv.second) sqlite3_finalize(kv.second);
    }
    stmt_cache_.clear();
}

void DictManager::prepare_statement() {
    if (db_ == nullptr && !config_.db_path.empty()) {
        if (sqlite3_open_v2(config_.db_path.c_str(), &db_, SQLITE_OPEN_READWRITE, nullptr) !=
            SQLITE_OK) {
            // 打开失败（文件缺失/损坏）：关闭句柄并置空，后续查询据 db_==nullptr 短路。
            if (db_) {
                sqlite3_close_v2(db_);
                db_ = nullptr;
            }
            return;
        }
        sqlite3_exec(db_, "PRAGMA case_sensitive_like=ON;", nullptr, nullptr, nullptr);
    }
}

void DictManager::clear_query_cache() {
    cache_map_.clear();
    cache_lru_.clear();
}

void DictManager::cache_put(const std::string& key, const CacheEntry& entry) {
    auto it = cache_map_.find(key);
    if (it != cache_map_.end()) {
        it->second = entry;
        cache_lru_.remove(key);
        cache_lru_.push_front(key);
        return;
    }
    if (cache_map_.size() >= kCacheLimit && !cache_lru_.empty()) {
        std::string evict = cache_lru_.back();
        cache_lru_.pop_back();
        cache_map_.erase(evict);
    }
    cache_map_[key] = entry;
    cache_lru_.push_front(key);
}

bool DictManager::cache_get(const std::string& key, CacheEntry& out) {
    auto it = cache_map_.find(key);
    if (it == cache_map_.end()) return false;
    out = it->second;
    cache_lru_.remove(key);
    cache_lru_.push_front(key);
    return true;
}

// 对应 cacheKey(query:)
std::string DictManager::cache_key(const std::string& query) const {
    std::ostringstream oss;
    oss << query << "|" << static_cast<int>(config_.code_mode) << "|"
        << config_.candidate_count << "|" << (config_.enable_word_input ? "true" : "false");
    return oss.str();
}

// 对应 getStatementSql
std::string DictManager::statement_sql(bool use_pagination) const {
    int candidate_count = config_.candidate_count;
    CodeMode code_mode = config_.code_mode;
    std::string word_input_filter = config_.enable_word_input ? "" : "and length(text) = 1";
    std::string type_filter;
    if (code_mode == CodeMode::Wubi) {
        type_filter = "and type in ('wb', 'user')";
    } else if (code_mode == CodeMode::Pinyin) {
        type_filter = "and type in ('py', 'user')";
    }
    std::string code_agg =
        (code_mode == CodeMode::WubiPinyin) ? "max(wbcode)" : "min(wbcode)";
    std::ostringstream sql;
    sql << "select " << code_agg << ", text, type, min(query) as query "
        << "from wb_py_dict "
        << "where query glob :queryLike " << type_filter << " " << word_input_filter << " "
        << "group by text order by query, id";
    if (use_pagination) {
        sql << " limit :offset, " << (candidate_count + 1);
    }
    return sql.str();
}

// 对应 getQueryLike
std::string DictManager::query_like(const std::string& origin) const {
    return origin.empty() ? origin : origin + "*";
}

bool DictManager::should_apply_dynamic_frequency(const std::string& query) const {
    return config_.enable_dynamic_frequency && utf8_count(query) >= 4;
}

std::vector<Candidate> DictManager::reorder_by_dynamic_frequency(
    const std::vector<Candidate>& candidates, const std::string& query) const {
    if (!should_apply_dynamic_frequency(query)) {
        return candidates;
    }
    auto mit = config_.dynamic_frequency_memory.find(query);
    if (mit == config_.dynamic_frequency_memory.end()) {
        return candidates;
    }
    const std::string& preferred_text = mit->second;
    int preferred_index = -1;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].text == preferred_text &&
            candidates[i].type != CandidateType::Placeholder) {
            preferred_index = static_cast<int>(i);
            break;
        }
    }
    if (preferred_index <= 0) {
        return candidates;
    }
    std::vector<Candidate> reordered = candidates;
    Candidate preferred = reordered[preferred_index];
    reordered.erase(reordered.begin() + preferred_index);
    reordered.insert(reordered.begin(), preferred);
    return reordered;
}

QueryResult DictManager::paginate(const std::vector<Candidate>& candidates, int page,
                                  int count) const {
    QueryResult result;
    int start = std::max(0, (page - 1) * count);
    if (start >= static_cast<int>(candidates.size())) {
        return result;
    }
    int end = std::min(start + count, static_cast<int>(candidates.size()));
    result.candidates.assign(candidates.begin() + start, candidates.begin() + end);
    result.has_next = end < static_cast<int>(candidates.size());
    return result;
}

// 对应 replaceTextWithVars：{yyyy}{MM}{dd}{HH}{mm}{ss}
std::string DictManager::replace_text_with_vars(const std::string& text) const {
    std::time_t t = std::time(nullptr);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[8];
    auto fmt = [&](const char* f) {
        std::strftime(buf, sizeof(buf), f, &tm_buf);
        return std::string(buf);
    };
    std::string result = text;
    const std::pair<std::string, std::string> vars[] = {
        {"{yyyy}", fmt("%Y")}, {"{MM}", fmt("%m")}, {"{dd}", fmt("%d")},
        {"{HH}", fmt("%H")},   {"{mm}", fmt("%M")}, {"{ss}", fmt("%S")},
    };
    for (const auto& kv : vars) {
        size_t pos = 0;
        while ((pos = result.find(kv.first, pos)) != std::string::npos) {
            result.replace(pos, kv.first.size(), kv.second);
            pos += kv.second.size();
        }
    }
    return result;
}

Candidate DictManager::apply_user_vars_if_needed(const Candidate& candidate) const {
    if (candidate.type != CandidateType::User) return candidate;
    return Candidate(candidate.code, replace_text_with_vars(candidate.text), candidate.type);
}

// 对应 punctuationCandidates
std::vector<Candidate> DictManager::punctuation_candidates(const std::string& query) const {
    std::string text = (utf8_count(query) == 1)
                           ? query
                           : utf8_take_suffix(query, utf8_count(query) - 1);
    return {Candidate(query, text, CandidateType::Placeholder,
                      "临时英文(空格输出半角符号,连敲;键两下输出全角符号)")};
}

int DictManager::min_id_from_dict_table() {
    const char* sql = "select min(id) from wb_py_dict";
    sqlite3_stmt* stmt = nullptr;
    int min_id = 0;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            min_id = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    return min_id;
}

// 对应 getReverseLookupCandidates
QueryResult DictManager::get_reverse_lookup_candidates(const std::string& pinyin, int page) {
    prepare_statement();
    QueryResult result;
    if (!db_) return result;
    if (pinyin.empty()) return result;
    if (!is_all_alpha(pinyin)) return result;

    int candidate_count = config_.candidate_count;
    std::string word_input_filter = config_.enable_word_input ? "" : "and length(text) = 1";
    std::ostringstream sql;
    sql << "select min(wbcode) as wbcode, text, type, min(query) as query "
        << "from wb_py_dict where query glob :queryLike and type = 'py' " << word_input_filter
        << " group by text order by query, id limit :offset, " << (candidate_count + 1);

    sqlite3_stmt* stmt = acquire_stmt(sql.str());
    if (!stmt) return result;
    std::string like = query_like(to_lower(pinyin));
    sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":queryLike"), like.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":offset"),
                     (page - 1) * candidate_count);

    std::vector<Candidate> candidates;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string wbcode = col_text(stmt, 0);
        std::string text = col_text(stmt, 1);
        // 反查结果按 py 类型渲染，展示形码
        candidates.emplace_back(wbcode, text, CandidateType::Py);
    }
    // 语句缓存复用：reset 释放本次执行状态，保留已编译语句。
    sqlite3_reset(stmt);

    bool has_next = static_cast<int>(candidates.size()) > candidate_count;
    if (has_next) candidates.pop_back();
    result.candidates = std::move(candidates);
    result.has_next = has_next;
    return result;
}

// 对应 getCandidates
QueryResult DictManager::get_candidates(const std::string& query, int page) {
    prepare_statement();
    QueryResult result;
    if (query.empty()) return result;

    if (query[0] == config_.temp_en_trigger) {
        result.candidates = punctuation_candidates(query);
        result.has_next = false;
        return result;
    }

    // 仅缓存 1-3 码的首屏结果
    bool cache_eligible = (page == 1) && (utf8_count(query) <= 3);
    std::string key = cache_key(query);
    if (cache_eligible) {
        CacheEntry cached;
        if (cache_get(key, cached)) {
            for (const auto& c : cached.candidates) {
                result.candidates.push_back(apply_user_vars_if_needed(c));
            }
            result.has_next = cached.has_next;
            return result;
        }
    }

    std::string like = query_like(query);
    bool dyn = should_apply_dynamic_frequency(query);

    if (!db_) return result;
    sqlite3_stmt* stmt = acquire_stmt(statement_sql(!dyn));
    if (!stmt) return result;
    sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":queryLike"), like.c_str(), -1,
                      SQLITE_TRANSIENT);
    if (!dyn) {
        sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":offset"),
                         (page - 1) * config_.candidate_count);
    }

    std::vector<Candidate> candidates;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string code = col_text(stmt, 0);
        std::string text = col_text(stmt, 1);
        std::string type_str = col_text(stmt, 2);
        CandidateType type = CandidateType::Wb;
        candidate_type_from_string(type_str, type);
        candidates.emplace_back(code, text, type);
    }
    // 语句缓存复用：查完 reset 释放本次执行状态，但保留已编译语句供下次按键复用。
    sqlite3_reset(stmt);

    int count = config_.candidate_count;
    bool has_next;
    if (dyn) {
        std::vector<Candidate> reordered = reorder_by_dynamic_frequency(candidates, query);
        QueryResult paged = paginate(reordered, page, count);
        candidates = std::move(paged.candidates);
        has_next = paged.has_next;
    } else {
        int all_count = static_cast<int>(candidates.size());
        if (all_count > count) candidates.resize(count);
        has_next = all_count > count;
    }

    if (candidates.empty()) {
        candidates.emplace_back(query, query, CandidateType::Placeholder);
    }

    if (cache_eligible) {
        CacheEntry entry;
        entry.candidates = candidates;
        entry.has_next = has_next;
        cache_put(key, entry);
    }

    for (const auto& c : candidates) {
        result.candidates.push_back(apply_user_vars_if_needed(c));
    }
    result.has_next = has_next;
    return result;
}

void DictManager::remember_dynamic_frequency(const std::string& query,
                                             const Candidate& candidate) {
    if (!should_apply_dynamic_frequency(query) || candidate.type == CandidateType::Placeholder) {
        return;
    }
    config_.dynamic_frequency_memory[query] = candidate.text;
}

void DictManager::set_candidate_to_first(const std::string& query, const Candidate& candidate) {
    Candidate new_candidate(query, candidate.text, CandidateType::User);
    prepend_candidate(new_candidate);
}

bool DictManager::prepend_candidate(const Candidate& candidate) {
    const char* sql =
        "insert into wb_py_dict(id, wbcode, text, type, query) "
        "values ((select MIN(id) - 1 from wb_py_dict), :code, :text, :type, :code);";
    sqlite3_stmt* stmt = nullptr;
    bool ok = false;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string type_str = to_string(CandidateType::User);
        sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":code"),
                          candidate.code.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":text"),
                          candidate.text.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":type"), type_str.c_str(), -1,
                          SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            ok = true;
            clear_query_cache();
        }
    }
    sqlite3_finalize(stmt);
    return ok;
}

void DictManager::prepend_candidates(const std::vector<Candidate>& candidates) {
    if (candidates.empty()) return;
    int min_id = min_id_from_dict_table();
    int n = static_cast<int>(candidates.size());

    // 参数化 INSERT，避免 code/text 中的单引号破坏 SQL（同时防注入）。
    const char* sql =
        "insert into wb_py_dict(id, wbcode, text, type, query) "
        "values (:id, :code, :text, :type, :code);";

    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return;
    }
    bool ok = true;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        for (int i = 0; i < n && ok; ++i) {
            std::string type_str = to_string(candidates[i].type);
            sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":id"), min_id - n + i);
            sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":code"),
                              candidates[i].code.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":text"),
                              candidates[i].text.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":type"), type_str.c_str(),
                              -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) != SQLITE_DONE) ok = false;
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
        }
    } else {
        ok = false;
    }
    sqlite3_finalize(stmt);
    sqlite3_exec(db_, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
    clear_query_cache();
}

void DictManager::update_user_dict(const std::string& dict_content) {
    std::string del = "delete from wb_py_dict where type = '" + to_string(CandidateType::User) + "'";
    sqlite3_exec(db_, del.c_str(), nullptr, nullptr, nullptr);
    clear_query_cache();

    std::vector<Candidate> candidates;
    std::istringstream iss(dict_content);
    std::string line;
    while (std::getline(iss, line)) {
        std::istringstream ls(line);
        std::vector<std::string> strs;
        std::string tok;
        while (ls >> tok) strs.push_back(tok);
        if (strs.size() <= 1) continue;
        const std::string& code = strs[0];
        for (size_t i = 1; i < strs.size(); ++i) {
            candidates.emplace_back(code, strs[i], CandidateType::User);
        }
    }
    prepend_candidates(candidates);
}

std::vector<Candidate> DictManager::get_user_candidates() {
    std::vector<Candidate> candidates;
    std::string sql =
        "select query, text from wb_py_dict where type = '" + to_string(CandidateType::User) + "'";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string code = col_text(stmt, 0);
            std::string text = col_text(stmt, 1);
            candidates.emplace_back(code, text, CandidateType::User);
        }
    }
    sqlite3_finalize(stmt);
    return candidates;
}

std::string DictManager::get_user_dict_content() {
    struct Line {
        std::string code;
        std::vector<std::string> texts;
    };
    std::vector<Line> list;
    for (const auto& c : get_user_candidates()) {
        int idx = -1;
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].code == c.code) { idx = static_cast<int>(i); break; }
        }
        if (idx < 0) {
            list.push_back({c.code, {c.text}});
        } else {
            auto& texts = list[idx].texts;
            if (std::find(texts.begin(), texts.end(), c.text) == texts.end()) {
                texts.push_back(c.text);
            }
        }
    }
    std::ostringstream oss;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i > 0) oss << "\n";
        oss << list[i].code;
        for (const auto& t : list[i].texts) oss << " " << t;
    }
    return oss.str();
}

}  // namespace fire
