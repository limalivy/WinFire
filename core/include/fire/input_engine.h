//
//  input_engine.h — 对应 Fire/FireInputController.swift + Fire/Fire.swift
//  16 段 handler 链 + 35/52/53 顶字状态机（平台无关）。
//
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fire/candidate.h"
#include "fire/config.h"
#include "fire/dict_manager.h"
#include "fire/dict_service.h"
#include "fire/input_client.h"
#include "fire/input_mode_cache.h"
#include "fire/key_event.h"
#include "fire/punctuation.h"

namespace fire {

// 上屏候选后回调（对应 Fire.candidateInserted 通知），用于统计。
struct CandidateInsertedInfo {
    Candidate candidate;
    std::string app_id;
    std::vector<std::string> hanzi_frequency_parts;  // 组合候选拆分（2+2 / 3+1）
};

class InputEngine {
public:
    InputEngine(Config& config, IDictService& dict, InputClient& client);

    // 主入口，对应 handle(_:client:)。返回 true 表示事件已被输入法消费。
    bool handle_key(const KeyEvent& event);

    // 中英文切换，对应 Fire.toggleInputMode
    void toggle_input_mode();
    void set_input_mode(InputMode mode, bool show_tip = true);
    InputMode input_mode() const { return input_mode_; }

    // 按应用输入模式（per-app），对应 FireInputServer.swift
    // restore：切到该应用应有的输入模式，返回是否发生了变化
    bool restore_input_mode_for_app(const std::string& app_id);
    // save：把当前输入模式记入该应用缓存
    void save_input_mode_for_app(const std::string& app_id);

    // 候选查询入口，对应 Fire.getCandidates
    QueryResult get_candidates(const std::string& origin, int page = 1);

    // 翻页
    void prev_page();
    void next_page();

    void clean();  // 对应 clean()

    // 供外层（候选窗点击）调用
    void insert_candidate(const Candidate& candidate);
    void insert_text(const std::string& text);
    void insert_origin_text();

    // 测试可见的只读状态
    const std::string& original_string() const { return original_string_; }
    const std::vector<Candidate>& candidates() const { return candidates_; }
    int cur_page() const { return cur_page_; }
    bool has_next() const { return has_next_; }

    // 统计回调注入（可选）
    void set_candidate_inserted_callback(std::function<void(const CandidateInsertedInfo&)> cb) {
        on_candidate_inserted_ = std::move(cb);
    }

private:
    Config& config_;
    IDictService& dict_;
    InputClient& client_;
    PunctuationConverter punctuation_;

    InputMode input_mode_ = InputMode::ZhHans;
    InputModeCache input_mode_cache_;

    std::vector<Candidate> candidates_;
    bool has_next_ = false;
    bool last_input_is_number_ = false;
    std::string last_input_text_;

    std::map<std::string, std::vector<std::string>> hanzi_frequency_parts_by_candidate_;

    // 35 顶状态
    bool wubi35_pending_fourth_ = false;
    bool wubi35_has_space_after_third_ = false;

    std::string original_string_;
    int cur_page_ = 1;

    std::function<void(const CandidateInsertedInfo&)> on_candidate_inserted_;

    // 中英文切换：Shift 单击检测
    double last_modifier_down_time_ = 0;

    // ---- 内部工具 ----
    void set_original_string(const std::string& value);
    void mark_text();
    void refresh_candidates_window();
    void update_candidates();

    WubiDingMode current_wubi_ding_mode() const;
    bool is_reverse_lookup_mode() const;
    std::string display_original_string() const;  // getWubi35DingDisplayOriginalString

    void remember_dynamic_frequency_if_needed(const Candidate& candidate);

    // 顶字辅助
    std::optional<Candidate> first_valid_candidate(const std::string& code);
    std::pair<std::optional<Candidate>, std::optional<Candidate>> wubi52_pair(const std::string& code);
    std::optional<Candidate> wubi52_combo(const std::string& code);
    bool commit_wubi52_leading_pair_and_carry(const std::string& next_input);

    // ---- handlers（返回 optional<bool>：有值表示已决定是否消费；无值表示继续下一个 handler）----
    std::optional<bool> hotkey_handler(const KeyEvent& e);
    std::optional<bool> caps_lock_handler(const KeyEvent& e);
    std::optional<bool> flag_changed_handler(const KeyEvent& e);
    std::optional<bool> en_mode_handler(const KeyEvent& e);
    std::optional<bool> predictor_handler(const KeyEvent& e);
    std::optional<bool> page_key_handler(const KeyEvent& e);
    std::optional<bool> delete_key_handler(const KeyEvent& e);
    std::optional<bool> wubi52_ding_handler(const KeyEvent& e);
    std::optional<bool> wubi53_ding_handler(const KeyEvent& e);
    std::optional<bool> wubi35_ding_handler(const KeyEvent& e);
    std::optional<bool> char_key_handler(const KeyEvent& e);
    std::optional<bool> number_key_handler(const KeyEvent& e);
    std::optional<bool> esc_key_handler(const KeyEvent& e);
    std::optional<bool> enter_key_handler(const KeyEvent& e);
    std::optional<bool> space_key_handler(const KeyEvent& e);
    std::optional<bool> punctuation_key_handler(const KeyEvent& e);

    void commit_original_text_as_uppercase();
    bool should_concat_with_whitespace(const std::string& last, const std::string& next) const;
};

// 工具：UTF-8 字符串的“字符数”（中文按 1 个计），对应 Swift String.count 语义
size_t utf8_length(const std::string& s);
// 取 UTF-8 前 n 个字符
std::string utf8_prefix(const std::string& s, size_t n);
// 取 UTF-8 后 n 个字符
std::string utf8_suffix(const std::string& s, size_t n);
// 删除末尾一个 UTF-8 字符
std::string utf8_drop_last(const std::string& s);

}  // namespace fire
