//
//  test_query_cache.cpp — query_cache_store 往返 / 失效 / 容错测试
//
#include "fire/query_cache_store.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "fire/candidate.h"
#include "test_util.h"

namespace {
// 取一个临时文件路径（基于进程启动时刻 + 计数，避免多测试并行冲突）。
// 不依赖平台 getpid/GetCurrentProcessId，保持跨平台可编译。
std::string tmp_path(int n) {
    static const auto boot = std::chrono::steady_clock::now().time_since_epoch().count();
    char buf[256];
    std::snprintf(buf, sizeof(buf), "wf_qcache_test_%lld_%d.bin",
                  static_cast<long long>(boot), n);
    return buf;
}

fire::CacheStoreSnapshot make_snapshot(int64_t mtime, int64_t size, uint32_t digest) {
    fire::CacheStoreSnapshot snap;
    snap.db_mtime = mtime;
    snap.db_size = size;
    snap.config_digest = digest;
    fire::CacheStoreEntry e;
    e.key = "a|0|5|true";
    e.has_next = true;
    e.candidates.push_back(fire::Candidate("a", "工", fire::CandidateType::Wb));
    e.candidates.push_back(fire::Candidate("a", "aa", fire::CandidateType::User, "显示"));
    snap.entries.push_back(e);

    fire::CacheStoreEntry e2;
    e2.key = "fg|0|5|false";
    e2.has_next = false;
    e2.candidates.push_back(fire::Candidate("fg", "于", fire::CandidateType::Wb));
    snap.entries.push_back(std::move(e2));
    return snap;
}
}  // namespace

TEST_CASE(qcache_save_load_roundtrip) {
    std::string p = tmp_path(1);
    fire::query_cache_store::Remove(p);
    auto snap = make_snapshot(100, 4096, 0xABCDEF01u);
    fire::query_cache_store::Save(p, snap);

    auto loaded = fire::query_cache_store::Load(p);
    CHECK(loaded.has_value());
    CHECK_EQ(loaded->db_mtime, 100);
    CHECK_EQ(loaded->db_size, 4096);
    CHECK_EQ(loaded->config_digest, 0xABCDEF01u);
    CHECK_EQ(loaded->entries.size(), (size_t)2);

    CHECK_STR_EQ(loaded->entries[0].key, "a|0|5|true");
    CHECK(loaded->entries[0].has_next == true);
    CHECK_EQ(loaded->entries[0].candidates.size(), (size_t)2);
    CHECK_STR_EQ(loaded->entries[0].candidates[0].text, "工");
    CHECK(loaded->entries[0].candidates[0].type == fire::CandidateType::Wb);
    CHECK_STR_EQ(loaded->entries[0].candidates[1].label, "显示");
    CHECK(loaded->entries[0].candidates[1].type == fire::CandidateType::User);

    CHECK_STR_EQ(loaded->entries[1].key, "fg|0|5|false");
    CHECK(loaded->entries[1].has_next == false);

    fire::query_cache_store::Remove(p);
}

// 回归：entries 写入顺序（MRU→LRU，front→back）必须在 Save/Load 后保持不变。
// DictManager::LoadCacheStore 据此把 entries[0] 当作 MRU 装回 cache_lru_，
// 顺序一旦被反转（旧 bug：用 push_front 装），淘汰 victim 会错指最新条目。
TEST_CASE(qcache_save_load_preserves_order) {
    std::string p = tmp_path(7);
    fire::query_cache_store::Remove(p);
    fire::CacheStoreSnapshot snap;
    snap.db_mtime = 1; snap.db_size = 1; snap.config_digest = 1u;
    // 写入顺序即期望的 LRU 新→旧顺序：k0(MRU) ... k4(LRU)。
    for (int i = 0; i < 5; ++i) {
        fire::CacheStoreEntry e;
        e.key = "k" + std::to_string(i);
        e.has_next = false;
        e.candidates.push_back(fire::Candidate("c", "t", fire::CandidateType::Wb));
        snap.entries.push_back(std::move(e));
    }
    fire::query_cache_store::Save(p, snap);
    auto loaded = fire::query_cache_store::Load(p);
    CHECK(loaded.has_value());
    CHECK_EQ(loaded->entries.size(), (size_t)5);
    // 顺序必须与写入一致：entries[i].key == "k<i>"。
    for (int i = 0; i < 5; ++i) {
        std::string want = "k" + std::to_string(i);
        CHECK_STR_EQ(loaded->entries[(size_t)i].key, want);
    }
    fire::query_cache_store::Remove(p);
}

// 回归：并发 Save（模拟 detached SaveCacheAsync 线程与 ~DictManager::SaveCacheStore
// 同时写同一 path）不得互相破坏文件。落盘后 Load 必须能读出一个完整快照。
// 旧实现两线程共用 path+".tmp" 且各自 exists+remove+rename，会留下半写/空文件。
TEST_CASE(qcache_concurrent_save_keeps_file_valid) {
    std::string p = tmp_path(8);
    fire::query_cache_store::Remove(p);

    auto snap1 = make_snapshot(11, 4096, 0x11111111u);
    auto snap2 = make_snapshot(22, 8192, 0x22222222u);

    std::vector<std::thread> ts;
    ts.emplace_back([&] { fire::query_cache_store::Save(p, snap1); });
    ts.emplace_back([&] { fire::query_cache_store::Save(p, snap2); });
    for (auto& t : ts) t.join();

    // 无论谁后写完，文件都必须是两份完整快照之一（不是半写/不是空）。
    auto loaded = fire::query_cache_store::Load(p);
    CHECK(loaded.has_value());
    CHECK_EQ(loaded->entries.size(), (size_t)2);
    bool isSnap1 = (loaded->db_mtime == 11 && loaded->db_size == 4096 &&
                    loaded->config_digest == 0x11111111u);
    bool isSnap2 = (loaded->db_mtime == 22 && loaded->db_size == 8192 &&
                    loaded->config_digest == 0x22222222u);
    CHECK(isSnap1 || isSnap2);
    fire::query_cache_store::Remove(p);
}

TEST_CASE(qcache_load_missing_returns_nullopt) {
    std::string p = tmp_path(2);
    fire::query_cache_store::Remove(p);
    auto loaded = fire::query_cache_store::Load(p);
    CHECK(!loaded.has_value());
}

TEST_CASE(qcache_load_bad_magic_returns_nullopt) {
    std::string p = tmp_path(3);
    fire::query_cache_store::Remove(p);
    // 写错误的 magic。
    FILE* fp = std::fopen(p.c_str(), "wb");
    CHECK(fp != nullptr);
    std::fwrite("XXXXgarbage", 1, 11, fp);
    std::fclose(fp);
    auto loaded = fire::query_cache_store::Load(p);
    CHECK(!loaded.has_value());
    fire::query_cache_store::Remove(p);
}

TEST_CASE(qcache_load_truncated_returns_nullopt) {
    std::string p = tmp_path(4);
    fire::query_cache_store::Remove(p);
    fire::query_cache_store::Save(p, make_snapshot(50, 8192, 0x1234u));
    // 把文件截断一半。
    {
        std::error_code ec;
        auto sz = std::filesystem::file_size(p, ec);
        if (!ec && sz > 4) {
            std::filesystem::resize_file(p, sz / 2, ec);
        }
    }
    auto loaded = fire::query_cache_store::Load(p);
    CHECK(!loaded.has_value());  // 截断后越界 → nullopt
    fire::query_cache_store::Remove(p);
}

TEST_CASE(qcache_config_digest_changes_with_inputs) {
    uint32_t d1 = fire::query_cache_store::ConfigDigest(0, 5, true);
    uint32_t d2 = fire::query_cache_store::ConfigDigest(1, 5, true);  // code_mode 变
    uint32_t d3 = fire::query_cache_store::ConfigDigest(0, 7, true);  // candidate_count 变
    uint32_t d4 = fire::query_cache_store::ConfigDigest(0, 5, false); // enable_word_input 变
    uint32_t d5 = fire::query_cache_store::ConfigDigest(0, 5, true);  // 同 d1
    CHECK(d1 != d2);
    CHECK(d1 != d3);
    CHECK(d1 != d4);
    CHECK(d1 == d5);  // 相同输入 → 相同摘要
}

TEST_CASE(qcache_remove_idempotent) {
    std::string p = tmp_path(5);
    fire::query_cache_store::Remove(p);  // 不存在
    fire::query_cache_store::Save(p, make_snapshot(1, 1, 1u));
    fire::query_cache_store::Remove(p);  // 删除
    fire::query_cache_store::Remove(p);  // 再删不报错
    auto loaded = fire::query_cache_store::Load(p);
    CHECK(!loaded.has_value());
}

TEST_CASE(qcache_empty_entries_save_load) {
    std::string p = tmp_path(6);
    fire::query_cache_store::Remove(p);
    fire::CacheStoreSnapshot snap;
    snap.db_mtime = 9;
    snap.db_size = 99;
    snap.config_digest = 0x0u;  // 空 entries
    fire::query_cache_store::Save(p, snap);
    auto loaded = fire::query_cache_store::Load(p);
    CHECK(loaded.has_value());
    CHECK_EQ(loaded->entries.size(), (size_t)0);
    fire::query_cache_store::Remove(p);
}
