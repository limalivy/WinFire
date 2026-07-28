//
//  statistics.cpp — 对应 Fire/Utils/Statistics.swift（去 SQLCipher、同步写入）
//
#include "fire/statistics.h"

#include <sqlite3.h>

#include <cstdint>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

namespace fire {

namespace {

// 建表/迁移语句序列，PRAGMA user_version 记录已应用到第几步
const char* kUpgrade[] = {
    R"SQL(CREATE TABLE IF NOT EXISTS "data" (
        "id" INTEGER PRIMARY KEY NOT NULL,
        "text" TEXT NOT NULL,
        "type" TEXT NOT NULL,
        "code" TEXT NOT NULL,
        "appId" TEXT NOT NULL DEFAULT '',
        "createdAt" TEXT NOT NULL DEFAULT (datetime('now'))
    ))SQL",
    R"SQL(CREATE TABLE IF NOT EXISTS "meta" (
        "key" TEXT PRIMARY KEY NOT NULL,
        "value" TEXT NOT NULL
    ))SQL",
    R"SQL(CREATE TABLE IF NOT EXISTS "hanzi_freq" (
        "hanzi" TEXT PRIMARY KEY NOT NULL,
        "count" INTEGER NOT NULL DEFAULT 0
    ))SQL",
    R"SQL(CREATE TABLE IF NOT EXISTS "word_freq" (
        "appId" TEXT NOT NULL,
        "word" TEXT NOT NULL,
        "count" INTEGER NOT NULL DEFAULT 0,
        PRIMARY KEY ("appId", "word")
    ))SQL",
};

std::string now_iso8601_ms() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.000", &tmv);
    return buf;
}

std::string escape_csv(const std::string& s) {
    bool need = s.find_first_of(",\"\n\r") != std::string::npos;
    if (!need) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

}  // namespace

Statistics::Statistics(const std::string& db_path) { init_db(db_path); }

Statistics::~Statistics() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Statistics::init_db(const std::string& db_path) {
    if (sqlite3_open_v2(db_path.c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK) {
        migrate();
    } else {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }
}

int32_t Statistics::get_version() {
    sqlite3_stmt* stmt = nullptr;
    int32_t v = 0;
    if (sqlite3_prepare_v2(db_, "PRAGMA user_version", -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
        v = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return v;
}

bool Statistics::set_version(int32_t version) {
    std::string sql = "PRAGMA user_version = " + std::to_string(version);
    return sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool Statistics::migrate() {
    const int count = (int)(sizeof(kUpgrade) / sizeof(kUpgrade[0]));
    int cur = get_version();
    if (cur >= count) return true;
    for (int i = 0; i < count; ++i) {
        sqlite3_exec(db_, kUpgrade[i], nullptr, nullptr, nullptr);
    }
    return set_version(count);
}

std::string Statistics::normalize_app_id(const std::string& raw) {
    return raw.empty() ? std::string("(unknown)") : raw;
}

bool Statistics::is_chinese_word(const std::string& text) {
    // 解析 UTF-8，判断是否含 CJK 汉字
    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t cp = 0;
        int len = 1;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; len = 4; }
        else { i += 1; continue; }
        for (int k = 1; k < len && i + k < text.size(); ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        }
        i += len;
        if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
            (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x20000 && cp <= 0x2EBEF)) {
            return true;
        }
    }
    return false;
}

int64_t Statistics::insert_data_row(const Candidate& candidate, const std::string& app_id) {
    const char* sql =
        "insert into data(text, type, code, appId, createdAt) "
        "values (:text, :type, :code, :appId, :createdAt)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return 0;
    }
    std::string type = to_string(candidate.type);
    std::string app = normalize_app_id(app_id);
    std::string created = now_iso8601_ms();
    sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":text"),
                      candidate.text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":type"),
                      type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":code"),
                      candidate.code.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":appId"),
                      app.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":createdAt"),
                      created.c_str(), -1, SQLITE_TRANSIENT);
    int64_t rowid = 0;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        rowid = sqlite3_last_insert_rowid(db_);
    }
    sqlite3_finalize(stmt);
    return rowid;
}

bool Statistics::update_word_frequency(const std::string& word, const std::string& app_id) {
    if (word.empty() || !is_chinese_word(word)) return false;
    std::string app = normalize_app_id(app_id);

    sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);
    bool ok = true;
    sqlite3_stmt* ins = nullptr;
    sqlite3_stmt* upd = nullptr;
    const char* insSQL = "insert or ignore into word_freq(appId, word, count) values(:appId, :word, 0)";
    const char* updSQL = "update word_freq set count = count + 1 where appId = :appId and word = :word";
    if (sqlite3_prepare_v2(db_, insSQL, -1, &ins, nullptr) == SQLITE_OK &&
        sqlite3_prepare_v2(db_, updSQL, -1, &upd, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(ins, sqlite3_bind_parameter_index(ins, ":appId"), app.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, sqlite3_bind_parameter_index(ins, ":word"), word.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(ins);
        sqlite3_bind_text(upd, sqlite3_bind_parameter_index(upd, ":appId"), app.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, sqlite3_bind_parameter_index(upd, ":word"), word.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(upd);
    } else {
        ok = false;
    }
    sqlite3_finalize(ins);
    sqlite3_finalize(upd);
    sqlite3_exec(db_, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok;
}

void Statistics::record_candidate(const Candidate& candidate,
                                  const std::string& app_id,
                                  const std::vector<std::string>& hanzi_parts,
                                  bool enable_stats,
                                  bool enable_hanzi) {
    if (!db_) return;
    if (candidate.type == CandidateType::Placeholder) return;

    if (enable_stats) {
        insert_data_row(candidate, app_id);
    }
    if (enable_hanzi) {
        if (!hanzi_parts.empty()) {
            for (const auto& w : hanzi_parts) {
                update_word_frequency(w, app_id);
            }
        } else {
            update_word_frequency(candidate.text, app_id);
        }
    }
}

int64_t Statistics::query_total_count() {
    if (!db_) return 0;
    sqlite3_stmt* stmt = nullptr;
    int64_t total = 0;
    if (sqlite3_prepare_v2(db_, "select ifnull(sum(length(text)), 0) from data", -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
        total = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return total;
}

std::vector<DateCount> Statistics::query_count_by_date(const std::string& start_date,
                                                       const std::string& end_date) {
    std::vector<DateCount> results;
    if (!db_) return results;
    const char* sql =
        "select date(createdAt) as d, sum(length(text)) as c from data "
        "where date(createdAt) >= :start and date(createdAt) <= :end "
        "group by date(createdAt) order by d desc";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return results;
    }
    sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":start"), start_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":end"), end_date.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DateCount dc;
        const unsigned char* d = sqlite3_column_text(stmt, 0);
        dc.date = d ? reinterpret_cast<const char*>(d) : "";
        dc.count = sqlite3_column_int64(stmt, 1);
        results.push_back(dc);
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<WordCount> Statistics::query_hanzi_frequency(int limit, const std::string& app_id) {
    std::vector<WordCount> res;
    if (!db_) return res;
    std::string sql;
    bool has_app = !app_id.empty();
    if (has_app) {
        sql = "select word, count from word_freq where appId = :appId order by count desc, word asc";
    } else {
        sql = "select word, sum(count) as count from word_freq group by word order by count desc, word asc";
    }
    if (limit > 0) sql += " limit " + std::to_string(limit);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return res;
    }
    if (has_app) {
        sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":appId"), app_id.c_str(), -1, SQLITE_TRANSIENT);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        WordCount wc;
        const unsigned char* w = sqlite3_column_text(stmt, 0);
        wc.word = w ? reinterpret_cast<const char*>(w) : "";
        wc.count = sqlite3_column_int64(stmt, 1);
        res.push_back(wc);
    }
    sqlite3_finalize(stmt);
    return res;
}

int64_t Statistics::query_hanzi_frequency_unique_count(const std::string& app_id) {
    if (!db_) return 0;
    std::string sql;
    bool has_app = !app_id.empty();
    if (has_app) {
        sql = "select count(*) from word_freq where appId = :appId";
    } else {
        sql = "select count(distinct word) from word_freq";
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return 0;
    }
    if (has_app) {
        sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":appId"), app_id.c_str(), -1, SQLITE_TRANSIENT);
    }
    int64_t c = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) c = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return c;
}

std::vector<std::string> Statistics::query_word_frequency_app_ids() {
    std::vector<std::string> res;
    if (!db_) return res;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "select distinct appId from word_freq order by appId asc", -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return res;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* a = sqlite3_column_text(stmt, 0);
        res.emplace_back(a ? reinterpret_cast<const char*>(a) : "");
    }
    sqlite3_finalize(stmt);
    return res;
}

void Statistics::clear() {
    if (!db_) return;
    sqlite3_exec(db_, "delete from data", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "delete from hanzi_freq", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "delete from word_freq", nullptr, nullptr, nullptr);
}

void Statistics::clear_hanzi_frequency() {
    if (!db_) return;
    sqlite3_exec(db_, "delete from word_freq", nullptr, nullptr, nullptr);
}

bool Statistics::export_hanzi_frequency_csv(const std::string& path) {
    if (!db_) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    // UTF-8 BOM，方便 Excel 识别
    f << "\xEF\xBB\xBF";
    f << "应用ID,词,次数\n";
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "select appId, word, count from word_freq order by appId asc, count desc, word asc";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* a = sqlite3_column_text(stmt, 0);
        const unsigned char* w = sqlite3_column_text(stmt, 1);
        int64_t c = sqlite3_column_int64(stmt, 2);
        std::string app = a ? reinterpret_cast<const char*>(a) : "";
        std::string word = w ? reinterpret_cast<const char*>(w) : "";
        f << escape_csv(app) << "," << escape_csv(word) << "," << c << "\n";
    }
    sqlite3_finalize(stmt);
    return true;
}

}  // namespace fire
