//
//  main.cpp — 词库构建工具，移植自 Fire/TableBuilder/main.cpp
//  码表 txt -> sqlite；并可合并五笔/拼音生成 wb_py_dict。
//
#include <sqlite3.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

static string dbPath;
static string tableName;
static string txtPath;
static sqlite3* db = nullptr;

static vector<string> split(const string& s) {
    vector<string> sv;
    string ss;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            ss += c;
        } else if (!ss.empty()) {
            sv.emplace_back(ss);
            ss.clear();
        }
    }
    if (!ss.empty()) sv.emplace_back(ss);
    return sv;
}

// 打开数据库；失败时返回非 0 退出码，供调用方（DictPage）感知构建失败。
static void open_database() {
    int err = sqlite3_open(dbPath.c_str(), &db);
    if (err) {
        cerr << "Can't open database: " << sqlite3_errmsg(db) << endl;
        exit(1);
    }
}

static bool table_exists(sqlite3* db, const string& name) {
    sqlite3_stmt* st = nullptr;
    bool exists = false;
    if (sqlite3_prepare_v2(db,
                           "select 1 from sqlite_master where type='table' and name=?", -1,
                           &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        exists = (sqlite3_step(st) == SQLITE_ROW);
        sqlite3_finalize(st);
    }
    return exists;
}

static void create_table(sqlite3* db, const string& tableName) {
    // 仅在表不存在时创建并初始化 sqlite_sequence，避免重复运行时插入重复 seq 行。
    bool existed = table_exists(db, tableName);
    string sql = "create table if not exists " + tableName +
                 "(id integer primary key autoincrement not null, "
                 "code text not null, text text not null)";
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        cout << "dict table created failure: " << sqlite3_errmsg(db) << endl;
        exit(2);
    }
    if (!existed) {
        // 新建表：把自增起点抬到 100000（与原实现一致）。
        sql = "insert into sqlite_sequence(name, seq) values('" + tableName + "', 100000)";
        sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    }
    cout << "dict table created successfully" << endl;

    sql = "create index if not exists " + tableName + "_code_index on " + tableName + "(code)";
    rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        cout << "dict table index created failure: " << sqlite3_errmsg(db) << endl;
        exit(2);
    }
    cout << "dict table index created successfully" << endl;
}

static void build_wb_py_dict() {
    bool existed = table_exists(db, "wb_py_dict");
    // 加 if not exists，允许重复运行不因表已存在直接失败。
    string createTable =
        "create table if not exists wb_py_dict ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, wbcode text not null, "
        "text text not null, type text not null, query text not null)";
    int rc = sqlite3_exec(db, createTable.c_str(), nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        cout << "dict wb_py_dict created failure: " << sqlite3_errmsg(db) << endl;
        exit(1);
    }
    if (!existed) {
        sqlite3_exec(db, "insert into sqlite_sequence(name, seq) values('wb_py_dict', 100000)",
                     nullptr, nullptr, nullptr);
    }
    const char* sql =
        "insert into wb_py_dict(wbcode, text, type, query) "
        "select code as wbcode, text, 'wb' as type, code as query from wb_dict; "
        "insert into wb_py_dict(wbcode, text, type, query) "
        "select wb.code as wbcode, py.text as text, 'py' as type, py.code as query "
        "from py_dict py inner join wb_dict wb on py.text = wb.text order by py.id;";
    rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        cout << "initilize wb_py_dict failure: " << sqlite3_errmsg(db) << endl;
        exit(1);
    }
    rc = sqlite3_exec(db, "create index if not exists query_index on wb_py_dict(query)", nullptr,
                      nullptr, nullptr);
    if (rc != SQLITE_OK) {
        cout << "create index fail: " << sqlite3_errmsg(db) << endl;
        exit(1);
    }
    cout << "wb_py_dict built successfully" << endl;
}

// 用参数化 stmt + 事务分批写入，避免超长 SQL 超限，且不篡改含单引号的词条内容。
static void insert_rows(sqlite3* db, const string& tableName,
                        const vector<vector<string>>& dict) {
    string insertSql = "insert into " + tableName + "(code, text) values(?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, insertSql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        cout << "prepare insert failure: " << sqlite3_errmsg(db) << endl;
        exit(1);
    }

    if (sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr) != SQLITE_OK) {
        cout << "begin transaction failure: " << sqlite3_errmsg(db) << endl;
        sqlite3_finalize(stmt);
        exit(1);
    }

    long long lineCount = 0;
    for (const auto& columns : dict) {
        if (columns.size() < 2) continue;  // 至少要有 code + 一个词条
        const string& code = columns[0];
        for (size_t i = 1; i < columns.size(); ++i) {
            sqlite3_bind_text(stmt, 1, code.c_str(), (int)code.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, columns[i].c_str(), (int)columns[i].size(),
                              SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                cout << "insert failure: " << sqlite3_errmsg(db) << endl;
                sqlite3_finalize(stmt);
                sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                exit(1);
            }
            sqlite3_reset(stmt);
            ++lineCount;
        }
    }

    sqlite3_finalize(stmt);
    if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
        cout << "commit failure: " << sqlite3_errmsg(db) << endl;
        exit(1);
    }
    cout << "line count:" << lineCount << endl;
}

int main(int argc, const char* argv[]) {
    if (argc <= 1) return 0;
    string cmd = argv[1];

    if (cmd == "--create-dict" && argc == 5) {
        txtPath = argv[2];
        tableName = argv[3];
        dbPath = argv[4];
        open_database();
    } else if (cmd == "--combine-dict" && argc == 3) {
        dbPath = argv[2];
        open_database();
        build_wb_py_dict();
        sqlite3_close(db);
        return 0;
    } else {
        cout << "usage:\n"
             << "  --create-dict <txt> <table> <db>\n"
             << "  --combine-dict <db>   (requires wb_dict / py_dict already built in <db>)\n";
        return 0;
    }

    ifstream infile(txtPath, ios::in);
    if (!infile.is_open()) {
        cerr << "Can't open code table file: " << txtPath << endl;
        sqlite3_close(db);
        exit(1);
    }

    vector<vector<string>> dict;
    string line;
    while (getline(infile, line)) dict.emplace_back(split(line));

    create_table(db, tableName);
    insert_rows(db, tableName, dict);

    sqlite3_close(db);
    return 0;
}
