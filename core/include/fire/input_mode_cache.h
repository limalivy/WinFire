//
//  input_mode_cache.h — 对应 Fire/InputModeCache.swift
//  按应用记忆输入模式的 LRU 缓存（默认容量 100）。平台无关。
//
#pragma once

#include <list>
#include <optional>
#include <string>
#include <unordered_map>

#include "fire/types.h"

namespace fire {

class InputModeCache {
public:
    explicit InputModeCache(size_t capacity = 100) : capacity_(capacity) {}

    size_t capacity() const { return capacity_; }

    // 取某应用记忆的输入模式；命中时刷新其为最近使用
    std::optional<InputMode> get(const std::string& key);

    // 写入某应用输入模式；超过容量时淘汰最久未使用项
    void put(const std::string& key, InputMode value);

private:
    void update_key_order(const std::string& key);

    size_t capacity_;
    std::unordered_map<std::string, InputMode> cache_;
    std::list<std::string> keys_;  // 头为最久未使用，尾为最近使用
};

}  // namespace fire
