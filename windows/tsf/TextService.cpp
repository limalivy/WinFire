//
//  TextService.cpp — TSF TIP 主实现
//
#include "TextService.h"
#include "LangBarButton.h"
#include "../candidate_window/CandidateWindow.h"
#include "../config/ConfigStore.h"

#include <shlobj.h>
#include <string>

namespace firewin {

// ---- EditSession：所有对文档的读写都必须在 EditSession 内完成 ----
class KeyEditSession : public ITfEditSession {
public:
    KeyEditSession(CFireTextService* svc, ITfContext* ctx, const fire::KeyEvent& ev)
        : svc_(svc), ctx_(ctx), ev_(ev) {
        ctx_->AddRef();
    }
    ~KeyEditSession() { ctx_->Release(); }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
            *ppv = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        TsfEditSession s;
        s.pContext = ctx_;
        s.editCookie = ec;
        s.clientId = svc_->clientId_;
        svc_->inputClient_.SetEditContext(s);
        svc_->inputClient_.SetComposition(svc_->inputClient_.Composition());
        fire::InputMode before = svc_->engine_->input_mode();
        eaten_ = svc_->engine_->handle_key(ev_);
        // 输入模式若发生变化，刷新语言栏按钮
        if (before != svc_->engine_->input_mode()) svc_->RefreshLangBar();
        return S_OK;
    }

    bool eaten_ = false;

private:
    CFireTextService* svc_;
    ITfContext* ctx_;
    fire::KeyEvent ev_;
    LONG ref_ = 1;
};

// ---- 通用动作 EditSession：候选窗点击/翻页等非按键路径的文档读写包装 ----
// 这些路径（WM_LBUTTONUP / WM_MOUSEWHEEL）不在任何 EditSession 内，若直接改文档会
// 复用上一次按键遗留的、已失效的 editCookie，被 TSF 拒绝导致上屏失败或组字悬空。
class ActionEditSession : public ITfEditSession {
public:
    ActionEditSession(CFireTextService* svc, ITfContext* ctx, std::function<void()> action)
        : svc_(svc), ctx_(ctx), action_(std::move(action)) {
        ctx_->AddRef();
    }
    ~ActionEditSession() { ctx_->Release(); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
            *ppv = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        TsfEditSession s;
        s.pContext = ctx_;
        s.editCookie = ec;
        s.clientId = svc_->clientId_;
        svc_->inputClient_.SetEditContext(s);
        if (action_) action_();
        return S_OK;
    }

private:
    CFireTextService* svc_;
    ITfContext* ctx_;
    std::function<void()> action_;
    LONG ref_ = 1;
};

// ---- CFireTextService ----
CFireTextService::CFireTextService() {
    DllAddRef();
}

CFireTextService::~CFireTextService() {
    DllRelease();
}

void CFireTextService::LoadConfigFromDisk() {
    // 读取 %APPDATA%\FireIME\config.json 的全量配置（不存在则用默认值）
    firecfg::ConfigStore::Load(config_);

    // 词库与统计库路径（若配置未指定则用默认目录）
    wchar_t appdata[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        std::wstring dir = std::wstring(appdata) + L"\\FireIME";
        if (config_.db_path.empty()) {
            config_.db_path = KeyEventTranslator::Utf16ToUtf8(dir + L"\\wb_py_dict.sqlite");
        }
        if (config_.stats_db_path.empty()) {
            config_.stats_db_path = KeyEventTranslator::Utf16ToUtf8(dir + L"\\statistics.sqlite");
        }
    }
    // 标点表：为空时用默认中文标点表
    if (config_.custom_punctuation_settings.empty()) {
        config_.custom_punctuation_settings = fire::default_punctuation();
    }
}

void CFireTextService::InitEngine() {
    LoadConfigFromDisk();
    dict_ = std::make_unique<fire::DictManager>(config_);
    engine_ = std::make_unique<fire::InputEngine>(config_, *dict_, inputClient_);

    // 注入组字终止回调 sink：宿主主动结束组字时经 OnCompositionTerminated 通知本 TIP。
    inputClient_.SetCompositionSink(static_cast<ITfCompositionSink*>(this));

    // 输入统计库（同步写入）。仅在开启任一统计开关时才创建。
    if (config_.enable_statistics || config_.enable_hanzi_frequency_statistics) {
        stats_ = std::make_unique<fire::Statistics>(config_.stats_db_path);
        engine_->set_candidate_inserted_callback([this](const fire::CandidateInsertedInfo& info) {
            if (stats_ && stats_->is_open()) {
                stats_->record_candidate(info.candidate, info.app_id,
                                         info.hanzi_frequency_parts,
                                         config_.enable_statistics,
                                         config_.enable_hanzi_frequency_statistics);
            }
        });
    }

    candWindow_ = std::make_unique<CandidateWindowController>(config_);
    candWindow_->Create(g_hInst);
    inputClient_.SetCandidateWindow(candWindow_.get());

    // 候选窗点击/翻页回调回灌到引擎。鼠标路径不在按键 EditSession 内，必须重新
    // 申请一次读写 EditSession，拿到有效 editCookie 再操作文档，否则会用失效 cookie。
    candWindow_->SetOnSelect([this](const fire::Candidate& c) {
        RunInEditSession([this, c]() { engine_->insert_candidate(c); });
    });
    candWindow_->SetOnPage([this](int delta) {
        RunInEditSession([this, delta]() {
            if (delta > 0) engine_->next_page(); else engine_->prev_page();
        });
    });
}

// ---- ITfTextInputProcessor(Ex) ----
STDMETHODIMP CFireTextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD /*dwFlags*/) {
    return Activate(ptim, tid);
}

STDMETHODIMP CFireTextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    threadMgr_ = ptim;
    clientId_ = tid;

    InitEngine();

    // 注册 ThreadMgrEventSink
    CComPtr<ITfSource> source;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfSource, (void**)&source))) {
        source->AdviseSink(IID_ITfThreadMgrEventSink,
                           static_cast<ITfThreadMgrEventSink*>(this), &threadMgrCookie_);
    }

    // 注册 KeyEventSink
    CComPtr<ITfKeystrokeMgr> keyMgr;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr, (void**)&keyMgr))) {
        keyMgr->AdviseKeyEventSink(clientId_, static_cast<ITfKeyEventSink*>(this), TRUE);
    }

    // 注册语言栏按钮（中/英状态）
    RegisterLangBarButton();
    return S_OK;
}

STDMETHODIMP CFireTextService::Deactivate() {
    UnregisterLangBarButton();

    CComPtr<ITfKeystrokeMgr> keyMgr;
    if (threadMgr_ && SUCCEEDED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr, (void**)&keyMgr))) {
        keyMgr->UnadviseKeyEventSink(clientId_);
    }
    if (threadMgr_ && threadMgrCookie_ != TF_INVALID_COOKIE) {
        CComPtr<ITfSource> source;
        if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfSource, (void**)&source))) {
            source->UnadviseSink(threadMgrCookie_);
        }
        threadMgrCookie_ = TF_INVALID_COOKIE;
    }
    if (candWindow_) candWindow_->Destroy();
    stats_.reset();
    engine_.reset();
    dict_.reset();
    candWindow_.reset();
    threadMgr_.Release();
    clientId_ = TF_CLIENTID_NULL;
    return S_OK;
}

// ---- 语言栏按钮注册/注销 ----
void CFireTextService::RegisterLangBarButton() {
    if (!threadMgr_ || langBar_) return;
    CComPtr<ITfLangBarItemMgr> mgr;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfLangBarItemMgr, (void**)&mgr))) {
        langBar_ = new CFireLangBarButton(this);
        mgr->AddItem(langBar_);
    }
}

void CFireTextService::UnregisterLangBarButton() {
    if (!langBar_) return;
    if (threadMgr_) {
        CComPtr<ITfLangBarItemMgr> mgr;
        if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfLangBarItemMgr, (void**)&mgr))) {
            mgr->RemoveItem(langBar_);
        }
    }
    langBar_->Release();
    langBar_ = nullptr;
}

void CFireTextService::RefreshLangBar() {
    if (langBar_) langBar_->UpdateModeText();
}

// ---- ITfThreadMgrEventSink ----
STDMETHODIMP CFireTextService::OnSetFocus(ITfDocumentMgr* pdimFocus,
                                          ITfDocumentMgr* /*pdimPrevFocus*/) {
    // 文档焦点切换：先清空当前组字并隐藏候选窗
    if (engine_) engine_->clean();

    // per-app 输入模式
    if (engine_ && config_.keep_app_input_mode) {
        if (pdimFocus == nullptr) {
            // 失去焦点（切到无 IME 的窗口）：保存当前应用的最后模式，避免丢失最近一次变更。
            if (!currentAppId_.empty()) {
                engine_->save_input_mode_for_app(currentAppId_);
            }
        } else {
            // 获得焦点：先把当前模式保存给上一个应用，再恢复新应用的模式。
            std::string appId = inputClient_.bundle_id();
            if (!currentAppId_.empty() && currentAppId_ != appId) {
                engine_->save_input_mode_for_app(currentAppId_);
            }
            bool changed = engine_->restore_input_mode_for_app(appId);
            currentAppId_ = appId;
            if (changed) RefreshLangBar();
        }
    }
    return S_OK;
}

// ---- ITfKeyEventSink ----
STDMETHODIMP CFireTextService::OnSetFocus(BOOL /*fForeground*/) {
    return S_OK;
}

bool CFireTextService::ShouldEat(const fire::KeyEvent& ev) const {
    if (!engine_) return false;
    if (engine_->input_mode() == fire::InputMode::EnUS && engine_->original_string().empty()) {
        // 英文模式且无组字：仅在需要切换/标点转换时吃键，其余透传
        return false;
    }
    // 组合快捷键交给宿主
    if (ev.has_command_shortcut_modifier()) return false;
    // 有组字时几乎所有可处理键都要吃
    if (!engine_->original_string().empty()) return true;
    // 无组字：可见字母/标点/数字（数字仅在有组字时才处理，这里放行给引擎判断）
    if (ev.is_alphabet()) return true;
    if (ev.special == fire::SpecialKey::None && !ev.text.empty()) return true;  // 标点等
    return false;
}

STDMETHODIMP CFireTextService::OnTestKeyDown(ITfContext* /*pic*/, WPARAM wParam, LPARAM lParam,
                                             BOOL* pfEaten) {
    BYTE kb[256];
    GetKeyboardState(kb);
    UINT scan = (UINT)((lParam >> 16) & 0xFF);
    // 查询阶段：不改写键盘死键状态
    fire::KeyEvent ev = translator_.Translate((UINT)wParam, scan, kb, /*noStateChange=*/true);
    *pfEaten = ShouldEat(ev) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP CFireTextService::OnTestKeyUp(ITfContext* /*pic*/, WPARAM wParam, LPARAM /*lParam*/,
                                           BOOL* pfEaten) {
    // Shift 单击切换：抬起时判断
    *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP CFireTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam,
                                         BOOL* pfEaten) {
    BYTE kb[256];
    GetKeyboardState(kb);
    UINT scan = (UINT)((lParam >> 16) & 0xFF);
    translator_.shiftChecker.OnKeyDown((UINT)wParam);

    fire::KeyEvent ev = translator_.Translate((UINT)wParam, scan, kb);
    if (!ShouldEat(ev)) {
        *pfEaten = FALSE;
        return S_OK;
    }
    bool eaten = ProcessKeyInEditSession(pic, ev);
    *pfEaten = eaten ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP CFireTextService::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM /*lParam*/,
                                       BOOL* pfEaten) {
    *pfEaten = FALSE;
    // Shift 单击 -> 中英文切换
    if (translator_.shiftChecker.OnKeyUp((UINT)wParam) && !config_.disable_en_mode) {
        fire::KeyEvent ev;
        ev.is_modifier_change = true;
        ev.changed_modifier = fire::SpecialKey::ShiftKey;
        ev.toggle_input_mode_request = true;
        ProcessKeyInEditSession(pic, ev);
        *pfEaten = TRUE;
    }
    return S_OK;
}

STDMETHODIMP CFireTextService::OnPreservedKey(ITfContext* /*pic*/, REFGUID /*rguid*/,
                                              BOOL* pfEaten) {
    *pfEaten = FALSE;
    return S_OK;
}

// ---- ITfCompositionSink ----
STDMETHODIMP CFireTextService::OnCompositionTerminated(TfEditCookie /*ec*/,
                                                       ITfComposition* /*pComposition*/) {
    inputClient_.SetComposition(nullptr);
    if (engine_) engine_->clean();
    return S_OK;
}

// ---- 语言栏按钮回调：切换 / 设置 / 查询输入模式 ----
void CFireTextService::ToggleInputModeFromLangBar() {
    if (!engine_) return;
    engine_->toggle_input_mode();
    if (config_.keep_app_input_mode && !currentAppId_.empty()) {
        engine_->save_input_mode_for_app(currentAppId_);
    }
    RefreshLangBar();
}

void CFireTextService::SetInputModeFromLangBar(fire::InputMode mode) {
    if (!engine_) return;
    engine_->set_input_mode(mode);
    if (config_.keep_app_input_mode && !currentAppId_.empty()) {
        engine_->save_input_mode_for_app(currentAppId_);
    }
    RefreshLangBar();
}

fire::InputMode CFireTextService::CurrentInputMode() const {
    return engine_ ? engine_->input_mode() : fire::InputMode::ZhHans;
}

void CFireTextService::RunInEditSession(std::function<void()> action) {
    if (!threadMgr_ || !engine_) return;
    // 取当前焦点文档的顶层上下文
    CComPtr<ITfDocumentMgr> docMgr;
    if (FAILED(threadMgr_->GetFocus(&docMgr)) || !docMgr) return;
    CComPtr<ITfContext> ctx;
    if (FAILED(docMgr->GetTop(&ctx)) || !ctx) return;

    ActionEditSession* session = new ActionEditSession(this, ctx, std::move(action));
    HRESULT hrSession = S_OK;
    ctx->RequestEditSession(clientId_, session, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    session->Release();
}

bool CFireTextService::ProcessKeyInEditSession(ITfContext* pic, const fire::KeyEvent& ev) {
    if (!pic) return false;
    KeyEditSession* session = new KeyEditSession(this, pic, ev);
    HRESULT hrSession = S_OK;
    // TF_ES_SYNC 要求同步执行；读写权限用 TF_ES_READWRITE
    HRESULT hr = pic->RequestEditSession(clientId_, session,
                                         TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    bool eaten = false;
    if (SUCCEEDED(hr) && SUCCEEDED(hrSession)) {
        eaten = session->eaten_;
    }
    session->Release();
    return eaten;
}

}  // namespace firewin
