/*
 * WinFire 项目级 SQLite 编译选项（单点真相源）
 * ===========================================================================
 *
 * 目的：通过 SQLITE_OMIT_* 等编译期宏裁剪 SQLite amalgamation 中本项目用不到
 * 的特性代码，缩小编译产物（fire_dictd.exe / fire_config.exe / tablebuilder.exe）
 * 体积。dictd 查询热路径（GLOB 前缀走 query_index b-tree）与被裁特性无关，性能持平。
 *
 * 本文件由 fire_sqlite3_amalg.c 在 #include "sqlite3.c" 之前引入，因此作用于
 * 整个 amalgamation 编译单元。三处编译入口（CMakeLists.txt 的 fire_sqlite3、
 * fire_dictd.vcxproj、fire_config.vcxproj）都经 wrapper 统一引用，避免宏散落。
 *
 * 每条宏下方的「依据」注明了为何裁掉它是安全的（对照 core/ dictd/ tablebuilder/
 * config/ 实际使用的 SQL 与 C API 反推）。升级 sqlite3.c 后请复核。
 *
 * 【已验证不可用 / 故意未启用】（3.49.1 amalgamation 上实测会破坏编译或收益过小）：
 *   - SQLITE_OMIT_VIRTUALTABLE：会级联 SQLITE_OMIT_ALTERTABLE，而 3.49.1 中
 *     sqlite3AlterRenameColumn 的前向声明（sqlite3.c:21664）与解析器调用点
 *     （:179166）均未做 ALTERTABLE 门控，仅定义（:117517）在门控内 → 定义消失
 *     声明保留，触发 C2129。待 amalgamation 修复前不可用。
 *   - SQLITE_OMIT_ATTACH：3.49.1 前向声明 sqlite3DbIsNamed（sqlite3.c:21509）未做
 *     ATTACH 门控，而其定义（:121316）在 #ifndef SQLITE_OMIT_ATTACH 内 → 定义消失
 *     声明保留，触发 C2129。待 amalgamation 修复前不可用。
 *   - SQLITE_OMIT_DECLTYPE：会连带误伤 sqlite3DbIsNamed 的内部使用，编译失败。
 *   - SQLITE_DQS：保守不设，避免双引号字符串的兼容性微妙风险。
 *
 * 【不能裁 / 已保留】（代码在用，无 OMIT 或会编译失败）：
 *   GLOB、date()/datetime('now')、ifnull()、length()、聚合(min/sum/count)、
 *   autoincrement+sqlite_sequence、vacuum、insert or ignore、CREATE TABLE IF NOT EXISTS、
 *   inner join、子查询，以及 PRAGMA user_version / journal_mode=WAL /
 *   synchronous / case_sensitive_like / mmap_size —— 均非 OMIT 可选项，保留。
 */
#ifndef FIRE_SQLITE_COMPILE_OPTIONS_H
#define FIRE_SQLITE_COMPILE_OPTIONS_H

/* --------------------------------------------------------------------------
 * 线程模型
 *
 *   =2 (multi-thread)：关闭 SQLite 内部「全局/跨连接」mutex，但单个 sqlite3*
 *   句柄仍可在「被外部串行化」的前提下由多线程交替使用。
 *
 *   取 =2 而非 =0（single-thread）的原因：fire_dictd.exe 是多线程进程
 *   （每个命名管道连接一个线程，main.cpp:100-110），而 DictManager::db_ 是
 *   全进程唯一的 sqlite3* 句柄，会被不同连接线程交替调用。虽然 DictServer::mu_
 *   （DictServer.cpp:249 的 lock_guard）已把所有 SQLite 访问串行化，但 =0 模式
 *   下 SQLite 不做任何线程隔离、存在线程局部状态风险；=2 在「外部串行化」前提下
 *   性能等同 =0 且零 UB。config.exe / tablebuilder.exe 单线程，=2 无副作用。
 * -------------------------------------------------------------------------- */
#define SQLITE_THREADSAFE 2

/* --------------------------------------------------------------------------
 * 默认运行时行为（不改功能，省代码/加速）
 * -------------------------------------------------------------------------- */
/* 关闭 sqlite3_memory_used()/highwater 等内存统计。项目无任何代码调用这些 API。 */
#define SQLITE_DEFAULT_MEMSTATUS 0

/* --------------------------------------------------------------------------
 * 功能特性 OMIT（项目代码完全不用，裁掉对应实现代码）
 *
 * 注意：部分 OMIT 在 3.49.1 amalgamation 上存在「前向声明未门控 / 定义被门控」的
 * 瑕疵会导致 C2129，见文末「已验证不可用」清单（VIRTUALTABLE / ATTACH / DECLTYPE）。
 * -------------------------------------------------------------------------- */
/* 无 json()/json_extract 等 JSON1 函数调用。 */
#define SQLITE_OMIT_JSON
/* 全 UTF-8：dict_manager/statistics/tablebuilder 均用 sqlite3_bind_text/_column_text
 * 与 *_v2 系列 API，无任何 UTF-16 入口（sqlite3_open16 / *_text16 / prepare16）。 */
#define SQLITE_OMIT_UTF16
/* 不使用 sqlite3_load_extension，无任何扩展加载（兼带安全收益：杜绝注入加载 .dll）。 */
#define SQLITE_OMIT_LOAD_EXTENSION
/* 不使用任何已废弃 API（sqlite3_global_recover 等）。 */
#define SQLITE_OMIT_DEPRECATED
/* 不使用 authorizer（sqlite3_set_authorizer）。 */
#define SQLITE_OMIT_AUTHORIZATION
/* 不使用进度/忙时回调（sqlite3_progress_handler / busy_handler 的进度路径）。 */
#define SQLITE_OMIT_PROGRESS_CALLBACK
/* 不使用 EXPLAIN（项目不发 "EXPLAIN ..." 语句）。 */
#define SQLITE_OMIT_EXPLAIN
/* 不调用 sqlite3_compileoption_get/used（编译选项自省 API）。 */
#define SQLITE_OMIT_COMPILEOPTION_DIAGS
/* 不调用 sqlite3_complete()（SQL 完整性判定函数）。 */
#define SQLITE_OMIT_COMPLETE

#endif /* FIRE_SQLITE_COMPILE_OPTIONS_H */
