//
//  utf8.cpp — UTF-8 字符级操作，对齐 Swift String.count / prefix / suffix / dropLast 语义
//
#include "fire/input_engine.h"

namespace fire {

namespace {
std::vector<size_t> char_starts(const std::string& s) {
    std::vector<size_t> starts;
    for (size_t i = 0; i < s.size();) {
        starts.push_back(i);
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) i += 1;
        else if ((c >> 5) == 0x6) i += 2;
        else if ((c >> 4) == 0xE) i += 3;
        else if ((c >> 3) == 0x1E) i += 4;
        else i += 1;
    }
    return starts;
}
}  // namespace

size_t utf8_length(const std::string& s) {
    return char_starts(s).size();
}

std::string utf8_prefix(const std::string& s, size_t n) {
    auto starts = char_starts(s);
    if (n >= starts.size()) return s;
    return s.substr(0, starts[n]);
}

std::string utf8_suffix(const std::string& s, size_t n) {
    auto starts = char_starts(s);
    if (n >= starts.size()) return s;
    return s.substr(starts[starts.size() - n]);
}

std::string utf8_drop_last(const std::string& s) {
    auto starts = char_starts(s);
    if (starts.empty()) return s;
    return s.substr(0, starts.back());
}

}  // namespace fire
