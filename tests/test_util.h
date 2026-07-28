//
//  test_util.h — 极简测试框架 + 测试用词库构建 + FakeClient
//
#pragma once

#include <sqlite3.h>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "fire/config.h"
#include "fire/input_client.h"

// ---- 极简断言框架 ----
namespace testfx {
struct Case {
    std::string name;
    std::function<void()> fn;
};
inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}
inline int& failures() {
    static int f = 0;
    return f;
}
struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};
}  // namespace testfx

#define TEST_CASE(name)                                                    \
    static void name();                                                    \
    static testfx::Registrar reg_##name(#name, name);                      \
    static void name()

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("  [FAIL] %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            testfx::failures()++;                                          \
        }                                                                  \
    } while (0)

#define CHECK_EQ(a, b)                                                     \
    do {                                                                   \
        auto _va = (a);                                                    \
        auto _vb = (b);                                                    \
        if (!(_va == _vb)) {                                               \
            std::printf("  [FAIL] %s:%d  CHECK_EQ(%s == %s)\n", __FILE__, __LINE__, #a, #b); \
            testfx::failures()++;                                          \
        }                                                                  \
    } while (0)

#define CHECK_STR_EQ(a, b)                                                 \
    do {                                                                   \
        std::string _va = (a);                                             \
        std::string _vb = (b);                                             \
        if (_va != _vb) {                                                  \
            std::printf("  [FAIL] %s:%d  CHECK_STR_EQ: got \"%s\", want \"%s\"\n", \
                        __FILE__, __LINE__, _va.c_str(), _vb.c_str());     \
            testfx::failures()++;                                          \
        }                                                                  \
    } while (0)

// ---- 构建测试词库：与 build_wb_py_dict 结构一致的 wb_py_dict 表 ----
// entries: (wbcode, text, type, query)
inline void build_test_db(const std::string& path,
                          const std::vector<std::tuple<std::string, std::string, std::string,
                                                       std::string>>& entries) {
    std::remove(path.c_str());
    sqlite3* db = nullptr;
    sqlite3_open(path.c_str(), &db);
    sqlite3_exec(db, "PRAGMA case_sensitive_like=ON;", nullptr, nullptr, nullptr);
    sqlite3_exec(db,
                 "create table wb_py_dict (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                 "wbcode text not null, text text not null, type text not null, query text not null); "
                 "insert into sqlite_sequence(name, seq) values('wb_py_dict', 100000);",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(db, "create index if not exists query_index on wb_py_dict(query)", nullptr, nullptr,
                 nullptr);
    const char* sql = "insert into wb_py_dict(wbcode, text, type, query) values(?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    for (const auto& e : entries) {
        sqlite3_bind_text(stmt, 1, std::get<0>(e).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, std::get<1>(e).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, std::get<2>(e).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, std::get<3>(e).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

// ---- FakeClient：记录上屏/组字/候选窗调用，供状态机测试断言 ----
class FakeClient : public fire::InputClient {
public:
    std::string inserted;       // 累计上屏文本
    std::string last_insert;    // 最近一次上屏
    std::string marked;         // 当前组字区
    bool candidates_visible = false;
    fire::CandidatesView last_view;
    std::string previous_text;  // 供 get_previous_text 返回
    std::string app_id_value = "test.app";

    void insert_text(const std::string& utf8) override {
        inserted += utf8;
        last_insert = utf8;
    }
    void set_marked_text(const std::string& utf8) override { marked = utf8; }
    void clear_marked_text() override { marked.clear(); }
    fire::CaretRect get_caret_rect() override { return {}; }
    std::string get_previous_text() override { return previous_text; }
    std::string bundle_id() override { return app_id_value; }
    void show_candidates(const fire::CandidatesView& view) override {
        candidates_visible = true;
        last_view = view;
    }
    void hide_candidates() override { candidates_visible = false; }
};
