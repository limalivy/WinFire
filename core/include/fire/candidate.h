//
//  candidate.h — 对应 Fire/types.swift 中的 CandidateType 与 Candidate
//
#pragma once

#include <string>

namespace fire {

// 候选词类型，对应 CandidateType
enum class CandidateType {
    Wb,          // 五笔
    Py,          // 拼音
    User,        // 用户词库
    Placeholder, // 运行时占位（无匹配）
};

std::string to_string(CandidateType type);              // 序列化为 "wb"/"py"/"user"/"placeholder"
bool candidate_type_from_string(const std::string& s, CandidateType& out);

// 候选词，对应 struct Candidate
struct Candidate {
    std::string code;   // 编码
    std::string text;   // 上屏文本
    CandidateType type = CandidateType::Wb;
    std::string label;  // 展示文本，默认与 text 相同

    Candidate() = default;
    Candidate(std::string code_, std::string text_, CandidateType type_,
              std::string label_ = std::string())
        : code(std::move(code_)),
          text(std::move(text_)),
          type(type_),
          label(label_.empty() ? text : std::move(label_)) {}

    // 对应 Swift Candidate 的 Hashable：按 code/text/type/label 值相等
    bool operator==(const Candidate& o) const {
        return code == o.code && text == o.text && type == o.type && label == o.label;
    }
};

}  // namespace fire
