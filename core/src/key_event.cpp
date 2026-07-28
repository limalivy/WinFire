//
//  key_event.cpp
//
#include "fire/key_event.h"

#include <cctype>

namespace fire {

bool KeyEvent::is_alphabet() const {
    if (text.empty()) return false;
    for (char c : text) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    }
    return true;
}

bool KeyEvent::is_digit(int& value) const {
    if (text.size() != 1) return false;
    char c = text[0];
    if (c < '0' || c > '9') return false;
    value = c - '0';
    return true;
}

}  // namespace fire
