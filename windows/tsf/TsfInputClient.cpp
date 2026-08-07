//
//  TsfInputClient.cpp
//
#include "TsfInputClient.h"
#include "DebugLog.h"
#include "KeyEventTranslator.h"
#include "../candidate_window/CandidateWindow.h"

#include <msctf.h>
#include <algorithm>
#include <cctype>

namespace firewin {

// 在组字区写文本：用 ITfInsertAtSelection 插入，并对新范围设置组字显示属性。
void TsfInputClient::SetCompositionText(const std::wstring& text) {
    // 字节级转储写入组字区的文本：看占位空格 ' '(0x20) 是否写入、长度是否为 1。
    std::string hex;
    hex.reserve(text.size() * 3);
    for (wchar_t wc : text) {
        // 只看 BMP code unit（输入法组字区不会有代理对）
        char b[8];
        _snprintf_s(b, _countof(b), _TRUNCATE, "U+%04X ", (unsigned)wc);
        hex += b;
    }
    FIRE_LOG(L"[WinFire] SetCompositionText: pContext=%p text_len=%zu composition=%p codeunits=[%hs]\n",
             (void*)session_.pContext, text.size(), (void*)composition_, hex.c_str());
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
        // 用 TF_ST_CORRECTION 标记占位文本（如 show_code_in_window 时的占位空格），
        // 语义为「修正既有内容」而非「新建内容」，抑制开始菜单/Explorer 搜索框等
        // 自带预测的宿主把它当成新输入而触发输入联想（参考 weasel Composition.cpp）。
        HRESULT hr = pRange->SetText(session_.editCookie, TF_ST_CORRECTION, text.c_str(),
                                     (LONG)text.size());
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
    // 转储待提交文本的 code unit，确认是否带空格(0x0020)。
    std::string hex;
    hex.reserve(text.size() * 7);
    for (wchar_t wc : text) {
        char b[8];
        _snprintf_s(b, _countof(b), _TRUNCATE, "U+%04X ", (unsigned)wc);
        hex += b;
    }
    FIRE_LOG(L"[WinFire] EndCompositionAndCommit: pContext=%p composition=%p text_len=%zu codeunits=[%hs]\n",
             (void*)session_.pContext, (void*)composition_, text.size(), hex.c_str());
    if (!session_.pContext) return;
    ITfRange* pRange = nullptr;
    if (composition_ && SUCCEEDED(composition_->GetRange(&pRange)) && pRange) {
        // 在组字 range 上原地用最终文本覆盖占位空格，然后 EndComposition（不清空文本）。
        //
        // 背景：曾用「先 SetText("") 清空 → EndComposition → InsertTextAtSelection
        // 重新插入」的三步法。但开始菜单搜索框（SearchHost.exe）/ Explorer 搜索框等
        // 自带「内联搜索预测/输入联想」的宿主，会把组字结束之后那次 InsertTextAtSelection
        // 当作「新用户输入」并据此补全预测——导致上屏后被预测内容吞掉
        //（如输入「一半」再按空格变成「一半海水一半火焰」），且占位空格未被干净移除
        // 残留成尾随空格（「一半 」）。
        //
        // 改为 weasel 验证过的模式：在组字 range 上 SetText(最终文本) 原地替换占位符，
        // 再 EndComposition（不清空，最终文本原地留在文档），不做组字结束后的 re-insert。
        // 这样提交对宿主是一次「修正既有内容」而非「新输入」，不触发预测/补全。
        // console 类宿主在 set_marked_text 已跳过组字，恒走下面的「无组字」分支，不受影响。
        HRESULT hrSet = pRange->SetText(session_.editCookie, TF_ST_CORRECTION,
                                        text.c_str(), (LONG)text.size());
        FIRE_LOG_HR(hrSet, L"EndCompositionAndCommit: SetText(final, in-place)");
        // 组字 range 在 SetText 后需重新锚定覆盖完整新文本（部分 TSF text store，
        // 如 Explorer 搜索框，SetText 后不自动扩展 range），再把光标定位到末尾。
        pRange->Collapse(session_.editCookie, TF_ANCHOR_START);
        LONG moved = 0;
        pRange->ShiftEnd(session_.editCookie, (LONG)text.size(), &moved, nullptr);
        pRange->Collapse(session_.editCookie, TF_ANCHOR_END);
        TF_SELECTION sel;
        sel.range = pRange;
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        session_.pContext->SetSelection(session_.editCookie, 1, &sel);
        pRange->Release();
        HRESULT hrEnd = composition_->EndComposition(session_.editCookie);
        FIRE_LOG_HR(hrEnd, L"EndCompositionAndCommit: EndComposition");
        composition_->Release();
        composition_ = nullptr;
        FIRE_LOG(L"[WinFire] EndCompositionAndCommit: committed in-place & ended\n");
    } else if (!text.empty()) {
        // 无组字：直接在选区插入
        FIRE_LOG(L"[WinFire] EndCompositionAndCommit: NO composition, InsertTextAtSelection\n");
        ITfInsertAtSelection* pInsert = nullptr;
        if (SUCCEEDED(session_.pContext->QueryInterface(IID_ITfInsertAtSelection,
                                                        (void**)&pInsert))) {
            ITfRange* r = nullptr;
            HRESULT hrIns = pInsert->InsertTextAtSelection(session_.editCookie, 0, text.c_str(),
                                                           (LONG)text.size(), &r);
            FIRE_LOG_HR(hrIns, L"EndCompositionAndCommit: InsertTextAtSelection(no-composition)");
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
        }
    }
}

void TsfInputClient::insert_text(const std::string& utf8) {
    // 字节级十六进制转储：区分 ASCII 空格(0x20)与全角空格(E3 80 80)，定位追加空格来源。
    // 仅 Debug 下生效，便于 DbgView 分析。
    std::string hex;
    hex.reserve(utf8.size() * 3);
    for (unsigned char c : utf8) {
        char b[4];
        _snprintf_s(b, _countof(b), _TRUNCATE, "%02X ", c);
        hex += b;
    }
    FIRE_LOG(L"[WinFire] insert_text: '%hs' (len=%zu bytes hex=[%hs])\n",
             utf8.c_str(), utf8.size(), hex.c_str());
    std::wstring w = KeyEventTranslator::Utf8ToUtf16(utf8);
    EndCompositionAndCommit(w);
    hide_candidates();
}

void TsfInputClient::set_marked_text(const std::string& utf8) {
    // console 类宿主（conhost/WindowsTerminal 等）的 TSF text store 与控制台输入行
    //（ReadConsole 输入缓冲）耦合：写入组字区的内容会立即进入输入行，且在提交时
    // 无法撤销。show_code_in_window 开启时引擎写入的占位空格 ' ' 会被控制台吸收，
    // 提交后残留成尾随空格（上屏变成「我 」）。
    //
    // 故对 console 宿主完全不写 preedit 文本——composition_ 全程保持 null，
    // 提交走「无组字」分支（直接 InsertTextAtSelection，无占位符可残留）。
    // 候选窗本就用兜底定位（无光标信息时居中 2/3），不依赖组字区锚点。
    if (IsConsoleHost()) {
        FIRE_LOG(L"[WinFire] set_marked_text: '%hs' SKIPPED (console host)\n", utf8.c_str());
        return;
    }
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
    // 字节级十六进制：看返回值是否为占位空格 ' '(0x20)，它会触发 should_concat_with_whitespace。
    std::string hex;
    hex.reserve(result.size() * 3);
    for (unsigned char c : result) {
        char b[4];
        _snprintf_s(b, _countof(b), _TRUNCATE, "%02X ", c);
        hex += b;
    }
    FIRE_LOG(L"[WinFire] get_previous_text: result='%hs' (len=%zu bytes hex=[%hs])\n",
             result.c_str(), result.size(), hex.c_str());
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

bool TsfInputClient::IsConsoleHost() const {
    if (consoleHostCached_ != -1) return consoleHostCached_ == 1;
    // bundle_id() 非常量（有 FIRE_LOG），此处用 const_cast 规避——读取无副作用。
    // 直接取宿主 exe 名（避免递归到带日志的 bundle_id）。
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring w(path);
    size_t pos = w.find_last_of(L"\\/");
    if (pos != std::wstring::npos) w = w.substr(pos + 1);
    // 转小写比较
    std::string name = KeyEventTranslator::Utf16ToUtf8(w);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    // 控制台类宿主名单：TSF text store 与控制台输入行耦合，
    // 写入组字区的内容会立即进入输入行且提交时无法撤销。
    static const char* kConsoleHosts[] = {
        "conhost.exe",       // Windows 经典控制台宿主（已由日志确认）
        "openconsole.exe",   // Windows Terminal 的 conhost 替身
        "windowsterminal.exe",
        "windowterminal.exe",
        "cmd.exe",
        "powershell.exe",    // Windows PowerShell
        "pwsh.exe",          // PowerShell 7+
    };
    bool matched = false;
    for (const char* h : kConsoleHosts) {
        if (name == h) { matched = true; break; }
    }
    consoleHostCached_ = matched ? 1 : 0;
    FIRE_LOG(L"[WinFire] IsConsoleHost: host='%hs' -> %d\n", name.c_str(), matched ? 1 : 0);
    return matched;
}

void TsfInputClient::show_candidates(const fire::CandidatesView& view) {
    FIRE_LOG(L"[WinFire] show_candidates: cand=%p list_size=%zu origin='%hs'\n",
             (void*)candWindow_, view.list.size(), view.original_string.c_str());
    if (candWindow_) {
        // SearchHost.exe/UWP 等沙箱宿主：候选窗必须以宿主活动视图窗口为 owner 才可见。
        // 每次显示前刷新 owner（跨应用切换时 context 变化，owner 需更新）。
        HWND owner = GetActiveViewWnd();
        if (owner) candWindow_->Reparent(owner);
        candWindow_->Show(view);
    }
}

HWND TsfInputClient::GetActiveViewWnd() {
    // 取宿主活动视图窗口，供候选窗作 owner。失败回退 ::GetFocus()。
    // 调用链与 get_caret_rect 一致：session_.pContext -> GetActiveView -> GetWnd。
    if (!session_.pContext) {
        HWND fw = ::GetFocus();
        FIRE_LOG(L"[WinFire] GetActiveViewWnd: pContext null, fallback GetFocus=%p\n", (void*)fw);
        return fw;
    }
    ITfContextView* pView = nullptr;
    if (FAILED(session_.pContext->GetActiveView(&pView)) || !pView) {
        HWND fw = ::GetFocus();
        FIRE_LOG(L"[WinFire] GetActiveViewWnd: GetActiveView failed/null, fallback GetFocus=%p\n", (void*)fw);
        return fw;
    }
    HWND wnd = nullptr;
    HRESULT hr = pView->GetWnd(&wnd);
    pView->Release();
    if (FAILED(hr) || !wnd) {
        HWND fw = ::GetFocus();
        FIRE_LOG(L"[WinFire] GetActiveViewWnd: GetWnd hr=0x%lX null, fallback GetFocus=%p\n",
                 (unsigned long)hr, (void*)fw);
        return fw;
    }
    FIRE_LOG(L"[WinFire] GetActiveViewWnd: view wnd=%p\n", (void*)wnd);
    return wnd;
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
