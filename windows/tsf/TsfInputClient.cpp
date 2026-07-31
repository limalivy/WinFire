//
//  TsfInputClient.cpp
//
#include "TsfInputClient.h"
#include "DebugLog.h"
#include "KeyEventTranslator.h"
#include "../candidate_window/CandidateWindow.h"

#include <msctf.h>

namespace firewin {

// 在组字区写文本：用 ITfInsertAtSelection 插入，并对新范围设置组字显示属性。
void TsfInputClient::SetCompositionText(const std::wstring& text) {
    FIRE_LOG(L"[WinFire] SetCompositionText: pContext=%p text_len=%zu composition=%p\n",
             (void*)session_.pContext, text.size(), (void*)composition_);
    if (!session_.pContext) return;

    ITfInsertAtSelection* pInsert = nullptr;
    if (FAILED(session_.pContext->QueryInterface(IID_ITfInsertAtSelection, (void**)&pInsert)))
        return;

    ITfRange* pRange = nullptr;
    // 若尚未开始组字，先创建 composition
    if (!composition_) {
        // 用非 QUERYONLY 方式真正插入初始文本，得到一个非零长度范围再开始组字，
        // 避免部分宿主在零长度组字范围上丢失首字符 / 不显示组字区。
        HRESULT hr = pInsert->InsertTextAtSelection(session_.editCookie, 0, text.c_str(),
                                                     (LONG)text.size(), &pRange);
        FIRE_LOG_HR(hr, L"InsertTextAtSelection");
        if (SUCCEEDED(hr)) {
            ITfContextComposition* pCtxComp = nullptr;
            if (SUCCEEDED(session_.pContext->QueryInterface(IID_ITfContextComposition,
                                                            (void**)&pCtxComp))) {
                // 传入 TextService 提供的 ITfCompositionSink，使宿主主动终止组字时能回调
                // OnCompositionTerminated，避免 composition_ 悬空、引擎与文档状态失步。
                hr = pCtxComp->StartComposition(session_.editCookie, pRange, compositionSink_,
                                           &composition_);
                FIRE_LOG_HR(hr, L"StartComposition");
                pCtxComp->Release();
            }
            // 初始文本已插入，后续 SetText 会覆盖为最新组字串。
        }
    } else {
        composition_->GetRange(&pRange);
    }

    if (pRange) {
        HRESULT hr = pRange->SetText(session_.editCookie, 0, text.c_str(), (LONG)text.size());
        FIRE_LOG_HR(hr, L"SetText");
        // 重新锚定 range 使其覆盖完整的新文本。
        // 部分 TSF 文本 store（如 Explorer 搜索框）在 SetText 后不会自动
        // 扩展 range 长度，导致后续 Collapse / SetSelection 定在错误位置。
        pRange->Collapse(session_.editCookie, TF_ANCHOR_START);
        LONG moved = 0;
        pRange->ShiftEnd(session_.editCookie, (LONG)text.size(), &moved, nullptr);
        // 更新选区到组字区末尾
        TF_SELECTION sel;
        sel.range = pRange;
        sel.style.ase = TF_AE_END;
        sel.style.fInterimChar = FALSE;
        pRange->Collapse(session_.editCookie, TF_ANCHOR_END);
        session_.pContext->SetSelection(session_.editCookie, 1, &sel);
        pRange->Release();
    }
    pInsert->Release();
}

void TsfInputClient::EndCompositionAndCommit(const std::wstring& text) {
    FIRE_LOG(L"[WinFire] EndCompositionAndCommit: pContext=%p composition=%p text_len=%zu\n",
             (void*)session_.pContext, (void*)composition_, text.size());
    if (!session_.pContext) return;
    ITfRange* pRange = nullptr;
    if (composition_ && SUCCEEDED(composition_->GetRange(&pRange)) && pRange) {
        pRange->SetText(session_.editCookie, 0, text.c_str(), (LONG)text.size());
        // 重新锚定 range 覆盖完整文本（同 SetCompositionText 的说明）
        pRange->Collapse(session_.editCookie, TF_ANCHOR_START);
        LONG moved = 0;
        pRange->ShiftEnd(session_.editCookie, (LONG)text.size(), &moved, nullptr);
        // 在 EndComposition 前克隆光标位置。Explorer 搜索框等控件在
        // EndComposition 后不会自动将光标移到组字文本末尾，必须显式 SetSelection。
        ITfRange* pCaret = nullptr;
        pRange->Clone(&pCaret);
        pCaret->Collapse(session_.editCookie, TF_ANCHOR_END);
        pRange->Release();
        composition_->EndComposition(session_.editCookie);
        composition_->Release();
        composition_ = nullptr;
        // 显式定位光标到上屏文本末尾
        TF_SELECTION sel;
        sel.range = pCaret;
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        session_.pContext->SetSelection(session_.editCookie, 1, &sel);
        pCaret->Release();
        FIRE_LOG(L"[WinFire] EndCompositionAndCommit: composition ended OK\n");
    } else if (!text.empty()) {
        // 无组字：直接在选区插入
        ITfInsertAtSelection* pInsert = nullptr;
        if (SUCCEEDED(session_.pContext->QueryInterface(IID_ITfInsertAtSelection,
                                                        (void**)&pInsert))) {
            ITfRange* r = nullptr;
            pInsert->InsertTextAtSelection(session_.editCookie, 0, text.c_str(),
                                           (LONG)text.size(), &r);
            if (r) {
                // 显式定位光标到插入文本末尾（同上原因）
                r->Collapse(session_.editCookie, TF_ANCHOR_END);
                TF_SELECTION sel2;
                sel2.range = r;
                sel2.style.ase = TF_AE_NONE;
                sel2.style.fInterimChar = FALSE;
                session_.pContext->SetSelection(session_.editCookie, 1, &sel2);
                r->Release();
            }
            pInsert->Release();
            FIRE_LOG(L"[WinFire] EndCompositionAndCommit: InsertTextAtSelection (no composition)\n");
        }
    }
}

void TsfInputClient::insert_text(const std::string& utf8) {
    FIRE_LOG(L"[WinFire] insert_text: '%hs'\n", utf8.c_str());
    std::wstring w = KeyEventTranslator::Utf8ToUtf16(utf8);
    EndCompositionAndCommit(w);
    hide_candidates();
}

void TsfInputClient::set_marked_text(const std::string& utf8) {
    FIRE_LOG(L"[WinFire] set_marked_text: '%hs'\n", utf8.c_str());
    std::wstring w = KeyEventTranslator::Utf8ToUtf16(utf8);
    SetCompositionText(w);
}

void TsfInputClient::clear_marked_text() {
    FIRE_LOG(L"[WinFire] clear_marked_text: composition=%p\n", (void*)composition_);
    if (composition_) {
        SetCompositionText(L"");
        composition_->EndComposition(session_.editCookie);
        composition_->Release();
        composition_ = nullptr;
    }
}

fire::CaretRect TsfInputClient::get_caret_rect() {
    FIRE_LOG_ENTER();
    fire::CaretRect rc;
    if (!session_.pContext) {
        FIRE_LOG(L"[WinFire] get_caret_rect: pContext null\n");
        return rc;
    }

    ITfContextView* pView = nullptr;
    if (FAILED(session_.pContext->GetActiveView(&pView)) || !pView) {
        FIRE_LOG(L"[WinFire] get_caret_rect: GetActiveView FAILED/null\n");
        return rc;
    }

    ITfRange* pRange = nullptr;
    if (composition_) {
        composition_->GetRange(&pRange);
        FIRE_LOG(L"[WinFire] get_caret_rect: from composition range=%p\n", (void*)pRange);
    } else {
        TF_SELECTION sel;
        ULONG fetched = 0;
        if (SUCCEEDED(session_.pContext->GetSelection(session_.editCookie, TF_DEFAULT_SELECTION,
                                                      1, &sel, &fetched)) && fetched) {
            pRange = sel.range;
            FIRE_LOG(L"[WinFire] get_caret_rect: from selection range=%p\n", (void*)pRange);
        }
    }
    if (pRange) {
        RECT r = {0};
        BOOL clipped = FALSE;
        HRESULT hr = pView->GetTextExt(session_.editCookie, pRange, &r, &clipped);
        FIRE_LOG_HR(hr, L"GetTextExt");
        if (SUCCEEDED(hr) && !(r.left == 0 && r.top == 0 && r.right == 0 && r.bottom == 0)) {
            rc.x = r.left;
            rc.y = r.top;
            rc.width = r.right - r.left;
            rc.height = r.bottom - r.top;
            lastValidCaret_ = rc;  // 缓存有效坐标，供后续回退
            FIRE_LOG(L"[WinFire] get_caret_rect: rect=(%d,%d,%dx%d) clipped=%d\n",
                     rc.x, rc.y, rc.width, rc.height, clipped ? 1 : 0);
        } else {
            // GetTextExt 失败或返回全零矩形（顶字自动上屏瞬间组字区过渡态常见）：
            // 回退到上次有效坐标，避免候选窗定位到屏幕左上角 (0,0)。
            FIRE_LOG(L"[WinFire] get_caret_rect: GetTextExt unavailable, fallback to last valid\n");
            rc = lastValidCaret_;
        }
        pRange->Release();
    }
    pView->Release();
    FIRE_LOG_EXIT();
    return rc;
}

std::string TsfInputClient::get_previous_text() {
    FIRE_LOG_ENTER();
    // 读取光标前一个字符：从选区起点向前扩展 1 个字符再取文本。
    if (!session_.pContext) {
        FIRE_LOG(L"[WinFire] get_previous_text: pContext null\n");
        return {};
    }
    TF_SELECTION sel;
    ULONG fetched = 0;
    if (FAILED(session_.pContext->GetSelection(session_.editCookie, TF_DEFAULT_SELECTION,
                                               1, &sel, &fetched)) || !fetched) {
        FIRE_LOG(L"[WinFire] get_previous_text: GetSelection failed/no fetch\n");
        return {};
    }
    std::string result;
    ITfRange* pRange = sel.range;
    // 先回退 2 个 UTF-16 code unit，覆盖代理对（emoji/生僻字）；读取后若首个单元
    // 是低代理项（承接了半个代理对），说明前面还有一个高代理项被截断，此时丢弃避免非法字节。
    LONG shifted = 0;
    if (SUCCEEDED(pRange->ShiftStart(session_.editCookie, -2, &shifted, nullptr)) && shifted != 0) {
        WCHAR buf[8] = {0};
        ULONG cch = 0;
        if (SUCCEEDED(pRange->GetText(session_.editCookie, 0, buf, 4, &cch)) && cch > 0) {
            std::wstring w(buf, cch);
            // 若开头是孤立的低代理项，去掉它（回退越过了字符边界）
            if (!w.empty() && w[0] >= 0xDC00 && w[0] <= 0xDFFF) {
                w.erase(w.begin());
            }
            // 只保留最后一个完整字符（一个 BMP 字符或一个代理对）
            if (w.size() >= 2 && w[w.size() - 2] >= 0xD800 && w[w.size() - 2] <= 0xDBFF &&
                w[w.size() - 1] >= 0xDC00 && w[w.size() - 1] <= 0xDFFF) {
                w = w.substr(w.size() - 2);
            } else if (!w.empty()) {
                w = w.substr(w.size() - 1);
            }
            result = KeyEventTranslator::Utf16ToUtf8(w);
        }
    }
    pRange->Release();
    FIRE_LOG(L"[WinFire] get_previous_text: result='%hs'\n", result.c_str());
    FIRE_LOG_EXIT();
    return result;
}

std::string TsfInputClient::bundle_id() {
    FIRE_LOG_ENTER();
    // 用宿主进程的可执行名作为“应用标识”（对应 macOS 的 bundleIdentifier）
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring w(path);
    size_t pos = w.find_last_of(L"\\/");
    if (pos != std::wstring::npos) w = w.substr(pos + 1);
    std::string id = KeyEventTranslator::Utf16ToUtf8(w);
    FIRE_LOG(L"[WinFire] bundle_id: '%hs'\n", id.c_str());
    FIRE_LOG_EXIT();
    return id;
}

void TsfInputClient::show_candidates(const fire::CandidatesView& view) {
    FIRE_LOG(L"[WinFire] show_candidates: cand=%p list_size=%zu origin='%hs'\n",
             (void*)candWindow_, view.list.size(), view.original_string.c_str());
    if (candWindow_) candWindow_->Show(view);
}

void TsfInputClient::hide_candidates() {
    FIRE_LOG(L"[WinFire] hide_candidates: cand=%p\n", (void*)candWindow_);
    if (candWindow_) candWindow_->Hide();
}

void TsfInputClient::show_input_mode_toast(const std::string& label) {
    FIRE_LOG(L"[WinFire] show_input_mode_toast: label='%hs' cand=%p\n",
             label.c_str(), (void*)candWindow_);
    if (!candWindow_) return;
    // 取新鲜光标供提示定位。get_caret_rect() 内部有完整降级链：
    //   实时光标（编辑会话内，Shift 单击切换的常见情形）
    //   → lastValidCaret_ 缓存（本会话曾显示过候选即有值）
    //   → {0,0,0,0}（全新会话未显示过候选，由 ComputePosition 的兜底处理）
    // 这样避免拷贝陈旧 view_.caret 把提示钉到屏幕左上角。
    fire::CaretRect caret = get_caret_rect();
    candWindow_->ShowToast(label, caret);
}

}  // namespace firewin
