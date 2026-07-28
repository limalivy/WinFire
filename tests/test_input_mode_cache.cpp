//
//  InputModeCache 单测，对应 Fire/InputModeCache.swift
//
#include "test_util.h"
#include "fire/input_mode_cache.h"

using namespace fire;

TEST_CASE(cache_get_miss_returns_nullopt) {
    InputModeCache c(3);
    CHECK(!c.get("a").has_value());
}

TEST_CASE(cache_put_and_get) {
    InputModeCache c(3);
    c.put("app1", InputMode::EnUS);
    auto v = c.get("app1");
    CHECK(v.has_value());
    CHECK(*v == InputMode::EnUS);
}

TEST_CASE(cache_overwrite_value) {
    InputModeCache c(3);
    c.put("app1", InputMode::EnUS);
    c.put("app1", InputMode::ZhHans);
    CHECK(*c.get("app1") == InputMode::ZhHans);
}

TEST_CASE(cache_lru_eviction) {
    InputModeCache c(2);
    c.put("a", InputMode::EnUS);
    c.put("b", InputMode::EnUS);
    // 访问 a 使其成为最近使用
    (void)c.get("a");
    // 插入 c，应淘汰最久未使用的 b
    c.put("c", InputMode::ZhHans);
    CHECK(c.get("a").has_value());
    CHECK(!c.get("b").has_value());
    CHECK(c.get("c").has_value());
}

TEST_CASE(cache_capacity_value) {
    InputModeCache c;
    CHECK_EQ(c.capacity(), (size_t)100);
}
