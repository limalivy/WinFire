/*
 * WinFire 项目级 SQLite 编译入口（wrapper）
 *
 * 先注入编译期裁剪选项，再编译 sqlite3.c amalgamation。所有 OMIT/编译宏集中
 * 在 fire_sqlite_compile_options.h 单点维护；这样 sqlite3.c / sqlite3.h 原文件
 * 保持官方原样，便于将来升级 amalgamation。三处编译入口（CMakeLists.txt 的
 * fire_sqlite3、fire_dictd.vcxproj、fire_config.vcxproj）均改引用本文件。
 */
#include "fire_sqlite_compile_options.h"
#include "sqlite3.c"
