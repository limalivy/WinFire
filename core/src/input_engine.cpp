//
//  input_engine.cpp — 对应 Fire/FireInputController.swift + Fire/Fire.swift
//  16 段 handler 链 + 35/52/53 顶字状态机。
//
#include "fire/input_engine.h"

#include <cctype>

namespace fire {

namespace {
bool str_is_all_alpha(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    }
    return true;
}

bool parse_int(const std::string& s, int& out) {
    if (s.empty()) return false;
    size_t i = 0;
    bool neg = false;
    if (s[0] == '-') { neg = true; i = 1; }
    if (i >= s.size()) return false;
    long v = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    out = static_cast<int>(neg ? -v : v);
    return true;
}

std::string to_upper(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return r;
}
}  // namespace

InputEngine::InputEngine(Config& config, IDictService& dict, InputClient& client)
    : config_(config), dict_(dict), client_(client), punctuation_(config) {}

// ---- 对应 Fire.getCandidates ----
QueryResult InputEngine::get_candidates(const std::string& origin, int page) {
    QueryResult empty;
    if (origin.empty()) return empty;
    // ` + 拼音反查形码
    if (config_.z_key_query && !origin.empty() && origin[0] == '`') {
        std::string pinyin = origin.substr(1);
        return dict_.GetReverseLookup(pinyin, page);
    }
    return dict_.GetCandidates(origin, page);
}

// ---- 中英文切换，对应 Fire.toggleInputMode ----
void InputEngine::toggle_input_mode() {
    input_mode_ = (input_mode_ == InputMode::EnUS) ? InputMode::ZhHans : InputMode::EnUS;
    client_.show_input_mode_toast(input_mode_ == InputMode::EnUS ? "英" : "中");
}

void InputEngine::set_input_mode(InputMode mode, bool show_tip) {
    if (mode == input_mode_) return;
    input_mode_ = mode;
    if (show_tip) {
        client_.show_input_mode_toast(input_mode_ == InputMode::EnUS ? "英" : "中");
    }
}

// ---- 按应用输入模式（per-app），对应 FireInputServer.swift ----
bool InputEngine::restore_input_mode_for_app(const std::string& app_id) {
    if (app_id.empty()) return false;
    InputMode before = input_mode_;
    // 1) 优先应用固定设置
    auto it = config_.app_settings.find(app_id);
    if (it != config_.app_settings.end()) {
        if (it->second == InputModeSetting::ZhHans) {
            set_input_mode(InputMode::ZhHans, false);
            return before != input_mode_;
        }
        if (it->second == InputModeSetting::EnUS) {
            set_input_mode(InputMode::EnUS, false);
            return before != input_mode_;
        }
        // RecentUsed 落到缓存逻辑
    }
    // 2) 启用「保持应用最后使用的输入模式」时读缓存
    if (config_.keep_app_input_mode) {
        if (auto cached = input_mode_cache_.get(app_id)) {
            set_input_mode(*cached, false);
            return before != input_mode_;
        }
    }
    return false;
}

void InputEngine::save_input_mode_for_app(const std::string& app_id) {
    if (app_id.empty()) return;
    if (!config_.keep_app_input_mode) return;
    // 有固定设置的应用不写缓存（与 macOS 版一致）
    if (config_.app_settings.find(app_id) != config_.app_settings.end()) return;
    input_mode_cache_.put(app_id, input_mode_);
}

// ---- _originalString didSet 等价 ----
void InputEngine::set_original_string(const std::string& value) {
    original_string_ = value;
    if (cur_page_ != 1) {
        cur_page_ = 1;
        mark_text();
        return;
    }
    mark_text();
    if (utf8_length(original_string_) > 0) {
        refresh_candidates_window();
    } else {
        client_.hide_candidates();
    }
}

void InputEngine::mark_text() {
    // 对应 markText。组字区展示串：
    std::string selected;
    bool show_code = config_.show_code_in_window &&
                     (config_.candidates_direction != CandidatesDirection::None ||
                      is_reverse_lookup_mode());
    if (show_code) {
        selected = utf8_length(original_string_) > 0 ? " " : "";
    } else {
        selected = display_original_string();
    }
    if (selected.empty()) {
        client_.clear_marked_text();
    } else {
        client_.set_marked_text(selected);
    }
}

WubiDingMode InputEngine::current_wubi_ding_mode() const {
    WubiDingMode mode = config_.wubi_ding_mode;
    if (mode == WubiDingMode::None && config_.wubi35_ding) {
        return WubiDingMode::Ding35;
    }
    return mode;
}

bool InputEngine::is_reverse_lookup_mode() const {
    return config_.z_key_query && input_mode_ == InputMode::ZhHans &&
           !original_string_.empty() && original_string_[0] == '`';
}

// 对应 getWubi35DingDisplayOriginalString
std::string InputEngine::display_original_string() const {
    if (!(input_mode_ == InputMode::ZhHans && current_wubi_ding_mode() == WubiDingMode::Ding35 &&
          config_.code_mode == CodeMode::Wubi && wubi35_has_space_after_third_)) {
        return original_string_;
    }
    size_t len = utf8_length(original_string_);
    if (len < 3) return original_string_;
    if (len == 3) return original_string_ + "_";
    return utf8_prefix(original_string_, 3) + "_" + utf8_suffix(original_string_, len - 3);
}

void InputEngine::remember_dynamic_frequency_if_needed(const Candidate& candidate) {
    if (is_reverse_lookup_mode() || candidate.text.empty()) return;
    dict_.RememberDynamicFrequency(original_string_, candidate);
}

std::optional<Candidate> InputEngine::first_valid_candidate(const std::string& code) {
    QueryResult r = get_candidates(code, 1);
    for (const auto& c : r.candidates) {
        if (c.type != CandidateType::Placeholder) return c;
    }
    return std::nullopt;
}

std::pair<std::optional<Candidate>, std::optional<Candidate>> InputEngine::wubi52_pair(
    const std::string& code) {
    if (utf8_length(code) != 4) return {std::nullopt, std::nullopt};
    std::string left_code = utf8_prefix(code, 2);
    std::string right_code = utf8_suffix(code, 2);
    return {first_valid_candidate(left_code), first_valid_candidate(right_code)};
}

std::optional<Candidate> InputEngine::wubi52_combo(const std::string& code) {
    auto pair = wubi52_pair(code);
    if (!pair.first || !pair.second) return std::nullopt;
    return Candidate(code, pair.first->text + pair.second->text, CandidateType::Wb);
}

bool InputEngine::commit_wubi52_leading_pair_and_carry(const std::string& next_input) {
    if (utf8_length(original_string_) != 4) return false;
    std::string left_code = utf8_prefix(original_string_, 2);
    std::string carry = utf8_suffix(original_string_, 2) + next_input;
    auto left = first_valid_candidate(left_code);
    if (!left) return false;
    insert_candidate(*left);
    set_original_string(carry);
    return true;
}

// ---- 翻页 ----
void InputEngine::prev_page() {
    int old = cur_page_;
    cur_page_ = cur_page_ > 1 ? cur_page_ - 1 : 1;
    if (old != cur_page_) refresh_candidates_window();
}
void InputEngine::next_page() {
    int old = cur_page_;
    cur_page_ = has_next_ ? cur_page_ + 1 : cur_page_;
    if (old != cur_page_) refresh_candidates_window();
}

// ---- clean ----
void InputEngine::clean() {
    wubi35_pending_fourth_ = false;
    wubi35_has_space_after_third_ = false;
    hanzi_frequency_parts_by_candidate_.clear();
    original_string_.clear();
    cur_page_ = 1;
    client_.hide_candidates();
    client_.clear_marked_text();
}

// ---- insertCandidate / insertText ----
void InputEngine::insert_candidate(const Candidate& candidate) {
    CandidateInsertedInfo info;
    info.candidate = candidate;
    info.app_id = client_.bundle_id();
    auto it = hanzi_frequency_parts_by_candidate_.find(candidate.text);
    if (it != hanzi_frequency_parts_by_candidate_.end() && !it->second.empty()) {
        info.hanzi_frequency_parts = it->second;
    }
    remember_dynamic_frequency_if_needed(candidate);
    insert_text(candidate.text);
    if (on_candidate_inserted_) on_candidate_inserted_(info);
}

bool InputEngine::should_concat_with_whitespace(const std::string& last,
                                                const std::string& next) const {
    // 对应 Utils.shouldConcatWithWhitespace
    if (last.empty() || next.empty()) return false;
    auto is_en = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };
    // 判断汉字（简单判断落在 CJK 区间 \u4e00-\u9fa5，UTF-8 三字节 E4..E9 起始范围内）
    auto starts_cjk = [](const std::string& s) {
        if (s.size() < 3) return false;
        unsigned char b0 = static_cast<unsigned char>(s[0]);
        unsigned char b1 = static_cast<unsigned char>(s[1]);
        unsigned char b2 = static_cast<unsigned char>(s[2]);
        if ((b0 & 0xF0) != 0xE0) return false;
        // 计算码点
        unsigned int cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
        return cp >= 0x4E00 && cp <= 0x9FA5;
    };
    auto ends_cjk = [&](const std::string& s) {
        std::string suf = utf8_suffix(s, 1);
        return starts_cjk(suf);
    };
    // last 末尾英数 + next 开头中文
    unsigned char last_back = static_cast<unsigned char>(last.back());
    if (is_en(last_back) && starts_cjk(next)) return true;
    // last 末尾中文 + next 开头英数
    unsigned char next_front = static_cast<unsigned char>(next.front());
    if (ends_cjk(last) && is_en(next_front)) return true;
    return false;
}

void InputEngine::insert_text(const std::string& text) {
    if (utf8_length(text) > 0) {
        std::string new_text = text;
        if (config_.enable_whitespace_between_zh_en &&
            should_concat_with_whitespace(last_input_text_, text)) {
            new_text = " " + new_text;
        }
        client_.insert_text(new_text);
        char last_char = new_text.empty() ? 0 : new_text.back();
        last_input_is_number_ = (last_char >= '0' && last_char <= '9');
    }
    clean();
}

void InputEngine::insert_origin_text() {
    if (utf8_length(original_string_) > 0) {
        insert_text(original_string_);
    }
}

void InputEngine::commit_original_text_as_uppercase() {
    if (original_string_.empty()) return;
    insert_text(to_upper(original_string_));
}

// ---- updateCandidates ----
void InputEngine::update_candidates() {
    QueryResult r = get_candidates(original_string_, cur_page_);
    has_next_ = r.has_next;
    std::vector<Candidate>& candidates = r.candidates;
    hanzi_frequency_parts_by_candidate_.clear();

    bool wubi_common = input_mode_ == InputMode::ZhHans && config_.code_mode == CodeMode::Wubi &&
                       !is_reverse_lookup_mode() && cur_page_ == 1 &&
                       utf8_length(original_string_) == 4;

    // 52 顶：4 码时候选优先展示 2+2
    if (wubi_common && current_wubi_ding_mode() == WubiDingMode::Ding52) {
        auto combo = wubi52_combo(original_string_);
        if (combo) {
            auto pair = wubi52_pair(original_string_);
            if (pair.first && pair.second) {
                hanzi_frequency_parts_by_candidate_[combo->text] = {pair.first->text,
                                                                    pair.second->text};
            }
            std::vector<Candidate> merged;
            merged.push_back(*combo);
            for (const auto& c : candidates) {
                if (c.text == combo->text) continue;
                merged.push_back(c);
            }
            candidates_ = std::move(merged);
            return;
        }
    }

    // 53 顶：4 码时候选优先展示 3+1
    if (wubi_common && current_wubi_ding_mode() == WubiDingMode::Ding53) {
        std::string prefix3 = utf8_prefix(original_string_, 3);
        std::string suffix1 = utf8_suffix(original_string_, 1);
        QueryResult c3 = get_candidates(prefix3, 1);
        QueryResult c1 = get_candidates(suffix1, 1);
        bool ok3 = !c3.candidates.empty() && c3.candidates.front().type != CandidateType::Placeholder;
        bool ok1 = !c1.candidates.empty() && c1.candidates.front().type != CandidateType::Placeholder;
        if (ok3 && ok1) {
            std::string text = c3.candidates.front().text + c1.candidates.front().text;
            Candidate combo(original_string_, text, CandidateType::Wb);
            hanzi_frequency_parts_by_candidate_[combo.text] = {c3.candidates.front().text,
                                                               c1.candidates.front().text};
            std::vector<Candidate> merged;
            merged.push_back(combo);
            for (const auto& c : candidates) {
                if (c.text == combo.text) continue;
                merged.push_back(c);
            }
            candidates_ = std::move(merged);
            return;
        }
    }

    candidates_ = std::move(candidates);
}

// ---- refreshCandidatesWindow ----
void InputEngine::refresh_candidates_window() {
    update_candidates();
    // 满 4 码唯一候选自动上屏
    if (!is_reverse_lookup_mode() && config_.wubi_auto_commit && candidates_.size() == 1 &&
        utf8_length(original_string_) >= 4 &&
        candidates_.front().type != CandidateType::Placeholder) {
        bool ding53_block = current_wubi_ding_mode() == WubiDingMode::Ding53 &&
                            config_.code_mode == CodeMode::Wubi &&
                            utf8_length(original_string_) == 4;
        if (!ding53_block) {
            insert_candidate(candidates_.front());
            return;
        }
    }
    if (config_.candidates_direction == CandidatesDirection::None && !is_reverse_lookup_mode()) {
        client_.hide_candidates();
        return;
    }
    if (!config_.show_code_in_window && candidates_.empty()) {
        client_.hide_candidates();
        return;
    }
    CandidatesView view;
    view.list = candidates_;
    view.original_string = display_original_string();
    view.has_prev = cur_page_ > 1;
    view.has_next = has_next_;
    view.caret = client_.get_caret_rect();
    client_.show_candidates(view);
}

// ======================= handlers =======================

std::optional<bool> InputEngine::hotkey_handler(const KeyEvent& e) {
    // control + 数字：调整候选到首位
    if (e.is_modifier_change) return std::nullopt;
    int num;
    if (!e.is_digit(num)) return std::nullopt;
    if (e.control && !e.shift && !e.option && !e.command && num > 0 &&
        num <= static_cast<int>(candidates_.size())) {
        dict_.SetCandidateToFirst(original_string_, candidates_[num - 1]);
        cur_page_ = 1;
        refresh_candidates_window();
        return true;
    }
    return std::nullopt;
}

std::optional<bool> InputEngine::caps_lock_handler(const KeyEvent& e) {
    // CapsLock 状态切换事件
    if (e.is_modifier_change && e.changed_modifier == SpecialKey::CapsLock) {
        if (e.caps_lock && !original_string_.empty()) {
            commit_original_text_as_uppercase();
            // 不切换输入模式：CapsLock 灯亮由系统管理，用户期望的是
            // "上屏大写字符 + CapsLock 灯亮"，而非切到英文模式。
            return true;
        }
        return false;
    }

    if (!(input_mode_ == InputMode::ZhHans && e.caps_lock && !e.text.empty())) {
        return std::nullopt;
    }
    if (e.command || e.control || e.option) {
        return std::nullopt;
    }
    if (!e.is_alphabet()) {
        return std::nullopt;
    }
    if (!original_string_.empty()) {
        commit_original_text_as_uppercase();
        return true;
    }
    insert_text(to_upper(e.text));
    return true;
}

std::optional<bool> InputEngine::flag_changed_handler(const KeyEvent& e) {
    // CapsLock 由 handler 链中在前的 caps_lock_handler 统一处理，此处不再重复调用。
    // 修饰键单击切换中英文（时序由平台层检测后置位 toggle_input_mode_request）
    if (!config_.disable_en_mode && e.toggle_input_mode_request) {
        insert_text(original_string_);
        toggle_input_mode();
        return true;
    }
    // 修饰键变化事件只用于切换中英文，其它不处理
    if (e.is_modifier_change) {
        return false;
    }
    // 带 Command/Control/Option 的组合键不处理
    if (e.command || e.control || e.option) {
        return false;
    }
    return std::nullopt;
}

std::optional<bool> InputEngine::en_mode_handler(const KeyEvent& e) {
    if (input_mode_ == InputMode::EnUS) {
        return false;
    }
    return std::nullopt;
}

std::optional<bool> InputEngine::predictor_handler(const KeyEvent& e) {
    // 数字后输入 "." 自动转小数点
    if (config_.enable_dot_after_number && e.text == "." && last_input_is_number_) {
        insert_text(".");
        last_input_is_number_ = false;
        return true;
    }
    last_input_is_number_ = false;
    last_input_text_ = client_.get_previous_text();
    return std::nullopt;
}

std::optional<bool> InputEngine::page_key_handler(const KeyEvent& e) {
    if (input_mode_ == InputMode::ZhHans && utf8_length(original_string_) > 0) {
        bool horizontal = config_.candidates_direction == CandidatesDirection::Horizontal;
        bool vertical = config_.candidates_direction == CandidatesDirection::Vertical;
        bool need_next = e.text == "=" ||
                         (e.special == SpecialKey::DownArrow && horizontal) ||
                         (e.special == SpecialKey::RightArrow && vertical);
        if (need_next) {
            int old = cur_page_;
            cur_page_ = has_next_ ? cur_page_ + 1 : cur_page_;
            if (old != cur_page_) refresh_candidates_window();
            return true;
        }
        bool need_prev = e.text == "-" ||
                         (e.special == SpecialKey::UpArrow && horizontal) ||
                         (e.special == SpecialKey::LeftArrow && vertical);
        if (need_prev) {
            int old = cur_page_;
            cur_page_ = cur_page_ > 1 ? cur_page_ - 1 : 1;
            if (old != cur_page_) refresh_candidates_window();
            return true;
        }
    }
    return std::nullopt;
}

std::optional<bool> InputEngine::delete_key_handler(const KeyEvent& e) {
    if (e.special == SpecialKey::Backspace) {
        if (wubi35_pending_fourth_) {
            wubi35_pending_fourth_ = false;
            wubi35_has_space_after_third_ = false;
            mark_text();
            if (utf8_length(original_string_) > 0) refresh_candidates_window();
            return true;
        }
        if (wubi35_has_space_after_third_ && utf8_length(original_string_) == 3) {
            wubi35_has_space_after_third_ = false;
            mark_text();
            if (utf8_length(original_string_) > 0) refresh_candidates_window();
            return true;
        }
        if (utf8_length(original_string_) > 0) {
            std::string next = utf8_drop_last(original_string_);
            wubi35_pending_fourth_ = false;
            if (utf8_length(next) < 3) {
                wubi35_has_space_after_third_ = false;
            }
            set_original_string(next);
            return true;
        }
        return false;
    }
    return std::nullopt;
}

std::optional<bool> InputEngine::wubi52_ding_handler(const KeyEvent& e) {
    if (!(input_mode_ == InputMode::ZhHans && current_wubi_ding_mode() == WubiDingMode::Ding52 &&
          config_.code_mode == CodeMode::Wubi && !is_reverse_lookup_mode() && !e.text.empty())) {
        return std::nullopt;
    }

    if (e.text == ";" && utf8_length(original_string_) > 0) {
        if (candidates_.size() <= 1) {
            return true;
        }
        insert_candidate(candidates_[1]);
        return true;
    }

    if (utf8_length(original_string_) != 4) {
        return std::nullopt;
    }

    if (e.special == SpecialKey::Space) {
        auto combo = wubi52_combo(original_string_);
        if (combo) {
            insert_candidate(*combo);
            return true;
        }
        return std::nullopt;
    }

    int number;
    if (e.is_digit(number) && number >= 2) {
        return std::nullopt;
    }

    if (!e.is_alphabet()) {
        return std::nullopt;
    }

    return commit_wubi52_leading_pair_and_carry(e.text) ? std::optional<bool>(true)
                                                        : std::nullopt;
}

std::optional<bool> InputEngine::wubi53_ding_handler(const KeyEvent& e) {
    if (!(input_mode_ == InputMode::ZhHans && current_wubi_ding_mode() == WubiDingMode::Ding53 &&
          config_.code_mode == CodeMode::Wubi && !is_reverse_lookup_mode())) {
        return std::nullopt;
    }

    auto commit_code = [&](const std::string& code) -> bool {
        QueryResult r = get_candidates(code, 1);
        if (!r.candidates.empty() && r.candidates.front().type != CandidateType::Placeholder) {
            insert_candidate(r.candidates.front());
            return true;
        }
        return false;
    };
    auto commit_exact4 = [&](const std::string& code) -> bool {
        QueryResult r = get_candidates(code, 1);
        for (const auto& c : r.candidates) {
            if (c.type == CandidateType::Placeholder) continue;
            insert_candidate(c);
            return true;
        }
        return false;
    };

    if (utf8_length(original_string_) != 4) return std::nullopt;
    if (e.text.empty()) return std::nullopt;

    if (e.text == ";") {
        return commit_exact4(original_string_) ? std::optional<bool>(true) : std::nullopt;
    }

    std::string prefix3 = utf8_prefix(original_string_, 3);
    std::string carry = utf8_suffix(original_string_, 1);

    if (!e.is_alphabet()) {
        return std::nullopt;
    }

    if (commit_code(prefix3)) {
        set_original_string(carry + e.text);
        return true;
    }
    return std::nullopt;
}

std::optional<bool> InputEngine::wubi35_ding_handler(const KeyEvent& e) {
    if (!(input_mode_ == InputMode::ZhHans && current_wubi_ding_mode() == WubiDingMode::Ding35 &&
          config_.code_mode == CodeMode::Wubi && !is_reverse_lookup_mode())) {
        wubi35_pending_fourth_ = false;
        wubi35_has_space_after_third_ = false;
        return std::nullopt;
    }

    // 3 码时的空格逻辑
    if (e.special == SpecialKey::Space && utf8_length(original_string_) == 3) {
        if (wubi35_pending_fourth_) {
            wubi35_pending_fourth_ = false;
            wubi35_has_space_after_third_ = false;
            if (!candidates_.empty() && candidates_.front().type != CandidateType::Placeholder) {
                insert_candidate(candidates_.front());
            }
        } else {
            wubi35_pending_fourth_ = true;
            wubi35_has_space_after_third_ = true;
            mark_text();
            if (utf8_length(original_string_) > 0) refresh_candidates_window();
        }
        return true;
    }

    if (e.text.empty() || !e.is_alphabet()) {
        wubi35_pending_fourth_ = false;
        return std::nullopt;
    }

    if (utf8_length(original_string_) == 3) {
        if (wubi35_pending_fourth_) {
            wubi35_pending_fourth_ = false;
            set_original_string(original_string_ + e.text);
            if (candidates_.size() == 1 && !candidates_.empty() &&
                candidates_.front().type != CandidateType::Placeholder) {
                insert_candidate(candidates_.front());
            }
            return true;
        }
        if (!candidates_.empty() && candidates_.front().type != CandidateType::Placeholder) {
            insert_candidate(candidates_.front());
            set_original_string(e.text);
            return true;
        }
    }

    if (wubi35_has_space_after_third_ && utf8_length(original_string_) >= 4) {
        if (!candidates_.empty() && candidates_.front().type != CandidateType::Placeholder) {
            insert_candidate(candidates_.front());
            set_original_string(e.text);
            return true;
        }
    }

    return std::nullopt;
}

std::optional<bool> InputEngine::char_key_handler(const KeyEvent& e) {
    const std::string& string = e.text;

    // ` 键反查
    if (config_.z_key_query && input_mode_ == InputMode::ZhHans && string == "`" &&
        original_string_.empty()) {
        set_original_string("`");
        return true;
    }

    bool is_alpha = str_is_all_alpha(string);
    if (utf8_length(original_string_) <= 0 && !is_alpha) {
        return std::nullopt;
    }
    if (is_alpha) {
        set_original_string(original_string_ + string);
        return true;
    }
    return std::nullopt;
}

std::optional<bool> InputEngine::number_key_handler(const KeyEvent& e) {
    int pos;
    if (e.is_digit(pos)) {
        if (utf8_length(original_string_) > 0) {
            int index = pos - 1;
            if (index >= 0 && index < static_cast<int>(candidates_.size())) {
                insert_candidate(candidates_[index]);
            } else {
                set_original_string(original_string_ + e.text);
            }
            return true;
        }
        last_input_is_number_ = true;
        if (config_.enable_whitespace_between_zh_en &&
            should_concat_with_whitespace(last_input_text_, e.text)) {
            insert_text(" ");
        }
    }
    return std::nullopt;
}

std::optional<bool> InputEngine::esc_key_handler(const KeyEvent& e) {
    if (e.special == SpecialKey::Escape && utf8_length(original_string_) > 0) {
        clean();
        return true;
    }
    return std::nullopt;
}

std::optional<bool> InputEngine::enter_key_handler(const KeyEvent& e) {
    if (e.special == SpecialKey::Enter && utf8_length(original_string_) > 0) {
        insert_text(original_string_);
        return true;
    }
    return std::nullopt;
}

std::optional<bool> InputEngine::space_key_handler(const KeyEvent& e) {
    if (e.special == SpecialKey::Space && utf8_length(original_string_) > 0) {
        if (config_.z_key_query && input_mode_ == InputMode::ZhHans && original_string_ == "`") {
            auto conv = punctuation_.conversion("`");
            insert_text(conv.value_or("·"));
            return true;
        }
        if (!candidates_.empty()) {
            insert_candidate(candidates_.front());
        }
        return true;
    }
    return std::nullopt;
}

std::optional<bool> InputEngine::punctuation_key_handler(const KeyEvent& e) {
    const std::string& string = e.text;
    if (input_mode_ != InputMode::ZhHans) return std::nullopt;
    if (string.empty()) return std::nullopt;

    std::string trigger(1, config_.temp_en_trigger);
    bool enter_temp_en =
        (!config_.disable_temp_en_mode && utf8_length(original_string_) <= 0 && string == trigger) ||
        (string != trigger && !original_string_.empty() &&
         original_string_[0] == config_.temp_en_trigger);
    if (enter_temp_en) {
        set_original_string(original_string_ + string);
        return true;
    }

    auto result = punctuation_.conversion(string);
    if (result) {
        if (config_.enable_punctuation_commit && utf8_length(original_string_) > 0 &&
            !candidates_.empty() && candidates_.front().type != CandidateType::Placeholder) {
            Candidate first = candidates_.front();
            remember_dynamic_frequency_if_needed(first);
            std::string committed = first.text + *result;
            CandidateInsertedInfo info;
            info.candidate = first;
            info.app_id = client_.bundle_id();
            insert_text(committed);
            if (on_candidate_inserted_) on_candidate_inserted_(info);
            return true;
        }
        insert_text(*result);
        return true;
    }
    return std::nullopt;
}

// ---- 防抖：同一按键在 20ms 内再次触发 → 丢弃（防电气抖动/硬件误触） ----
// 比较 text+special 确定按键身份；不影响正常打字（阈值低于 Windows 最快自动重复间隔 ~33ms）。
bool InputEngine::debounce_key(const KeyEvent& e) {
    auto now = std::chrono::steady_clock::now();
    if (e.text == last_key_text_ && e.special == last_key_special_ &&
        (now - last_key_time_) < kKeyDebounceInterval) {
        return true;  // 丢弃
    }
    last_key_text_ = e.text;
    last_key_special_ = e.special;
    last_key_time_ = now;
    return false;
}

// ---- 主入口，对应 handle(_:client:) ----
bool InputEngine::handle_key(const KeyEvent& e) {
    // 防抖：同一按键短时间内重复触发 → 消费掉，不进入引擎状态机
    if (debounce_key(e)) {
        return true;
    }

    using H = std::optional<bool> (InputEngine::*)(const KeyEvent&);
    static const H handlers[] = {
        &InputEngine::hotkey_handler,      &InputEngine::caps_lock_handler,
        &InputEngine::flag_changed_handler, &InputEngine::en_mode_handler,
        &InputEngine::predictor_handler,   &InputEngine::page_key_handler,
        &InputEngine::delete_key_handler,  &InputEngine::wubi52_ding_handler,
        &InputEngine::wubi53_ding_handler, &InputEngine::wubi35_ding_handler,
        &InputEngine::char_key_handler,    &InputEngine::number_key_handler,
        &InputEngine::esc_key_handler,     &InputEngine::enter_key_handler,
        &InputEngine::space_key_handler,   &InputEngine::punctuation_key_handler,
    };
    for (H h : handlers) {
        std::optional<bool> r = (this->*h)(e);
        if (r.has_value()) {
            return r.value();
        }
    }
    return false;
}

}  // namespace fire
