//
//  test_query_cache.cpp — query_cache_store 往返 / 失效 / 容错测试
//
#include "fire/query_cache_store.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "fire/candidate.h"
#include "test_util.h"

namespace {
// 取一个临时文件路径（基于当前进程 + 计数，避免冲突）。
std::string tmp_path(int n) {
    char buf[256];
#ifdef _WIN32
    std::snprintf(buf, sizeof(buf), "wf_qcache_test_%lu_%d.bin",
                  (unsigned long)GetCurrentProcessId(), n);
#else
    std::snprintf(buf, sizeof(buf), "wf_qcache_test_%d_%d.bin", (int)getpid(), n);
#endif
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
