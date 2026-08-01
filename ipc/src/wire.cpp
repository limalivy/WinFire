//
//  wire.cpp — IPC payload 二进制读写实现
//
#include "fire/ipc/wire.h"

namespace fire {
namespace ipc {

// ---- Writer ----
void Writer::put_u8(uint8_t v) { buf_.push_back(v); }

void Writer::put_u16(uint16_t v) {
    buf_.push_back(static_cast<uint8_t>(v & 0xFF));
    buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void Writer::put_u32(uint32_t v) {
    buf_.push_back(static_cast<uint8_t>(v & 0xFF));
    buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf_.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf_.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void Writer::put_u64(uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        buf_.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
}

void Writer::put_i32(int32_t v) { put_u32(static_cast<uint32_t>(v)); }

void Writer::put_i64(int64_t v) {
    uint64_t u = static_cast<uint64_t>(v);
    for (int i = 0; i < 8; ++i) {
        buf_.push_back(static_cast<uint8_t>((u >> (i * 8)) & 0xFF));
    }
}

void Writer::put_string(const std::string& s) {
    put_u32(static_cast<uint32_t>(s.size()));
    buf_.insert(buf_.end(), s.begin(), s.end());
}

void Writer::put_candidate(const Candidate& c) {
    put_string(c.code);
    put_string(c.text);
    put_u8(static_cast<uint8_t>(c.type));
    put_string(c.label);
}

void Writer::put_query_result(const QueryResult& r) {
    put_u8(r.has_next ? 1 : 0);
    put_u32(static_cast<uint32_t>(r.candidates.size()));
    for (const auto& c : r.candidates) put_candidate(c);
}

void Writer::put_string_list(const std::vector<std::string>& list) {
    put_u32(static_cast<uint32_t>(list.size()));
    for (const auto& s : list) put_string(s);
}

void Writer::put_candidate_list(const std::vector<Candidate>& list) {
    put_u32(static_cast<uint32_t>(list.size()));
    for (const auto& c : list) put_candidate(c);
}

// ---- Reader ----
bool Reader::ensure(size_t n) {
    if (!ok_) return false;
    if (pos_ + n > len_) {
        ok_ = false;
        return false;
    }
    return true;
}

uint8_t Reader::get_u8() {
    if (!ensure(1)) return 0;
    return data_[pos_++];
}

uint16_t Reader::get_u16() {
    if (!ensure(2)) return 0;
    uint16_t v = static_cast<uint16_t>(data_[pos_]) |
                 (static_cast<uint16_t>(data_[pos_ + 1]) << 8);
    pos_ += 2;
    return v;
}

uint32_t Reader::get_u32() {
    if (!ensure(4)) return 0;
    uint32_t v = static_cast<uint32_t>(data_[pos_]) |
                 (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
                 (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
                 (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
    pos_ += 4;
    return v;
}

uint64_t Reader::get_u64() {
    if (!ensure(8)) return 0;
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
        u |= static_cast<uint64_t>(data_[pos_ + i]) << (i * 8);
    }
    pos_ += 8;
    return u;
}

int32_t Reader::get_i32() { return static_cast<int32_t>(get_u32()); }

int64_t Reader::get_i64() {
    if (!ensure(8)) return 0;
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
        u |= static_cast<uint64_t>(data_[pos_ + i]) << (i * 8);
    }
    pos_ += 8;
    return static_cast<int64_t>(u);
}

std::string Reader::get_string() {
    uint32_t n = get_u32();
    if (!ok_) return {};
    if (!ensure(n)) return {};
    std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
    pos_ += n;
    return s;
}

Candidate Reader::get_candidate() {
    Candidate c;
    c.code = get_string();
    c.text = get_string();
    uint8_t t = get_u8();
    // 校验枚举范围（Wb=0..Placeholder=3），越界回退 Wb 且不视作解析失败以外的错误。
    if (t > static_cast<uint8_t>(CandidateType::Placeholder)) {
        ok_ = false;
        c.type = CandidateType::Wb;
    } else {
        c.type = static_cast<CandidateType>(t);
    }
    c.label = get_string();
    if (c.label.empty()) c.label = c.text;
    return c;
}

QueryResult Reader::get_query_result() {
    QueryResult r;
    r.has_next = get_u8() != 0;
    uint32_t n = get_u32();
    if (!ok_) return {};
    r.candidates.reserve(n < 4096 ? n : 0);  // 防止恶意大 count 预分配爆内存
    for (uint32_t i = 0; i < n && ok_; ++i) {
        r.candidates.push_back(get_candidate());
    }
    return r;
}

std::vector<std::string> Reader::get_string_list() {
    std::vector<std::string> out;
    uint32_t n = get_u32();
    if (!ok_) return {};
    out.reserve(n < 4096 ? n : 0);
    for (uint32_t i = 0; i < n && ok_; ++i) out.push_back(get_string());
    return out;
}

std::vector<Candidate> Reader::get_candidate_list() {
    std::vector<Candidate> out;
    uint32_t n = get_u32();
    if (!ok_) return {};
    out.reserve(n < 4096 ? n : 0);
    for (uint32_t i = 0; i < n && ok_; ++i) out.push_back(get_candidate());
    return out;
}

}  // namespace ipc
}  // namespace fire
