//
//  types.cpp — 默认中文标点表，对应 Fire/types.swift 的 punctuation 字典
//
#include "fire/types.h"

namespace fire {

const std::unordered_map<std::string, std::string>& default_punctuation() {
    // 与原项目 punctuation 一致
    static const std::unordered_map<std::string, std::string> table = {
        {",", "，"},  {".", "。"},  {"/", "、"},  {";", "；"},
        {"'", "‘"},   {"[", "【"},  {"]", "】"},  {"`", "·"},
        {"!", "！"},  {"@", "＠"},  {"#", "＃"},  {"$", "￥"},
        {"%", "％"},  {"^", "……"}, {"&", "＆"},  {"*", "＊"},
        {"(", "（"},  {")", "）"},  {"-", "-"},   {"_", "——"},
        {"+", "＋"},  {"=", "="},   {"~", "～"},  {"{", "「"},
        {"\\", "、"}, {"|", "｜"},  {"}", "」"},  {":", "："},
        {"\"", "“"},  {"<", "《"},  {">", "》"},  {"?", "？"},
    };
    return table;
}

}  // namespace fire
