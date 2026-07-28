//
//  input_mode_cache.cpp
//
#include "fire/input_mode_cache.h"

#include <algorithm>

namespace fire {

std::optional<InputMode> InputModeCache::get(const std::string& key) {
    auto it = cache_.find(key);
    if (it == cache_.end()) return std::nullopt;
    update_key_order(key);
    return it->second;
}

void InputModeCache::put(const std::string& key, InputMode value) {
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        if (keys_.size() >= capacity_ && !keys_.empty()) {
            const std::string oldest = keys_.front();
            keys_.pop_front();
            cache_.erase(oldest);
        }
        keys_.push_back(key);
    } else {
        update_key_order(key);
    }
    cache_[key] = value;
}

void InputModeCache::update_key_order(const std::string& key) {
    auto it = std::find(keys_.begin(), keys_.end(), key);
    if (it != keys_.end()) {
        keys_.erase(it);
        keys_.push_back(key);
    }
}

}  // namespace fire
