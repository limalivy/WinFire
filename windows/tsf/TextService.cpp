//
//  TextService.cpp — TSF TIP 主实现
//
#include "TextService.h"
#include "DebugLog.h"
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
        FIRE_LOG_ENTER();
        TsfEditSession s;
        s.pContext = ctx_;
        s.editCookie = ec;
        s.clientId = svc_->clientId_;
        svc_->inputClient_.SetEditContext(s);
        svc_->inputClient_.SetComposition(svc_->inputClient_.Composition());
        fire::InputMode before = svc_->engine_->input_mode();
        FIRE_LOG(L"[FireIME] DoEditSession: calling handle_key, ev.text='%hs' ev.special=%d\n",
                 ev_.text.c_str(), (int)ev_.special);
        eaten_ = svc_->engine_->handle_key(ev_);
        FIRE_LOG(L"[FireIME] DoEditSession: handle_key returned eaten=%d\n", eaten_ ? 1 : 0);
        // 输入模式若发生变化，刷新语言栏按钮
        if (before != svc_->engine_->input_mode()) svc_->RefreshLangBar();
        FIRE_LOG_EXIT();
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
        FIRE_LOG_ENTER();
        TsfEditSession s;
        s.pContext = ctx_;
        s.editCookie = ec;
        s.clientId = svc_->clientId_;
        svc_->inputClient_.SetEditContext(s);
        if (action_) action_();
        FIRE_LOG_EXIT();
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
    FIRE_LOG(L"[FireIME] CFireTextService() ctor [tid=%lu]\n", GetCurrentThreadId());
    DllAddRef();
}

CFireTextService::~CFireTextService() {
    FIRE_LOG(L"[FireIME] ~CFireTextService() dtor [tid=%lu]\n", GetCurrentThreadId());
    DllRelease();
}

void CFireTextService::LoadConfigFromDisk() {
    FIRE_LOG_ENTER();
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
    FIRE_LOG(L"[FireIME] LoadConfigFromDisk: db_path='%hs' stats_db_path='%hs'\n",
             config_.db_path.c_str(), config_.stats_db_path.c_str());
    FIRE_LOG_EXIT();
}

void CFireTextService::InitEngine() {
    FIRE_LOG_ENTER();

    LoadConfigFromDisk();
    FIRE_LOG(L"[FireIME] InitEngine: config loaded\n");

    FIRE_LOG(L"[FireIME] InitEngine: creating DictManager\n");
    dict_ = std::make_unique<fire::DictManager>(config_);
    FIRE_LOG(L"[FireIME] InitEngine: DictManager created OK\n");

    FIRE_LOG(L"[FireIME] InitEngine: creating InputEngine\n");
    engine_ = std::make_unique<fire::InputEngine>(config_, *dict_, inputClient_);
    FIRE_LOG(L"[FireIME] InitEngine: InputEngine created OK\n");

    // 注入组字终止回调 sink：宿主主动结束组字时经 OnCompositionTerminated 通知本 TIP。
    inputClient_.SetCompositionSink(static_cast<ITfCompositionSink*>(this));

    // 输入统计库（同步写入）。仅在开启任一统计开关时才创建。
    if (config_.enable_statistics || config_.enable_hanzi_frequency_statistics) {
        FIRE_LOG(L"[FireIME] InitEngine: creating Statistics, path='%hs'\n",
                 config_.stats_db_path.c_str());
        stats_ = std::make_unique<fire::Statistics>(config_.stats_db_path);
        engine_->set_candidate_inserted_callback([this](const fire::CandidateInsertedInfo& info) {
            if (stats_ && stats_->is_open()) {
                stats_->record_candidate(info.candidate, info.app_id,
                                         info.hanzi_frequency_parts,
                                         config_.enable_statistics,
                                         config_.enable_hanzi_frequency_statistics);
            }
        });
        FIRE_LOG(L"[FireIME] InitEngine: Statistics created OK\n");
    }

    FIRE_LOG(L"[FireIME] InitEngine: creating CandidateWindow, g_hInst=%p\n", (void*)g_hInst);
    candWindow_ = std::make_unique<CandidateWindowController>(config_);
    if (!candWindow_->Create(g_hInst)) {
        FIRE_LOG(L"[FireIME] InitEngine: CandidateWindow::Create FAILED\n");
    } else {
        FIRE_LOG(L"[FireIME] InitEngine: CandidateWindow created OK\n");
    }
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

    FIRE_LOG(L"[FireIME] InitEngine: complete\n");
    FIRE_LOG_EXIT();
}

// ---- ITfTextInputProcessor(Ex) ----
STDMETHODIMP CFireTextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD /*dwFlags*/) {
    return Activate(ptim, tid);
}

// SEH 包装器：C2712 — __try 不能与需要栈展开的 C++ 对象共存，
// 因此必须将 SEH 隔离在一个没有任何局部 C++ 对象的自由函数里。
static bool InitEngineSafe(CFireTextService* svc) {
    FIRE_LOG_ENTER();
    __try {
        svc->InitEngine();
        FIRE_LOG(L"[FireIME] InitEngineSafe: InitEngine returned OK\n");
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        DWORD code = GetExceptionCode();
        FIRE_LOG(L"[FireIME] CRASH in InitEngine() — SEH exception code=0x%08lX, aborting Activate.\n",
                 (unsigned long)code);
        return false;
    }
}

STDMETHODIMP CFireTextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    FIRE_LOG_ENTER();
    // Activate 在 TSF 主线程调用，已脱离 DllMain 的加载器锁，可安全启用文件日志。
    firewin::FireLogSetFsReady(true);
    FIRE_LOG(L"[FireIME] Activate: ptim=%p tid=%lu [tid=%lu]\n",
             (void*)ptim, (unsigned long)tid, GetCurrentThreadId());
    threadMgr_ = ptim;
    clientId_ = tid;

    if (!InitEngineSafe(this)) {
        FIRE_LOG(L"[FireIME] Activate: InitEngineSafe FAILED, returning E_FAIL\n");
        threadMgr_.Release();
        threadMgr_ = nullptr;
        clientId_ = TF_CLIENTID_NULL;
        return E_FAIL;
    }
    FIRE_LOG(L"[FireIME] Activate: InitEngine OK\n");

    // 注册 ThreadMgrEventSink
    CComPtr<ITfSource> source;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfSource, (void**)&source))) {
        HRESULT hr = source->AdviseSink(IID_ITfThreadMgrEventSink,
                           static_cast<ITfThreadMgrEventSink*>(this), &threadMgrCookie_);
        FIRE_LOG_HR(hr, L"AdviseSink(ThreadMgrEventSink)");
    } else {
        FIRE_LOG(L"[FireIME] Activate: QI ITfSource FAILED\n");
    }

    // 注册 KeyEventSink
    CComPtr<ITfKeystrokeMgr> keyMgr;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr, (void**)&keyMgr))) {
        HRESULT hr = keyMgr->AdviseKeyEventSink(clientId_, static_cast<ITfKeyEventSink*>(this), TRUE);
        FIRE_LOG_HR(hr, L"AdviseKeyEventSink");
    } else {
        FIRE_LOG(L"[FireIME] Activate: QI ITfKeystrokeMgr FAILED\n");
    }

    // 注册语言栏按钮（中/英状态）
    RegisterLangBarButton();
    FIRE_LOG(L"[FireIME] Activate: complete, returning S_OK\n");
    FIRE_LOG_EXIT();
    return S_OK;
}

STDMETHODIMP CFireTextService::Deactivate() {
    FIRE_LOG_ENTER();
    // Deactivate 后可能进入 DLL_PROCESS_DETACH 路径，关闭文件日志避免加载器锁问题。
    firewin::FireLogSetFsReady(false);
    UnregisterLangBarButton();

    CComPtr<ITfKeystrokeMgr> keyMgr;
    if (threadMgr_ && SUCCEEDED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr, (void**)&keyMgr))) {
        keyMgr->UnadviseKeyEventSink(clientId_);
        FIRE_LOG(L"[FireIME] Deactivate: UnadviseKeyEventSink done\n");
    }
    if (threadMgr_ && threadMgrCookie_ != TF_INVALID_COOKIE) {
        CComPtr<ITfSource> source;
        if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfSource, (void**)&source))) {
            source->UnadviseSink(threadMgrCookie_);
            FIRE_LOG(L"[FireIME] Deactivate: UnadviseSink done\n");
        }
        threadMgrCookie_ = TF_INVALID_COOKIE;
    }
    FIRE_LOG(L"[FireIME] Deactivate: destroying resources\n");
    if (candWindow_) candWindow_->Destroy();
    stats_.reset();
    engine_.reset();
    dict_.reset();
    candWindow_.reset();
    threadMgr_.Release();
    clientId_ = TF_CLIENTID_NULL;
    FIRE_LOG(L"[FireIME] Deactivate: complete\n");
    FIRE_LOG_EXIT();
    return S_OK;
}

// ---- 语言栏按钮注册/注销 ----
void CFireTextService::RegisterLangBarButton() {
    FIRE_LOG_ENTER();
    if (!threadMgr_ || langBar_) {
        FIRE_LOG(L"[FireIME] RegisterLangBarButton: skip (threadMgr=%p langBar=%p)\n",
                 (void*)threadMgr_.p, (void*)langBar_);
        return;
    }
    CComPtr<ITfLangBarItemMgr> mgr;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfLangBarItemMgr, (void**)&mgr))) {
        langBar_ = new CFireLangBarButton(this);
        HRESULT hr = mgr->AddItem(langBar_);
        FIRE_LOG_HR(hr, L"AddItem(LangBar)");
    } else {
        FIRE_LOG(L"[FireIME] RegisterLangBarButton: QI ITfLangBarItemMgr FAILED\n");
    }
    FIRE_LOG_EXIT();
}

void CFireTextService::UnregisterLangBarButton() {
    FIRE_LOG_ENTER();
    if (!langBar_) {
        FIRE_LOG(L"[FireIME] UnregisterLangBarButton: langBar_ null, skip\n");
        return;
    }
    if (threadMgr_) {
        CComPtr<ITfLangBarItemMgr> mgr;
        if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfLangBarItemMgr, (void**)&mgr))) {
            mgr->RemoveItem(langBar_);
            FIRE_LOG(L"[FireIME] UnregisterLangBarButton: RemoveItem done\n");
        }
    }
    langBar_->Release();
    langBar_ = nullptr;
    FIRE_LOG_EXIT();
}

void CFireTextService::RefreshLangBar() {
    if (langBar_) langBar_->UpdateModeText();
}

// ---- ITfThreadMgrEventSink ----
STDMETHODIMP CFireTextService::OnSetFocus(ITfDocumentMgr* pdimFocus,
                                          ITfDocumentMgr* /*pdimPrevFocus*/) {
    FIRE_LOG(L"[FireIME] OnSetFocus(docmgr): pdimFocus=%p engine=%p keepApp=%d [tid=%lu]\n",
             (void*)pdimFocus, (void*)engine_.get(), config_.keep_app_input_mode ? 1 : 0,
             GetCurrentThreadId());
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
            FIRE_LOG(L"[FireIME] OnSetFocus(docmgr): appId='%hs' currentAppId='%hs'\n",
                     appId.c_str(), currentAppId_.c_str());
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
STDMETHODIMP CFireTextService::OnSetFocus(BOOL fForeground) {
    FIRE_LOG(L"[FireIME] OnSetFocus(keyboard): fForeground=%d [tid=%lu]\n",
             fForeground ? 1 : 0, GetCurrentThreadId());
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
    FIRE_LOG(L"[FireIME] OnTestKeyDown: wParam=0x%lX [tid=%lu]\n",
             (unsigned long)wParam, GetCurrentThreadId());
    BYTE kb[256];
    GetKeyboardState(kb);
    UINT scan = (UINT)((lParam >> 16) & 0xFF);
    // 查询阶段：不改写键盘死键状态
    fire::KeyEvent ev = translator_.Translate((UINT)wParam, scan, kb, /*noStateChange=*/true);
    bool eat = ShouldEat(ev);
    *pfEaten = eat ? TRUE : FALSE;
    FIRE_LOG(L"[FireIME] OnTestKeyDown: eat=%d text='%hs'\n", eat ? 1 : 0, ev.text.c_str());
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
    FIRE_LOG(L"[FireIME] OnKeyDown: wParam=0x%lX pic=%p [tid=%lu]\n",
             (unsigned long)wParam, (void*)pic, GetCurrentThreadId());
    BYTE kb[256];
    GetKeyboardState(kb);
    UINT scan = (UINT)((lParam >> 16) & 0xFF);
    translator_.shiftChecker.OnKeyDown((UINT)wParam);

    fire::KeyEvent ev = translator_.Translate((UINT)wParam, scan, kb);
    if (!ShouldEat(ev)) {
        *pfEaten = FALSE;
        FIRE_LOG(L"[FireIME] OnKeyDown: not eaten (ShouldEat=false)\n");
        return S_OK;
    }
    bool eaten = ProcessKeyInEditSession(pic, ev);
    *pfEaten = eaten ? TRUE : FALSE;
    FIRE_LOG(L"[FireIME] OnKeyDown: eaten=%d\n", eaten ? 1 : 0);
    return S_OK;
}

STDMETHODIMP CFireTextService::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM /*lParam*/,
                                       BOOL* pfEaten) {
    *pfEaten = FALSE;
    // Shift 单击 -> 中英文切换
    if (translator_.shiftChecker.OnKeyUp((UINT)wParam) && !config_.disable_en_mode) {
        FIRE_LOG(L"[FireIME] OnKeyUp: Shift single-click detected, toggling mode\n");
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
    FIRE_LOG_ENTER();
    inputClient_.SetComposition(nullptr);
    if (engine_) engine_->clean();
    FIRE_LOG_EXIT();
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
    FIRE_LOG_ENTER();
    if (!threadMgr_ || !engine_) {
        FIRE_LOG(L"[FireIME] RunInEditSession: abort (threadMgr=%p engine=%p)\n",
                 (void*)threadMgr_.p, (void*)engine_.get());
        return;
    }
    // 取当前焦点文档的顶层上下文
    CComPtr<ITfDocumentMgr> docMgr;
    if (FAILED(threadMgr_->GetFocus(&docMgr)) || !docMgr) {
        FIRE_LOG(L"[FireIME] RunInEditSession: GetFocus failed\n");
        return;
    }
    CComPtr<ITfContext> ctx;
    if (FAILED(docMgr->GetTop(&ctx)) || !ctx) {
        FIRE_LOG(L"[FireIME] RunInEditSession: GetTop failed\n");
        return;
    }

    ActionEditSession* session = new ActionEditSession(this, ctx, std::move(action));
    HRESULT hrSession = S_OK;
    HRESULT hr = ctx->RequestEditSession(clientId_, session, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    FIRE_LOG_HR(hr, L"RequestEditSession(ActionEditSession)");
    session->Release();
    FIRE_LOG_EXIT();
}

bool CFireTextService::ProcessKeyInEditSession(ITfContext* pic, const fire::KeyEvent& ev) {
    FIRE_LOG_ENTER();
    if (!pic) {
        FIRE_LOG(L"[FireIME] ProcessKeyInEditSession: pic is null, returning false\n");
        return false;
    }
    KeyEditSession* session = new KeyEditSession(this, pic, ev);
    HRESULT hrSession = S_OK;
    // TF_ES_SYNC 要求同步执行；读写权限用 TF_ES_READWRITE
    HRESULT hr = pic->RequestEditSession(clientId_, session,
                                         TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    FIRE_LOG(L"[FireIME] ProcessKeyInEditSession: RequestEditSession hr=0x%08lX hrSession=0x%08lX\n",
             (unsigned long)hr, (unsigned long)hrSession);
    bool eaten = false;
    if (SUCCEEDED(hr) && SUCCEEDED(hrSession)) {
        eaten = session->eaten_;
    }
    session->Release();
    FIRE_LOG(L"[FireIME] ProcessKeyInEditSession: eaten=%d\n", eaten ? 1 : 0);
    FIRE_LOG_EXIT();
    return eaten;
}

}  // namespace firewin
