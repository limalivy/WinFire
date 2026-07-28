//
//  标点转换测试，对应 PunctuationConversion.swift
//
#include "test_util.h"
#include "fire/punctuation.h"

using namespace fire;

TEST_CASE(punctuation_zhhans_basic) {
    Config cfg;
    cfg.punctuation_mode = PunctuationMode::ZhHans;
    PunctuationConverter p(cfg);
    CHECK_STR_EQ(p.conversion(",").value_or("?"), "，");
    CHECK_STR_EQ(p.conversion(".").value_or("?"), "。");
    CHECK_STR_EQ(p.conversion("/").value_or("?"), "、");
    // 非标点返回 nullopt
    CHECK(!p.conversion("a").has_value());
}

TEST_CASE(punctuation_enus_passthrough) {
    Config cfg;
    cfg.punctuation_mode = PunctuationMode::EnUs;
    PunctuationConverter p(cfg);
    CHECK_STR_EQ(p.conversion(",").value_or("?"), ",");
    CHECK_STR_EQ(p.conversion(".").value_or("?"), ".");
}

TEST_CASE(punctuation_quote_pairing) {
    Config cfg;
    cfg.punctuation_mode = PunctuationMode::ZhHans;
    PunctuationConverter p(cfg);
    // 单引号：第一次左，第二次右
    CHECK_STR_EQ(p.conversion("'").value_or("?"), "‘");
    CHECK_STR_EQ(p.conversion("'").value_or("?"), "’");
    CHECK_STR_EQ(p.conversion("'").value_or("?"), "‘");
    // 双引号独立计数
    CHECK_STR_EQ(p.conversion("\"").value_or("?"), "“");
    CHECK_STR_EQ(p.conversion("\"").value_or("?"), "”");
}

TEST_CASE(punctuation_square_pairing) {
    Config cfg;
    cfg.punctuation_mode = PunctuationMode::ZhHans;
    PunctuationConverter p(cfg);
    // { -> 「 第一次，第二次 『
    CHECK_STR_EQ(p.conversion("{").value_or("?"), "「");
    CHECK_STR_EQ(p.conversion("{").value_or("?"), "『");
}
