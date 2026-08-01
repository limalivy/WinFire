//
//  query_cache_store.cpp — LRU 缓存单文件全量快照（Load/Save/Remove/ConfigDigest）
//
#include "fire/query_cache_store.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>

namespace fire {
namespace query_cache_store {

namespace {

// 以二进制读全文件到 string。失败返回 false。
// 用 std::filesystem::path 构造路径——在 Windows 上 MSVC 的 path(std::string)
// 把字节按 UTF-8 解码，能正确处理非 ASCII 用户名路径，无需 windows.h。
bool read_all(const std::string& path, std::string& out) {
    std::ifstream ifs(std::filesystem::u8path(path), std::ios::binary);
    if (!ifs) return false;
    out.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    return true;
}

// 二进制写全量。成功返回 true。
bool write_all(const std::string& path, const std::string& data) {
    std::ofstream ofs(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(ofs);
}

// ---- 小端字节读写（裸实现，无外部依赖）----

void put_u32(std::string& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
}
void put_i64(std::string& out, int64_t v) {
    uint64_t u = static_cast<uint64_t>(v);
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((u >> (i * 8)) & 0xFF));
}
void put_str(std::string& out, const std::string& s) {
    put_u32(out, static_cast<uint32_t>(s.size()));
    out.append(s);
}

// 越界检查的读取游标。任一读取越界后 ok=false，后续读取全部返回 0/空。
struct Cursor {
    const char* p;
    size_t len;
    size_t pos = 0;
    bool ok = true;

    uint32_t u32() {
        if (!ok || pos + 4 > len) { ok = false; return 0; }
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<uint32_t>(static_cast<uint8_t>(p[pos + i])) << (i * 8);
        pos += 4;
        return v;
    }
    int64_t i64() {
        if (!ok || pos + 8 > len) { ok = false; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= static_cast<uint64_t>(static_cast<uint8_t>(p[pos + i])) << (i * 8);
        pos += 8;
        return static_cast<int64_t>(v);
    }
    std::string str() {
        uint32_t n = u32();
        if (!ok || pos + n > len) { ok = false; return {}; }
        std::string s(p + pos, n);
        pos += n;
        return s;
    }
};

uint8_t type_to_u8(CandidateType t) {
    switch (t) {
        case CandidateType::Wb:          return 0;
        case CandidateType::Py:          return 1;
        case CandidateType::User:        return 2;
        case CandidateType::Placeholder: return 3;
    }
    return 0;
}
bool u8_to_type(uint8_t v, CandidateType& out) {
    switch (v) {
        case 0: out = CandidateType::Wb;          return true;
        case 1: out = CandidateType::Py;          return true;
        case 2: out = CandidateType::User;        return true;
        case 3: out = CandidateType::Placeholder; return true;
        default: return false;  // 非法枚举值 → 解码失败
    }
}

// 把单个 candidate 追加进缓冲。
void put_candidate(std::string& out, const Candidate& c) {
    put_str(out, c.code);
    put_str(out, c.text);
    out.push_back(static_cast<char>(type_to_u8(c.type)));
    put_str(out, c.label);
}

// 从游标读一个 candidate。解码失败（越界/非法枚举）置 ok=false。
bool get_candidate(Cursor& cur, Candidate& out) {
    out.code = cur.str();
    out.text = cur.str();
    uint8_t t = 0;
    if (cur.ok && cur.pos < cur.len) {
        t = static_cast<uint8_t>(cur.p[cur.pos]);
        cur.pos += 1;
    } else {
        cur.ok = false;
    }
    out.label = cur.str();
    if (!cur.ok) return false;
    if (!u8_to_type(t, out.type)) { cur.ok = false; return false; }
    return true;
}

}  // namespace

uint32_t ConfigDigest(int code_mode, int candidate_count, bool enable_word_input) {
    // FNV-1a 32bit over 字节拼接。
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) {
        h ^= v;
        h *= 16777619u;
    };
    mix(static_cast<uint32_t>(code_mode));
    mix(static_cast<uint32_t>(candidate_count));
    mix(enable_word_input ? 1u : 0u);
    return h;
}

uint64_t Fnv1a64(const uint8_t* data, size_t len) {
    // FNV-1a 64bit：offset basis 0xcbf29ce484222325，prime 0x100000001b3。
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

std::optional<CacheStoreSnapshot> Load(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(path), ec)) return std::nullopt;

    // 一次性读全文件到内存（快照很小，1-3 码首屏条目最多几千条，几 MB 量级）。
    std::string buf;
    if (!read_all(path, buf)) return std::nullopt;

    if (buf.size() < 4 + 8 + 8 + 4 + 4) return std::nullopt;  // 头部都不够
    if (std::memcmp(buf.data(), "WFCQ", 4) != 0) return std::nullopt;

    Cursor cur{buf.data(), buf.size(), 4};  // 跳过 magic
    CacheStoreSnapshot snap;
    snap.db_mtime = cur.i64();
    snap.db_size = cur.i64();
    snap.config_digest = cur.u32();
    uint32_t entry_count = cur.u32();
    if (!cur.ok) return std::nullopt;

    snap.entries.reserve(entry_count < 20000 ? entry_count : 0);  // 防恶意巨大 count
    for (uint32_t i = 0; i < entry_count; ++i) {
        CacheStoreEntry e;
        e.key = cur.str();
        if (cur.ok && cur.pos < cur.len) {
            e.has_next = cur.p[cur.pos] != 0;
            cur.pos += 1;
        } else {
            cur.ok = false;
        }
        uint32_t cand_count = cur.u32();
        if (!cur.ok) return std::nullopt;
        e.candidates.reserve(cand_count < 256 ? cand_count : 0);  // 候选数合理上限
        for (uint32_t j = 0; j < cand_count; ++j) {
            Candidate c;
            if (!get_candidate(cur, c)) return std::nullopt;
            e.candidates.push_back(std::move(c));
        }
        snap.entries.push_back(std::move(e));
    }
    if (!cur.ok) return std::nullopt;
    // 允许尾部有少量多余字节（向前兼容新版本追加字段），不强制 at_end。
    return snap;
}

void Save(const std::string& path, const CacheStoreSnapshot& snap) {
    // 进程内串行化写盘：daemon 侧 SaveCacheAsync 的 detached 子线程可能与
    // ~DictManager 的 SaveCacheStore 并发写同一 path（两者都用 path+".tmp"）。
    // 不加锁会互相截断 .tmp、或一个 rename 时另一个正 remove 目标，留下半写文件
    // 或丢全部积累。文件缓存可容忍丢少量最新数据，但不可整体损坏，故此处加进程内锁。
    // 跨进程并发（多 fire_dictd 实例）由 daemon 单实例 mutex 保证不会出现，无需文件锁。
    static std::mutex save_mu;
    std::lock_guard<std::mutex> lock(save_mu);

    std::string buf;
    buf.append("WFCQ", 4);
    put_i64(buf, snap.db_mtime);
    put_i64(buf, snap.db_size);
    put_u32(buf, snap.config_digest);
    put_u32(buf, static_cast<uint32_t>(snap.entries.size()));
    for (const auto& e : snap.entries) {
        put_str(buf, e.key);
        buf.push_back(e.has_next ? '\x01' : '\x00');
        put_u32(buf, static_cast<uint32_t>(e.candidates.size()));
        for (const auto& c : e.candidates) put_candidate(buf, c);
    }

    // 先写 .tmp 再 rename：原子替换，防止写一半的中间态被下次 Load 读到。
    std::string tmp = path + ".tmp";
    if (!write_all(tmp, buf)) return;  // 写不开就算了，下次冷启动从空开始
    std::error_code ec;
    auto tmp_p = std::filesystem::u8path(tmp);
    auto dst_p = std::filesystem::u8path(path);
    // 直接 rename 覆盖：MSVC 的 std::filesystem::rename 在 Windows 上走
    // MoveFileExW(MOVEFILE_REPLACE_EXISTING)，目标存在即原子替换，无需先 remove。
    // 旧实现「先 exists+remove 再 rename」会在 remove 与 rename 之间出现「目标不存在」
    // 窗口；若此时进程被强杀，则 .tmp 与目标都不在 → 整份积累全丢。原子 rename
    // 保证任一时刻目标要么是旧完整文件、要么是新完整文件，最坏只丢这次未落盘的写入。
    std::filesystem::rename(tmp_p, dst_p, ec);
    if (ec) {
        // 极少数老平台 rename 覆盖失败：回退到 remove+rename，仍受本函数锁保护，
        // 不存在与并发 Save 撞 remove 的风险（同一路径的 Save 已被串行化）。
        if (std::filesystem::exists(dst_p, ec)) std::filesystem::remove(dst_p, ec);
        std::filesystem::rename(tmp_p, dst_p, ec);
        if (ec) std::filesystem::remove(tmp_p, ec);
    }
}

void Remove(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(path), ec);
    // 顺手清可能残留的 .tmp。
    std::filesystem::remove(std::filesystem::u8path(path + ".tmp"), ec);
}

}  // namespace query_cache_store
}  // namespace fire
