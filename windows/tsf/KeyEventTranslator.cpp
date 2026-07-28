//
//  KeyEventTranslator.cpp
//
#include "KeyEventTranslator.h"

namespace firewin {

// ---- ModifierKeyUpChecker ----
bool ModifierKeyUpChecker::OnKeyDown(UINT vk) {
    if (vk == vk_ || vk == VK_LSHIFT || vk == VK_RSHIFT) {
        if (!modifierDown_) {
            modifierDown_ = true;
            otherKeyBetween_ = false;
            downTick_ = GetTickCount();
        }
        return false;
    }
    // 修饰键按下期间的其它键，破坏“单击”条件
    if (modifierDown_) {
        otherKeyBetween_ = true;
    }
    return false;
}

bool ModifierKeyUpChecker::OnKeyUp(UINT vk) {
    if (vk == vk_ || vk == VK_LSHIFT || vk == VK_RSHIFT) {
        bool wasClean = modifierDown_ && !otherKeyBetween_;
        DWORD dt = GetTickCount() - downTick_;
        modifierDown_ = false;
        otherKeyBetween_ = false;
        return wasClean && dt <= kMaxIntervalMs;
    }
    return false;
}

// ---- 编码转换 ----
std::string KeyEventTranslator::Utf16ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring KeyEventTranslator::Utf8ToUtf16(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

bool KeyEventTranslator::VkToText(UINT vk, UINT scanCode, const BYTE keyboardState[256],
                                  std::string& utf8Out, bool noStateChange) {
    WCHAR buf[8] = {0};
    // 使用当前键盘布局，翻译成 Unicode 字符。
    // wFlags 位 2(0x4)：不改写键盘死键状态（Windows 10 1607+）；查询阶段(OnTest*)用它，
    // 避免污染死键缓冲影响后续真实按键。
    UINT flags = noStateChange ? 0x4u : 0u;
    int r = ToUnicode(vk, scanCode, keyboardState, buf, 4, flags);
    if (r < 0 && !noStateChange) {
        // r<0：这是一个死键，ToUnicode 已把它压入布局的死键缓冲。再调一次把它清出，
        // 以免残留死键状态影响下一个真实按键（旧系统不支持 0x4 标志时的兜底）。
        WCHAR flush[8] = {0};
        ToUnicode(vk, scanCode, keyboardState, flush, 4, 0);
    }
    if (r == 1 || r == 2) {
        std::wstring w(buf, r);
        // 过滤控制字符（回车/退格/ESC/Tab 等由 SpecialKey 处理）
        if (w.size() == 1 && w[0] < 0x20) return false;
        utf8Out = Utf16ToUtf8(w);
        return !utf8Out.empty();
    }
    return false;
}

fire::KeyEvent KeyEventTranslator::Translate(UINT vk, UINT scanCode, const BYTE keyboardState[256],
                                             bool noStateChange) {
    fire::KeyEvent e;

    e.shift = (keyboardState[VK_SHIFT] & 0x80) != 0;
    e.control = (keyboardState[VK_CONTROL] & 0x80) != 0;
    e.option = (keyboardState[VK_MENU] & 0x80) != 0;  // Alt
    e.command = (keyboardState[VK_LWIN] & 0x80) || (keyboardState[VK_RWIN] & 0x80);
    e.caps_lock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;

    switch (vk) {
        case VK_SPACE:  e.special = fire::SpecialKey::Space; return e;
        case VK_RETURN: e.special = fire::SpecialKey::Enter; return e;
        case VK_BACK:   e.special = fire::SpecialKey::Backspace; return e;
        case VK_DELETE: e.special = fire::SpecialKey::ForwardDelete; return e;
        case VK_ESCAPE: e.special = fire::SpecialKey::Escape; return e;
        case VK_TAB:    e.special = fire::SpecialKey::Tab; return e;
        case VK_LEFT:   e.special = fire::SpecialKey::LeftArrow; return e;
        case VK_RIGHT:  e.special = fire::SpecialKey::RightArrow; return e;
        case VK_UP:     e.special = fire::SpecialKey::UpArrow; return e;
        case VK_DOWN:   e.special = fire::SpecialKey::DownArrow; return e;
        case VK_HOME:   e.special = fire::SpecialKey::Home; return e;
        case VK_END:    e.special = fire::SpecialKey::End; return e;
        case VK_PRIOR:  e.special = fire::SpecialKey::PageUp; return e;
        case VK_NEXT:   e.special = fire::SpecialKey::PageDown; return e;
        default: break;
    }

    // 可见字符
    std::string text;
    if (VkToText(vk, scanCode, keyboardState, text, noStateChange)) {
        e.text = text;
    }
    return e;
}

}  // namespace firewin
