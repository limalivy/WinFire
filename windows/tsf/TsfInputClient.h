//
//  TsfInputClient.h — 实现 fire::InputClient，桥接内核与 TSF 组字/上屏/候选窗
//
#pragma once

#include <windows.h>
#include <msctf.h>
#include <string>

#include "fire/input_client.h"

namespace firewin {

class CandidateWindowController;  // 前置声明（候选窗控制器）

// 承载“一次编辑会话”所需的 TSF 上下文。TextService 在处理按键时把当前
// edit cookie / context 注入进来，供组字与上屏调用。
struct TsfEditSession {
    ITfContext* pContext = nullptr;
    TfEditCookie editCookie = 0;
    TfClientId clientId = 0;
};

class TsfInputClient : public fire::InputClient {
public:
    TsfInputClient() = default;

    // 绑定候选窗控制器（可选）
    void SetCandidateWindow(CandidateWindowController* w) { candWindow_ = w; }

    // 每次进入 EditSession 时更新
    void SetEditContext(const TsfEditSession& s) { session_ = s; }

    // 组合对象（由 TextService 在开始/结束组字时管理）
    void SetComposition(ITfComposition* comp) { composition_ = comp; }
    ITfComposition* Composition() const { return composition_; }

    // 组字终止回调 sink（由 TextService 提供，实现 ITfCompositionSink）
    void SetCompositionSink(ITfCompositionSink* sink) { compositionSink_ = sink; }

    // ---- fire::InputClient ----
    void insert_text(const std::string& utf8) override;
    void set_marked_text(const std::string& utf8) override;
    void clear_marked_text() override;
    fire::CaretRect get_caret_rect() override;
    std::string get_previous_text() override;
    std::string bundle_id() override;
    void show_candidates(const fire::CandidatesView& view) override;
    void hide_candidates() override;
    void show_input_mode_toast(const std::string& label) override;

    // 供 TextService 查询是否需要结束组字（marked 清空且无候选）
    bool HasComposition() const { return composition_ != nullptr; }

private:
    TsfEditSession session_;
    ITfComposition* composition_ = nullptr;   // 不持有强引用生命周期由 TextService 管理
    ITfCompositionSink* compositionSink_ = nullptr;  // 组字终止回调（由 TextService 注入，不持有强引用）
    CandidateWindowController* candWindow_ = nullptr;

    // 上一次有效光标坐标缓存。顶字自动上屏瞬间，组字区/选区正处于过渡态，
    // GetTextExt 可能失败或返回 {0,0,0,0}，导致候选窗定位回退到屏幕左上角。
    // 此时回退到此缓存值，保持候选窗在原位置附近刷新。
    fire::CaretRect lastValidCaret_;

    // 在组字区写入文本（UTF-8）。空串表示清空组字区。
    void SetCompositionText(const std::wstring& text);
    // 结束组字并把文本落地到宿主
    void EndCompositionAndCommit(const std::wstring& text);
};

}  // namespace firewin
