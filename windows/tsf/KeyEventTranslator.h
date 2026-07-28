//
//  KeyEventTranslator.h — 把 Windows 键盘消息翻译为 fire::KeyEvent
//
#pragma once

#include <windows.h>
#include <string>

#include "fire/key_event.h"

namespace firewin {

// 修饰键单击检测（对应 macOS ModifierKeyUpChecker）：
// 记录 Shift 按下时刻，若按下-抬起之间没有其它键，且间隔在阈值内，则视为“单击 Shift”，
// 用于中英文切换。这一时序检测属于平台层职责，内核只消费 toggle_input_mode_request。
class ModifierKeyUpChecker {
public:
    // 目标修饰键（默认左右 Shift）
    explicit ModifierKeyUpChecker(UINT vkModifier = VK_SHIFT) : vk_(vkModifier) {}

    // 在 keydown/keyup 时调用；返回 true 表示“检测到一次单击切换”。
    bool OnKeyDown(UINT vk);
    bool OnKeyUp(UINT vk);

private:
    UINT vk_;
    bool modifierDown_ = false;   // 目标修饰键当前是否按下
    bool otherKeyBetween_ = false; // 修饰键按下后是否出现其它键
    DWORD downTick_ = 0;
    static constexpr DWORD kMaxIntervalMs = 300;
};

// 把一次 WM_KEYDOWN（配合修饰键状态与 ToUnicode 结果）翻译为 fire::KeyEvent。
// wparam: 虚拟键码；返回的 KeyEvent 已填好 text / special / 修饰位。
class KeyEventTranslator {
public:
    // 生成 KeyEvent。isKeyUp 为 true 表示这是抬起事件（主要用于修饰键切换检测）。
    // noStateChange=true（用于 OnTest* 查询阶段）时不改写键盘布局的死键状态，
    // 避免影响后续真实按键的死键组合（欧洲语言布局）。
    fire::KeyEvent Translate(UINT vk, UINT scanCode, const BYTE keyboardState[256],
                             bool noStateChange = false);

    // 判断某个 VK 是否可见字符键（用 ToUnicode 拿字符）。
    static bool VkToText(UINT vk, UINT scanCode, const BYTE keyboardState[256],
                         std::string& utf8Out, bool noStateChange = false);

    // UTF-16 -> UTF-8
    static std::string Utf16ToUtf8(const std::wstring& w);
    static std::wstring Utf8ToUtf16(const std::string& s);

    ModifierKeyUpChecker shiftChecker;
};

}  // namespace firewin
