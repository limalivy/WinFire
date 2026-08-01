//
//  query_cache_store.h — DictManager 内存 LRU 缓存的单文件全量快照（极简方案）
//
//  目的：冷启动时把上次会话积累的 1-3 码首屏候选缓存快速恢复进内存，使前几次
//  查询在 sqlite 尚未热起来时也能命中。设计极简：
//    - 启动时 Load（校验指纹：db mtime + size + config 摘要，不符则当空）
//    - 退出时 Save（全量序列化，先写 .tmp 再 rename 原子替换）
//    - 数据库变更时 Remove（整体失效，由 DictManager::clear_query_cache 收口）
//  无写线程、无增量、无 tomb。强杀进程只是丢积累，无半写坏中间态。
//
//  文件格式（小端）：
//    magic(4B "WFCQ") | db_mtime(i64) | db_size(i64) | config_digest(u32)
//    | entry_count(u32) | entry × N
//    entry: key_len(u32) | key | has_next(u8) | cand_count(u32) | candidate × M
//    candidate: code(str) | text(str) | type(u8) | label(str)   （str = u32 len + bytes）
//
//  读取越界/校验不符 → 整体当空（返回 nullopt），不崩溃。
//
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "fire/candidate.h"

namespace fire {

// 一条缓存条目（与 DictManager::CacheEntry 对应，但展平为值类型便于序列化）。
struct CacheStoreEntry {
    std::string key;
    bool has_next = false;
    std::vector<Candidate> candidates;
};

// 加载结果：指纹 + 条目集合。
struct CacheStoreSnapshot {
    int64_t db_mtime = 0;
    int64_t db_size = 0;
    uint32_t config_digest = 0;
    std::vector<CacheStoreEntry> entries;
};

namespace query_cache_store {

// 计算配置摘要（FNV-1a over code_mode/candidate_count/enable_word_input 的字节拼接）。
// 不同配置（候选个数、码表方案、词组开关）下摘要不同，加载时据此丢弃过期快照。
uint32_t ConfigDigest(int code_mode, int candidate_count, bool enable_word_input);

// FNV-1a 64bit 哈希（字节序列）。供 dictd 把多维指纹（db mtime/size/config_digest/
// user_cache_generation）压成一个 u64 token，DLL 仅比较相等性，不解析其内部。
// 放在 query_cache_store（DLL + dictd 共享的跨层模块）便于双方复用同一实现。
uint64_t Fnv1a64(const uint8_t* data, size_t len);
inline uint64_t Fnv1a64(const std::vector<uint8_t>& data) {
    return Fnv1a64(data.data(), data.size());
}

// 从 path 加载快照。文件不存在 / magic 错 / 读取越界 / 解码失败 → 返回 nullopt。
// 注意：本函数不校验指纹（指纹校验由调用方对比 db 当前 mtime/size/digest 决定），
// 这里只负责「能否完整读出」。返回的 snapshot 含文件里记录的指纹供调用方比对。
std::optional<CacheStoreSnapshot> Load(const std::string& path);

// 全量序列化写文件：先写 path+".tmp" 再 rename 为 path（原子替换，防写一半）。
// 失败（磁盘满/权限）静默忽略——最坏情况下次冷启动从空开始，无损。
void Save(const std::string& path, const CacheStoreSnapshot& snapshot);

// 删除文件（整体失效）。文件不存在视为成功。
void Remove(const std::string& path);

}  // namespace query_cache_store
}  // namespace fire
