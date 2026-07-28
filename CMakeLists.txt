#
# my_win_input — 业火五笔输入法 Windows 移植
#
# 跨平台内核（状态机 + 词库）用 CMake 构建，可在 macOS 上编译并运行 ctest 验证。
# Windows 层（TSF/ATL、Win32/GDI+ 候选窗、MFC 配置界面）不参与 CMake。
#
# 选项：
#   BUILD_CORE        构建 fire_core 静态库（默认 ON）
#   BUILD_TESTS       构建内核单元测试并注册 ctest（默认 ON）
#   BUILD_TABLEBUILDER 构建词库构建工具（默认 ON）
#

cmake_minimum_required(VERSION 3.16)
project(my_win_input LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_CORE "Build fire_core static library" ON)
option(BUILD_TESTS "Build core unit tests and register ctest" ON)
option(BUILD_TABLEBUILDER "Build wubi/pinyin table builder tool" ON)

# ---- 定位 sqlite3 ----
# macOS 上系统 SDK 提供 sqlite3.h 与 libsqlite3.tbd，直接链接 -lsqlite3 即可。
find_package(SQLite3 QUIET)

# ---- 内核静态库 ----
if(BUILD_CORE)
    add_library(fire_core STATIC
        core/src/types.cpp
        core/src/candidate.cpp
        core/src/key_event.cpp
        core/src/punctuation.cpp
        core/src/dict_manager.cpp
        core/src/input_engine.cpp
        core/src/input_mode_cache.cpp
        core/src/statistics.cpp
        core/src/utf8.cpp
    )
    target_include_directories(fire_core PUBLIC core/include)

    if(SQLite3_FOUND)
        target_link_libraries(fire_core PUBLIC SQLite::SQLite3)
    else()
        target_link_libraries(fire_core PUBLIC sqlite3)
    endif()
endif()

# ---- 词库构建工具 ----
if(BUILD_TABLEBUILDER)
    add_executable(tablebuilder tablebuilder/main.cpp)
    if(SQLite3_FOUND)
        target_link_libraries(tablebuilder PRIVATE SQLite::SQLite3)
    else()
        target_link_libraries(tablebuilder PRIVATE sqlite3)
    endif()
endif()

# ---- 内核单元测试 ----
if(BUILD_TESTS AND BUILD_CORE)
    enable_testing()
    add_executable(fire_tests
        tests/test_main.cpp
        tests/test_punctuation.cpp
        tests/test_dict.cpp
        tests/test_engine.cpp
        tests/test_input_mode_cache.cpp
        tests/test_statistics.cpp
    )
    target_include_directories(fire_tests PRIVATE tests)
    target_link_libraries(fire_tests PRIVATE fire_core)
    if(SQLite3_FOUND)
        target_link_libraries(fire_tests PRIVATE SQLite::SQLite3)
    else()
        target_link_libraries(fire_tests PRIVATE sqlite3)
    endif()

    add_test(NAME fire_tests COMMAND fire_tests)
endif()
