//
//  config.h — 运行时配置，对应 Fire/types.swift 的 Defaults.Keys 与 ThemeConfig.swift
//  内核只读取配置；JSON 持久化由外层负责（Windows: config.json）。
//
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>

#include "fire/types.h"

namespace fire {

// 颜色，对应 ColorData
struct ColorData {
    double red = 0;
    double green = 0;
    double blue = 0;
    double opacity = 1;
};

// 单一外观（浅色/深色），对应 ApperanceThemeConfig
// 字段与业火主题 JSON（camelCase）一一对应；v2 新增字段在末尾，缺省时取业火默认值。
struct AppearanceThemeConfig {
    ColorData window_background_color{1, 1, 1, 1};
    float window_padding_top = 0;
    float window_padding_left = 0;
    float window_padding_right = 0;
    float window_padding_bottom = 0;
    float window_border_radius = 6;

    ColorData origin_code_color{0.3, 0.3, 0.3, 1};
    float origin_candidates_space = 0;
    float candidate_space = 0;

    ColorData candidate_index_color{0.1, 0.1, 0.1, 1};
    ColorData candidate_text_color{0.1, 0.1, 0.1, 1};
    ColorData candidate_code_color{0.3, 0.3, 0.3, 0.8};

    ColorData selected_index_color{0.863, 0.078, 0.235, 1};
    ColorData selected_text_color{0.863, 0.078, 0.235, 1};
    ColorData selected_code_color{0.863, 0.078, 0.235, 0.8};

    ColorData page_indicator_color{0.863, 0.078, 0.235, 1};
    ColorData page_indicator_disabled_color{0.863, 0.078, 0.235, 0.4};

    std::string font_name = "system";
    float font_size = 20;

    // v2 新增字段（业火 schemaVersion=2）。v1 主题缺这些字段时取下列默认值。
    bool enable_liquid_glass = true;          // Windows 无 NSVisualEffectView 等价物，渲染层忽略
    ColorData selected_background_color{0, 0, 0, 0};  // 选中项圆角背景；全透=不画
    float candidate_radius = 0;               // 选中背景圆角半径
    float candidate_padding_top = 2;
    float candidate_padding_left = 2;
    float candidate_padding_right = 2;
    float candidate_padding_bottom = 2;
    float origin_padding_top = 0;
    float origin_padding_left = 0;
    float origin_padding_right = 0;
    float origin_padding_bottom = 0;
    float index_font_size = 20;               // 序号字号；v1 取 font_size
    float code_font_size = 20;                // 编码提示字号；v1 取 font_size
};

// 主题，对应 ThemeConfig
struct ThemeConfig {
    int schema_version = 2;       // 业火 themeSchemaVersion；导入时按文件值，未写则 2
    std::string id = "default";
    std::string name = "默认";
    std::string author = "微火输入法";
    AppearanceThemeConfig light;   // 浅色
    AppearanceThemeConfig dark;    // 深色

    // 深色模式偏好：0=跟随系统，1=强制浅色，2=强制深色。
    // 渲染层据此与 light/dark 配色选择。跟随系统时读 Windows 注册表 AppsUseLightTheme。
    int dark_mode_preference = 0;

    const AppearanceThemeConfig& appearance(bool is_dark) const {
        return is_dark ? dark : light;
    }
};

// 运行时配置。字段名与 Defaults.Keys 对应。
struct Config {
    // 反查 & 候选
    bool z_key_query = true;                    // zKeyQuery：` + 拼音反查形码
    CandidatesDirection candidates_direction = CandidatesDirection::Horizontal;
    bool show_code_in_window = true;
    bool wubi_code_tip = true;
    bool wubi_auto_commit = false;

    // 顶字
    bool wubi35_ding = false;                   // 历史布尔开关，兼容用
    WubiDingMode wubi_ding_mode = WubiDingMode::None;

    bool enable_word_input = true;              // 是否允许词组（否则 length(text)=1）
    bool enable_dynamic_frequency = false;      // 动态调频
    std::map<std::string, std::string> dynamic_frequency_memory;  // query -> text
    int candidate_count = 5;
    CodeMode code_mode = CodeMode::WubiPinyin;

    // 中英文切换
    bool disable_en_mode = false;
    bool disable_temp_en_mode = false;          // ; 临时英文
    ModifierKey toggle_input_mode_key = ModifierKey::Shift;

    // 标点
    PunctuationMode punctuation_mode = PunctuationMode::ZhHans;
    std::unordered_map<std::string, std::string> custom_punctuation_settings;  // 默认为 default_punctuation()
    bool enable_dot_after_number = true;        // 数字后 "。" 转 "."
    bool enable_punctuation_commit = true;      // 编码后标点先上屏首候选
    bool enable_whitespace_between_zh_en = true;

    // 按应用输入模式（per-app）
    bool keep_app_input_mode = false;           // 保持应用最后使用的输入模式
    AppInputModeTipShowTime app_input_mode_tip_show_time = AppInputModeTipShowTime::OnlyChanged;
    // 应用固定输入模式设置：宿主标识（Windows 用进程 exe 名）-> InputModeSetting
    std::map<std::string, InputModeSetting> app_settings;

    // 输入统计
    bool enable_statistics = false;
    bool enable_hanzi_frequency_statistics = false;
    std::string stats_db_path;                  // 统计库 sqlite 路径

    // 词库路径
    std::string wb_table_path;
    std::string py_table_path;
    std::string db_path;                        // sqlite 词库路径

    // 主题
    ThemeConfig theme;

    // 临时英文触发标点（与原项目一致，固定为 ';'）
    char temp_en_trigger = ';';
};

}  // namespace fire
