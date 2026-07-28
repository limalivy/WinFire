//
//  punctuation.cpp — 对应 Fire/PunctuationConversion.swift
//
#include "fire/punctuation.h"

namespace fire {

// 转换单双引号：第一次按引号输入左引号，第二次输入右引号
std::string PunctuationConverter::transform_quote(const std::string& result) {
    auto it = quote_count_.find(result);
    if (it == quote_count_.end()) {
        return result;
    }
    static const std::map<std::string, std::string> result_map = {
        {"‘", "’"},
        {"“", "”"},
    };
    it->second = (it->second + 1) % 2;
    if (it->second == 0) {
        return result_map.at(result);
    }
    return result;
}

// 转换方括号：第一次按 { 输出「，第二次输出『；按 } 时以左括号为优先匹配
std::string PunctuationConverter::transform_square(const std::string& result) {
    auto it = square_count_.find(result);
    if (it == square_count_.end()) {
        return result;
    }
    static const std::map<std::string, std::string> result_map = {
        {"「", "『"},
        {"」", "』"},
    };
    it->second = (it->second + 1) % 2;
    if (result == "「") {
        square_count_["」"] = (it->second + 1) % 2;
    }
    if (it->second == 0) {
        return result_map.at(result);
    }
    return result;
}

std::string PunctuationConverter::transform_result(const std::string& result) {
    return transform_quote(transform_square(result));
}

std::optional<std::string> PunctuationConverter::conversion(const std::string& origin) {
    const auto& def = default_punctuation();
    bool is_punctuation = def.find(origin) != def.end();
    if (!is_punctuation) {
        return std::nullopt;
    }
    PunctuationMode mode = config_.punctuation_mode;
    if (mode == PunctuationMode::EnUs) {
        return origin;
    }
    if (mode == PunctuationMode::ZhHans) {
        auto it = def.find(origin);
        if (it == def.end()) return std::nullopt;
        return transform_result(it->second);
    }
    if (mode == PunctuationMode::Custom) {
        // 自定义配置可能来自旧版本，缺失项回退到默认中文标点映射
        auto cit = config_.custom_punctuation_settings.find(origin);
        if (cit != config_.custom_punctuation_settings.end()) {
            return transform_result(cit->second);
        }
        auto fit = def.find(origin);
        if (fit != def.end()) {
            return transform_result(fit->second);
        }
        return std::nullopt;
    }
    return std::nullopt;
}

void PunctuationConverter::reset_pairs() {
    for (auto& kv : quote_count_) kv.second = 0;
    for (auto& kv : square_count_) kv.second = 0;
}

}  // namespace fire
