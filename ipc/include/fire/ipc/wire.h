//
//  wire.h — IPC payload 二进制读写缓冲（跨平台，纯 C++，零依赖）
//
//  编码规则（小端）：
//    - 整数：小端定长（u8/u16/u32/i32/i64）
//    - 字符串：u32 长度前缀 + UTF-8 字节（不含 NUL）
//    - 数组：u32 count + count × 元素
//    - Candidate：str code + str text + u8 type + str label
//    - QueryResult：u8 has_next + u32 count + count × Candidate
//
//  Reader 所有读取均带越界检查：任一读取越界后进入 error 状态，
//  后续读取全部失败，ok() 返回 false（不抛异常、不崩溃）。
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fire/candidate.h"
#include "fire/dict_manager.h"  // QueryResult

namespace fire {
namespace ipc {

class Writer {
public:
    void put_u8(uint8_t v);
    void put_u16(uint16_t v);
    void put_u32(uint32_t v);
    void put_u64(uint64_t v);
    void put_i32(int32_t v);
    void put_i64(int64_t v);
    void put_string(const std::string& s);
    void put_candidate(const Candidate& c);
    void put_query_result(const QueryResult& r);
    void put_string_list(const std::vector<std::string>& list);
    void put_candidate_list(const std::vector<Candidate>& list);

    const std::vector<uint8_t>& buffer() const { return buf_; }
    std::vector<uint8_t> take() { return std::move(buf_); }
    size_t size() const { return buf_.size(); }

private:
    std::vector<uint8_t> buf_;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t len) : data_(data), len_(len) {}
    explicit Reader(const std::vector<uint8_t>& v) : data_(v.data()), len_(v.size()) {}

    uint8_t  get_u8();
    uint16_t get_u16();
    uint32_t get_u32();
    uint64_t get_u64();
    int32_t  get_i32();
    int64_t  get_i64();
    std::string get_string();
    Candidate get_candidate();
    QueryResult get_query_result();
    std::vector<std::string> get_string_list();
    std::vector<Candidate> get_candidate_list();

    // 任一读取越界后置为 false。
    bool ok() const { return ok_; }
    // 是否已消费全部字节（可用于校验帧无多余尾巴）。
    bool at_end() const { return pos_ == len_; }
    size_t remaining() const { return len_ >= pos_ ? len_ - pos_ : 0; }

private:
    bool ensure(size_t n);

    const uint8_t* data_;
    size_t len_;
    size_t pos_ = 0;
    bool ok_ = true;
};

}  // namespace ipc
}  // namespace fire
