//
//  input_client.h — 宿主交互抽象，对应 IMK 的 client()
//  TSF 层实现该接口。
//
#pragma once

#include <string>
#include <vector>

#include "fire/candidate.h"

namespace fire {

// 屏幕坐标下的光标矩形（用于候选窗定位）
struct CaretRect {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
};

// 候选窗展示数据
struct CandidatesView {
    std::vector<Candidate> list;
    std::string original_string;  // 组字区展示串（可能含 35 顶占位 "_"）
    bool has_prev = false;
    bool has_next = false;
    CaretRect caret;              // 定位锚点
};

// 宿主交互接口，对应 FireInputController 中对 client() 的调用集合。
class InputClient {
public:
    virtual ~InputClient() = default;

    // 上屏文本，对应 client().insertText
    virtual void insert_text(const std::string& utf8) = 0;

    // 设置组字区（预编辑）文本，对应 setMarkedText
    virtual void set_marked_text(const std::string& utf8) = 0;

    // 清空组字区
    virtual void clear_marked_text() = 0;

    // 获取光标屏幕矩形，对应 getOriginPoint / attributes(forCharacterIndex:)
    virtual CaretRect get_caret_rect() = 0;

    // 获取光标前一个字符，对应 getPreviousText，用于中英文间加空格判断
    virtual std::string get_previous_text() = 0;

    // 宿主应用标识，对应 client().bundleIdentifier()
    virtual std::string bundle_id() = 0;

    // 显示 / 隐藏候选窗
    virtual void show_candidates(const CandidatesView& view) = 0;
    virtual void hide_candidates() = 0;

    // 中英文切换提示（对应 toast）
    virtual void show_input_mode_toast(const std::string& label) {}
};

}  // namespace fire
