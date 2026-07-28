//
//  types.h — 对应 Fire/types.swift 中的枚举与标点表
//  平台无关内核，使用现代 C++17。
//
#pragma once

#include <string>
#include <unordered_map>

namespace fire {

// 候选词方向，对应 CandidatesDirection
enum class CandidatesDirection {
    Vertical = 0,
    Horizontal = 1,
    None = 2,
};

// 输入模式，对应 InputMode
enum class InputMode {
    ZhHans,  // 中文
    EnUS,    // 英文
};

// 应用级输入模式设置，对应 InputModeSetting
enum class InputModeSetting {
    ZhHans,
    EnUS,
    RecentUsed,
};

// 编码方案，对应 CodeMode
enum class CodeMode {
    Wubi = 0,       // 纯五笔
    Pinyin = 1,     // 纯拼音
    WubiPinyin = 2, // 五笔拼音混输
};

// 五笔顶字模式，对应 WubiDingMode
enum class WubiDingMode {
    None,
    Ding35,
    Ding52,
    Ding53,
};

// 标点模式，对应 PunctuationMode
enum class PunctuationMode {
    EnUs,   // 半角
    ZhHans, // 全角
    Custom, // 自定义
};

// 按应用输入模式提示时机，对应 AppInputModeTipShowTime
enum class AppInputModeTipShowTime {
    OnlyChanged = 0, // 仅在变化时显示
    Always = 1,      // 总是显示
    None = 2,        // 不显示
};

// 切换中英文的修饰键，对应 ModifierKey
enum class ModifierKey {
    Shift,
    LeftShift,
    RightShift,
    Control,
    Command, // Windows 键
    Option,  // Alt
    Function,
};

// 默认中文标点映射表，对应 types.swift 中的 punctuation 字典
const std::unordered_map<std::string, std::string>& default_punctuation();

}  // namespace fire
