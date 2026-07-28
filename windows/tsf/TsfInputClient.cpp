//
//  TsfInputClient.cpp
//
#include "TsfInputClient.h"
#include "KeyEventTranslator.h"
#include "../candidate_window/CandidateWindow.h"

#include <msctf.h>

namespace firewin {

// 在组字区写文本：用 ITfInsertAtSelection 插入，并对新范围设置组字显示属性。
void TsfInputClient::SetCompositionText(const std::wstring& text) {
    if (!session_.pContext) return;

    ITfInsertAtSelection* pInsert = nullptr;
    if (FAILED(session_.pContext->QueryInterface(IID_ITfInsertAtSelection, (void**)&pInsert)))
        return;

    ITfRange* pRange = nullptr;
    // 若尚未开始组字，先创建 composition
    if (!composition_) {
        // 用非 QUERYONLY 方式真正插入初始文本，得到一个非零长度范围再开始组字，
        // 避免部分宿主在零长度组字范围上丢失首字符 / 不显示组字区。
        if (SUCCEEDED(pInsert->InsertTextAtSelection(session_.editCookie, 0, text.c_str(),
                                                     (LONG)text.size(), &pRange))) {
            ITfContextComposition* pCtxComp = nullptr;
            if (SUCCEEDED(session_.pContext->QueryInterface(IID_ITfContextComposition,
                                                            (void**)&pCtxComp))) {
                // 传入 TextService 提供的 ITfCompositionSink，使宿主主动终止组字时能回调
                // OnCompositionTerminated，避免 composition_ 悬空、引擎与文档状态失步。
                pCtxComp->StartComposition(session_.editCookie, pRange, compositionSink_,
                                           &composition_);
                pCtxComp->Release();
            }
            // 初始文本已插入，后续 SetText 会覆盖为最新组字串。
        }
    } else {
        composition_->GetRange(&pRange);
    }

    if (pRange) {
        pRange->SetText(session_.editCookie, 0, text.c_str(), (LONG)text.size());
        // TODO: 用 ITfProperty(GUID_PROP_ATTRIBUTE) + GUID_FireDisplayAttributeInput 设置下划线高亮
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
    if (!session_.pContext) return;
    ITfRange* pRange = nullptr;
    if (composition_ && SUCCEEDED(composition_->GetRange(&pRange)) && pRange) {
        pRange->SetText(session_.editCookie, 0, text.c_str(), (LONG)text.size());
        pRange->Collapse(session_.editCookie, TF_ANCHOR_END);
        pRange->Release();
        composition_->EndComposition(session_.editCookie);
        composition_->Release();
        composition_ = nullptr;
    } else if (!text.empty()) {
        // 无组字：直接在选区插入
        ITfInsertAtSelection* pInsert = nullptr;
        if (SUCCEEDED(session_.pContext->QueryInterface(IID_ITfInsertAtSelection,
                                                        (void**)&pInsert))) {
            ITfRange* r = nullptr;
            pInsert->InsertTextAtSelection(session_.editCookie, 0, text.c_str(),
                                           (LONG)text.size(), &r);
            if (r) r->Release();
            pInsert->Release();
        }
    }
}

void TsfInputClient::insert_text(const std::string& utf8) {
    std::wstring w = KeyEventTranslator::Utf8ToUtf16(utf8);
    EndCompositionAndCommit(w);
    hide_candidates();
}

void TsfInputClient::set_marked_text(const std::string& utf8) {
    std::wstring w = KeyEventTranslator::Utf8ToUtf16(utf8);
    SetCompositionText(w);
}

void TsfInputClient::clear_marked_text() {
    if (composition_) {
        SetCompositionText(L"");
        composition_->EndComposition(session_.editCookie);
        composition_->Release();
        composition_ = nullptr;
    }
}

fire::CaretRect TsfInputClient::get_caret_rect() {
    fire::CaretRect rc;
    if (!session_.pContext) return rc;

    ITfContextView* pView = nullptr;
    if (FAILED(session_.pContext->GetActiveView(&pView)) || !pView) return rc;

    ITfRange* pRange = nullptr;
    if (composition_) {
        composition_->GetRange(&pRange);
    } else {
        TF_SELECTION sel;
        ULONG fetched = 0;
        if (SUCCEEDED(session_.pContext->GetSelection(session_.editCookie, TF_DEFAULT_SELECTION,
                                                      1, &sel, &fetched)) && fetched) {
            pRange = sel.range;
        }
    }
    if (pRange) {
        RECT r = {0};
        BOOL clipped = FALSE;
        if (SUCCEEDED(pView->GetTextExt(session_.editCookie, pRange, &r, &clipped))) {
            rc.x = r.left;
            rc.y = r.top;
            rc.width = r.right - r.left;
            rc.height = r.bottom - r.top;
        }
        pRange->Release();
    }
    pView->Release();
    return rc;
}

std::string TsfInputClient::get_previous_text() {
    // 读取光标前一个字符：从选区起点向前扩展 1 个字符再取文本。
    if (!session_.pContext) return {};
    TF_SELECTION sel;
    ULONG fetched = 0;
    if (FAILED(session_.pContext->GetSelection(session_.editCookie, TF_DEFAULT_SELECTION,
                                               1, &sel, &fetched)) || !fetched) {
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
    return result;
}

std::string TsfInputClient::bundle_id() {
    // 用宿主进程的可执行名作为“应用标识”（对应 macOS 的 bundleIdentifier）
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring w(path);
    size_t pos = w.find_last_of(L"\\/");
    if (pos != std::wstring::npos) w = w.substr(pos + 1);
    return KeyEventTranslator::Utf16ToUtf8(w);
}

void TsfInputClient::show_candidates(const fire::CandidatesView& view) {
    if (candWindow_) candWindow_->Show(view);
}

void TsfInputClient::hide_candidates() {
    if (candWindow_) candWindow_->Hide();
}

void TsfInputClient::show_input_mode_toast(const std::string& label) {
    if (candWindow_) candWindow_->ShowToast(label);
}

}  // namespace firewin
