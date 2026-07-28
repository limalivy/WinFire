//
//  punctuation.h — 对应 Fire/PunctuationConversion.swift
//  标点转换 + 引号/方括号成对状态机。
//
#pragma once

#include <map>
#include <optional>
#include <string>

#include "fire/config.h"

namespace fire {

class PunctuationConverter {
public:
    explicit PunctuationConverter(const Config& config) : config_(config) {}

    // 对应 conversion(_:)。返回 std::nullopt 表示该字符不是标点，不做转换。
    std::optional<std::string> conversion(const std::string& origin);

    void reset_pairs();  // 重置成对状态（可选，便于测试）

private:
    const Config& config_;
    // 引号计数：‘ “ -> 0/1
    std::map<std::string, int> quote_count_{{"‘", 0}, {"“", 0}};
    // 方括号计数：「 」-> 0/1
    std::map<std::string, int> square_count_{{"「", 0}, {"」", 0}};

    std::string transform_quote(const std::string& result);
    std::string transform_square(const std::string& result);
    std::string transform_result(const std::string& result);
};

}  // namespace fire
