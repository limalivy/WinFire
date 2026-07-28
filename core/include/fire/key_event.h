//
//  key_event.h — 平台无关按键事件，抽象 macOS NSEvent / Windows VK_*
//
#pragma once

#include <string>

namespace fire {

// 特殊功能键。普通可见字符通过 text 字段传入，SpecialKey 为 None。
enum class SpecialKey {
    None,
    Space,
    Enter,
    Backspace,     // 退格（macOS kVK_Delete）
    ForwardDelete, // 前向删除
    Escape,
    Tab,
    LeftArrow,
    RightArrow,
    UpArrow,
    DownArrow,
    Home,
    End,
    PageUp,
    PageDown,
    CapsLock,
    ShiftKey,
    ControlKey,
    OptionKey,   // Alt
    CommandKey,  // Win
    FunctionKey,
};

// 平台无关按键事件。
// TSF 层负责把 Windows 消息（WM_KEYDOWN / VK_*）翻译成本结构。
struct KeyEvent {
    // 可见字符（UTF-8）。对于纯功能键或修饰键，为空。
    std::string text;

    SpecialKey special = SpecialKey::None;

    // 修饰键状态
    bool shift = false;
    bool control = false;
    bool option = false;   // Alt
    bool command = false;  // Win
    bool caps_lock = false;

    // 是否为修饰键状态变化事件（对应 macOS flagsChanged / keyUp of modifier），
    // 用于 Shift 单击切换中英文的检测。
    bool is_modifier_change = false;

    // 修饰键变化事件对应的键（仅 is_modifier_change 时有效）
    SpecialKey changed_modifier = SpecialKey::None;

    // 中英文切换请求：由平台层（TSF 的 ModifierKeyUpChecker）完成“修饰键单击”
    // 的时序检测后置位。核心据此触发 toggle，不在内核处理平台相关的计时。
    bool toggle_input_mode_request = false;

    // 便捷判断：是否含有 command/control/option 中的任意一个（应用快捷键）
    bool has_command_shortcut_modifier() const {
        return command || control || option;
    }

    bool is_alphabet() const;  // text 是否为纯 [a-zA-Z]
    bool is_digit(int& value) const;  // text 是否为单个数字，返回数值
};

}  // namespace fire
