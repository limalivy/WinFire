//
//  TextService.cpp — TSF TIP 主实现
//
#include "TextService.h"
#include "DebugLog.h"
#include "DictIpcProxy.h"
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
        FIRE_LOG(L"[WinFire] DoEditSession: calling handle_key, ev.text='%hs' ev.special=%d\n",
                 ev_.text.c_str(), (int)ev_.special);
        eaten_ = svc_->engine_->handle_key(ev_);
        FIRE_LOG(L"[WinFire] DoEditSession: handle_key returned eaten=%d\n", eaten_ ? 1 : 0);
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
    FIRE_LOG(L"[WinFire] CFireTextService() ctor [tid=%lu]\n", GetCurrentThreadId());
    DllAddRef();
}

CFireTextService::~CFireTextService() {
    FIRE_LOG(L"[WinFire] ~CFireTextService() dtor [tid=%lu]\n", GetCurrentThreadId());
    DllRelease();
}

void CFireTextService::LoadConfigFromDisk() {
    FIRE_LOG_ENTER();
    // 读取 %APPDATA%\WinFire\config.json 的全量配置（不存在则用默认值）。
    // 仅 Activate 时 bootstrap 调用一次：给引擎一个合理初始值，避免 dictd 冷启时
    // 引擎以全 default 起步。之后 config 全部经 IPC 拿（dictd 是唯一真相源）。
    firecfg::ConfigStore::Load(config_);

    // 标点表：为空时用默认中文标点表
    if (config_.custom_punctuation_settings.empty()) {
        config_.custom_punctuation_settings = fire::default_punctuation();
    }
    // 注：db_path / stats_db_path 由 fire_dictd.exe 后台进程使用，DLL 端不再直接读库。
    FIRE_LOG_EXIT();
}

void CFireTextService::MaybeReloadConfig() {
    // 零轮询 config 热加载（兜底）：节流（60s）到期后直接发一次 ValidateCache IPC（不读盘）。
    // dictd 比对 config_token，不一致时回传全量 config_json，回调里原地填 config_。
    // 节流的意义从"省 stat IO"变为"省 IPC 往返"——仍是零磁盘 IO。
    // 主路径是切应用触发的 ReloadConfigNow；此处仅兜底「用户长时间不切应用连续打字」
    // 期间被外部脚本（fire_dictd.exe --reload-config）改 config 的极端场景。
    static const ULONGLONG kConfigCheckIntervalMs = 60 * 1000;
    ULONGLONG now = GetTickCount64();
    if (now - lastConfigCheckTick_ < kConfigCheckIntervalMs) {
        return;
    }
    lastConfigCheckTick_ = now;
    if (dictService_) {
        dictService_->ValidateCache();
    }
}

void CFireTextService::ReloadConfigNow() {
    // 切应用即时同步：绕过 MaybeReloadConfig 的 60s 节流，立即发一次 ValidateCache。
    // 切应用是低频事件，且只在该次焦点切换的线程上阻塞一次同步 IPC（≤20ms），
    // 不会冲击按键热路径。失败时静默（dictService_ 内部已处理，不影响输入）。
    // 同时刷新节流时间戳，避免紧接着的 OnKeyDown 兜底又查一次（仍是零磁盘 IO）。
    lastConfigCheckTick_ = GetTickCount64();
    if (dictService_) {
        dictService_->ValidateCache();
    }
}

void CFireTextService::InitEngine() {
    FIRE_LOG_ENTER();

    // bootstrap：读一次磁盘给引擎合理初始值（沙箱场景可能读不到，降级用 default）。
    // 之后首次 ValidateCache(client_config_token=0) 会从 dictd 拉全量 config 覆盖。
    LoadConfigFromDisk();
    lastConfigCheckTick_ = GetTickCount64();
    FIRE_LOG(L"[WinFire] InitEngine: config loaded (bootstrap)\n");

    // 查字/统计服务：经 IPC 转发给 fire_dictd.exe（正常 IL 后台进程），
    // 使 SearchHost.exe/UWP 等 AppContainer 沙箱进程也能出候选。
    // 后台不可用时 IsAvailable()=false，引擎降级透传（不再回退本进程直接查库）。
    {
        auto proxy = std::make_unique<DictIpcProxy>(inputClient_.bundle_id());
        // config 更新回调：dictd 在 CacheValidate 响应里回传全量 config_json 时调用。
        // 原地填 config_（引擎/PunctuationConverter 持引用，即见，不重建引擎）。
        proxy->SetConfigUpdatedCallback([this](const std::string& config_json) {
            firecfg::ConfigStore::LoadFromString(config_, config_json);
            if (config_.custom_punctuation_settings.empty()) {
                config_.custom_punctuation_settings = fire::default_punctuation();
            }
        });
        ULONGLONG tHs = GetTickCount64();
        proxy->Handshake();
        FIRE_LOG(L"[WinFire] InitEngine: DictIpcProxy handshake ready=%d took=%lums\n",
                 proxy->IsAvailable() ? 1 : 0, (unsigned long)(GetTickCount64() - tHs));
        // 握手成功后校验本地缓存策略 + 拉 config（client_config_token=0 强制全量）。
        // 紧随握手复用同一管道连接，仅 Activate 时一次往返。dictd 不可用时跳过，
        // 引擎保留 bootstrap 的磁盘 config（降级可用）。
        if (proxy->IsAvailable()) {
            proxy->ValidateCache();
        }
        dictService_ = std::move(proxy);
    }

    FIRE_LOG(L"[WinFire] InitEngine: creating InputEngine\n");
    engine_ = std::make_unique<fire::InputEngine>(config_, *dictService_, inputClient_);
    FIRE_LOG(L"[WinFire] InitEngine: InputEngine created OK\n");

    // 注入组字终止回调 sink：宿主主动结束组字时经 OnCompositionTerminated 通知本 TIP。
    inputClient_.SetCompositionSink(static_cast<ITfCompositionSink*>(this));

    // 上屏统计回调：经 IDictService 写入（本地实现内部转调 Statistics）。
    engine_->set_candidate_inserted_callback([this](const fire::CandidateInsertedInfo& info) {
        dictService_->RecordCandidate(info.candidate, info.app_id,
                                      info.hanzi_frequency_parts,
                                      config_.enable_statistics,
                                      config_.enable_hanzi_frequency_statistics);
    });

    FIRE_LOG(L"[WinFire] InitEngine: creating CandidateWindow, g_hInst=%p\n", (void*)g_hInst);
    candWindow_ = std::make_unique<CandidateWindowController>(config_);
    if (!candWindow_->Create(g_hInst)) {
        FIRE_LOG(L"[WinFire] InitEngine: CandidateWindow::Create FAILED\n");
    } else {
        FIRE_LOG(L"[WinFire] InitEngine: CandidateWindow created OK\n");
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

    FIRE_LOG(L"[WinFire] InitEngine: complete\n");
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
        FIRE_LOG(L"[WinFire] InitEngineSafe: InitEngine returned OK\n");
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        DWORD code = GetExceptionCode();
        FIRE_LOG(L"[WinFire] CRASH in InitEngine() — SEH exception code=0x%08lX, aborting Activate.\n",
                 (unsigned long)code);
        return false;
    }
}

STDMETHODIMP CFireTextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    FIRE_LOG_ENTER();
    // Activate 在 TSF 主线程调用，已脱离 DllMain 的加载器锁，可安全启用文件日志。
    firewin::FireLogSetFsReady(true);
    firewin::FireLogDiagBannerOnce();  // 每进程一次：打印 WinFire/宿主版本号供 DbgView 识别
    FIRE_LOG(L"[WinFire] Activate: ptim=%p tid=%lu [tid=%lu]\n",
             (void*)ptim, (unsigned long)tid, GetCurrentThreadId());
    threadMgr_ = ptim;
    clientId_ = tid;

    if (!InitEngineSafe(this)) {
        FIRE_LOG(L"[WinFire] Activate: InitEngineSafe FAILED, returning E_FAIL\n");
        threadMgr_.Release();
        threadMgr_ = nullptr;
        clientId_ = TF_CLIENTID_NULL;
        return E_FAIL;
    }
    FIRE_LOG(L"[WinFire] Activate: InitEngine OK\n");

    // 初始化 currentAppId_，避免首次 OnSetFocus(docmgr) 误判为跨应用切换而 clean()
    currentAppId_ = inputClient_.bundle_id();
    FIRE_LOG(L"[WinFire] Activate: currentAppId='%hs'\n", currentAppId_.c_str());

    // 注册 ThreadMgrEventSink
    CComPtr<ITfSource> source;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfSource, (void**)&source))) {
        HRESULT hr = source->AdviseSink(IID_ITfThreadMgrEventSink,
                           static_cast<ITfThreadMgrEventSink*>(this), &threadMgrCookie_);
        FIRE_LOG_HR(hr, L"AdviseSink(ThreadMgrEventSink)");
    } else {
        FIRE_LOG(L"[WinFire] Activate: QI ITfSource FAILED\n");
    }

    // 注册 KeyEventSink
    CComPtr<ITfKeystrokeMgr> keyMgr;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr, (void**)&keyMgr))) {
        HRESULT hr = keyMgr->AdviseKeyEventSink(clientId_, static_cast<ITfKeyEventSink*>(this), TRUE);
        FIRE_LOG_HR(hr, L"AdviseKeyEventSink");
    } else {
        FIRE_LOG(L"[WinFire] Activate: QI ITfKeystrokeMgr FAILED\n");
    }

    // 注册语言栏按钮（中/英状态）
    RegisterLangBarButton();
    FIRE_LOG(L"[WinFire] Activate: complete, returning S_OK\n");
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
        FIRE_LOG(L"[WinFire] Deactivate: UnadviseKeyEventSink done\n");
    }
    if (threadMgr_ && threadMgrCookie_ != TF_INVALID_COOKIE) {
        CComPtr<ITfSource> source;
        if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfSource, (void**)&source))) {
            source->UnadviseSink(threadMgrCookie_);
            FIRE_LOG(L"[WinFire] Deactivate: UnadviseSink done\n");
        }
        threadMgrCookie_ = TF_INVALID_COOKIE;
    }
    FIRE_LOG(L"[WinFire] Deactivate: destroying resources\n");
    // 通知后台尽快落盘本次会话积累的 LRU 缓存（fire-and-forget，不等回复）。
    // daemon 带 1 分钟节流，即使没存也不影响退出。在 engine_/dictService_ 销毁前发。
    if (dictService_) {
        dictService_->SaveCache(currentAppId_);
    }
    if (candWindow_) candWindow_->Destroy();
    engine_.reset();
    dictService_.reset();
    candWindow_.reset();
    threadMgr_.Release();
    clientId_ = TF_CLIENTID_NULL;
    FIRE_LOG(L"[WinFire] Deactivate: complete\n");
    FIRE_LOG_EXIT();
    return S_OK;
}

// ---- 语言栏按钮注册/注销 ----
void CFireTextService::RegisterLangBarButton() {
    FIRE_LOG_ENTER();
    if (!threadMgr_ || langBar_) {
        FIRE_LOG(L"[WinFire] RegisterLangBarButton: skip (threadMgr=%p langBar=%p)\n",
                 (void*)threadMgr_.p, (void*)langBar_);
        return;
    }
    CComPtr<ITfLangBarItemMgr> mgr;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfLangBarItemMgr, (void**)&mgr))) {
        langBar_ = new CFireLangBarButton(this);
        HRESULT hr = mgr->AddItem(langBar_);
        FIRE_LOG_HR(hr, L"AddItem(LangBar)");
    } else {
        FIRE_LOG(L"[WinFire] RegisterLangBarButton: QI ITfLangBarItemMgr FAILED\n");
    }
    FIRE_LOG_EXIT();
}

void CFireTextService::UnregisterLangBarButton() {
    FIRE_LOG_ENTER();
    if (!langBar_) {
        FIRE_LOG(L"[WinFire] UnregisterLangBarButton: langBar_ null, skip\n");
        return;
    }
    if (threadMgr_) {
        CComPtr<ITfLangBarItemMgr> mgr;
        if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfLangBarItemMgr, (void**)&mgr))) {
            mgr->RemoveItem(langBar_);
            FIRE_LOG(L"[WinFire] UnregisterLangBarButton: RemoveItem done\n");
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
    FIRE_LOG(L"[WinFire] OnSetFocus(docmgr): pdimFocus=%p engine=%p keepApp=%d [tid=%lu]\n",
             (void*)pdimFocus, (void*)engine_.get(), config_.keep_app_input_mode ? 1 : 0,
             GetCurrentThreadId());

    // 计算当前应用标识，用于判断是否真正跨应用切换
    std::string appId;
    if (pdimFocus) {
        appId = inputClient_.bundle_id();
    }
    bool appChanged = (appId != currentAppId_);

    // 仅在真正跨应用切换时才清空组字。
    // 注意：StartComposition/SetCompositionText 会触发 OnSetFocus，若无条件 clean
    // 会把刚建立的组字立即清空，导致 CapsLock/Shift 上屏失效、一简字卡住等问题。
    if (engine_ && appChanged) {
        FIRE_LOG(L"[WinFire] OnSetFocus(docmgr): app changed '%hs' -> '%hs', cleaning\n",
                 currentAppId_.c_str(), appId.c_str());
        engine_->clean();
        // config 即时同步：config.exe 是独立窗口，改完设置切回目标应用必经此处。
        // appChanged 判定天然规避了 StartComposition 反身触发的高频 OnSetFocus。
        ReloadConfigNow();
    }

    // per-app 输入模式
    if (engine_ && config_.keep_app_input_mode) {
        if (pdimFocus == nullptr) {
            // 失去焦点（切到无 IME 的窗口）：保存当前应用的最后模式，避免丢失最近一次变更。
            if (!currentAppId_.empty()) {
                engine_->save_input_mode_for_app(currentAppId_);
            }
        } else {
            // 获得焦点：先把当前模式保存给上一个应用，再恢复新应用的模式。
            FIRE_LOG(L"[WinFire] OnSetFocus(docmgr): appId='%hs' currentAppId='%hs'\n",
                     appId.c_str(), currentAppId_.c_str());
            if (!currentAppId_.empty() && appChanged) {
                engine_->save_input_mode_for_app(currentAppId_);
            }
            bool changed = engine_->restore_input_mode_for_app(appId);
            currentAppId_ = appId;
            if (changed) RefreshLangBar();
        }
    } else if (pdimFocus) {
        // 即便不开 keep_app_input_mode，也要更新 currentAppId_，
        // 否则 appChanged 判定会失效（后续同应用切换会被误判为跨应用）
        currentAppId_ = appId;
    }
    return S_OK;
}

// ---- ITfKeyEventSink ----
STDMETHODIMP CFireTextService::OnSetFocus(BOOL fForeground) {
    FIRE_LOG(L"[WinFire] OnSetFocus(keyboard): fForeground=%d [tid=%lu]\n",
             fForeground ? 1 : 0, GetCurrentThreadId());
    return S_OK;
}

bool CFireTextService::ShouldEat(const fire::KeyEvent& ev) const {
    if (!engine_) return false;
    // 字典不可用时（如 SearchHost.exe 等系统进程尚未加载到正确路径），
    // 全部透传，避免吃键但不产出文本（Fix C）。
    if (!dictService_ || !dictService_->IsAvailable()) return false;
    // 英文模式且无组字：仅在需要切换/标点转换时吃键，其余透传
    if (engine_->input_mode() == fire::InputMode::EnUS && engine_->original_string().empty())
        return false;
    // 组合快捷键交给宿主
    if (ev.has_command_shortcut_modifier()) return false;
    // 有组字时只吃有输出的键（文本或特殊键）；裸修饰键（Shift/Ctrl/Alt
    // 单独按下）放行给系统，避免干扰 TSF 对 OnTestKeyUp 的调用链，
    // 导致 shiftChecker 收不到抬起事件、中英切换失效。
    if (!engine_->original_string().empty()) {
        if (ev.is_alphabet() || !ev.text.empty() ||
            ev.special != fire::SpecialKey::None)
            return true;
        return false;
    }
    // 无组字：可见字母/标点/数字（数字仅在有组字时才处理，这里放行给引擎判断）
    if (ev.is_alphabet()) return true;
    if (ev.special == fire::SpecialKey::None && !ev.text.empty()) return true;  // 标点等
    return false;
}

STDMETHODIMP CFireTextService::OnTestKeyDown(ITfContext* /*pic*/, WPARAM wParam, LPARAM lParam,
                                             BOOL* pfEaten) {
    FIRE_LOG(L"[WinFire] OnTestKeyDown: wParam=0x%lX [tid=%lu]\n",
             (unsigned long)wParam, GetCurrentThreadId());
    BYTE kb[256];
    GetKeyboardState(kb);
    UINT scan = (UINT)((lParam >> 16) & 0xFF);
    // 查询阶段：不改写键盘死键状态
    fire::KeyEvent ev = translator_.Translate((UINT)wParam, scan, kb, /*noStateChange=*/true);
    // shiftChecker 状态机在 OnTest* 中维护：OnTest* 每次按键必被调用（无论是否吃键），
    // 而 OnKeyDown/OnKeyUp 仅在该键被 OnTest* 判定为 eat=TRUE 时才调用。
    // 裸 Shift 的 ShouldEat=FALSE → OnKeyDown 收不到，故必须在此记录状态。
    translator_.shiftChecker.OnKeyDown((UINT)wParam);
    bool eat = ShouldEat(ev);
    *pfEaten = eat ? TRUE : FALSE;
    FIRE_LOG(L"[WinFire] OnTestKeyDown: eat=%d text='%hs'\n", eat ? 1 : 0, ev.text.c_str());
    return S_OK;
}

STDMETHODIMP CFireTextService::OnTestKeyUp(ITfContext* /*pic*/, WPARAM wParam, LPARAM /*lParam*/,
                                           BOOL* pfEaten) {
    *pfEaten = FALSE;
    FIRE_LOG(L"[WinFire] OnTestKeyUp: wParam=0x%lX [tid=%lu]\n",
             (unsigned long)wParam, GetCurrentThreadId());
    // Shift 单击切换：OnTestKeyUp 每次抬起必被调用。检测到单击则吃掉此次抬起，
    // 使 TSF 随后调用 OnKeyUp 执行真正切换（OnKeyUp 在 EditSession 内，可改文档）。
    if (translator_.shiftChecker.OnKeyUp((UINT)wParam) && !config_.disable_en_mode) {
        pendingShiftToggle_ = true;
        *pfEaten = TRUE;
        FIRE_LOG(L"[WinFire] OnTestKeyUp: Shift single-click detected, pending toggle\n");
    }
    // CapsLock 组字上屏：组字时按 CapsLock，吃掉抬起，在 OnKeyUp 中上屏大写字符。
    // CapsLock 状态由系统键盘驱动管理，吃掉 KeyUp 不影响灯亮/灭。
    if (wParam == VK_CAPITAL && engine_ && !engine_->original_string().empty()) {
        pendingCapsLockCommit_ = true;
        *pfEaten = TRUE;
        FIRE_LOG(L"[WinFire] OnTestKeyUp: CapsLock with composition, pending commit\n");
    }
    return S_OK;
}

STDMETHODIMP CFireTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam,
                                         BOOL* pfEaten) {
    FIRE_LOG(L"[WinFire] OnKeyDown: wParam=0x%lX pic=%p [tid=%lu]\n",
             (unsigned long)wParam, (void*)pic, GetCurrentThreadId());

    // 配置热加载（兜底）：用户长时间不切应用连续打字期间，外部脚本改 config 的兜底。
    // 主路径在 OnSetFocus(appChanged) —— config.exe 改完设置切回应用即实时同步。
    // 零磁盘 IO，仅节流到期时发一次 ValidateCache IPC。
    MaybeReloadConfig();

    // 查字后台可用性恢复：若 dictService_ 不可用（如开机时后台尚未启动、
    // 或某次查询超时后 available_ 被置 false），在按键热路径上周期性尝试重连。
    // TryRecover 自带 1s 退避；后台就绪后下一次按键即恢复中文输入，
    // 避免「超时一次后该进程再也无法输入中文」的死锁。
    if (dictService_ && !dictService_->IsAvailable()) {
        FIRE_LOG(L"[WinFire] OnKeyDown: dict unavailable, attempting TryRecover (key passthrough until ready)\n");
        bool wasAvail = dictService_->IsAvailable();
        dictService_->TryRecover();
        bool nowAvail = dictService_->IsAvailable();
        if (!wasAvail && nowAvail) {
            FIRE_LOG(L"[WinFire] OnKeyDown: TryRecover SUCCEEDED, dict now available\n");
        }
    }

    BYTE kb[256];
    GetKeyboardState(kb);
    UINT scan = (UINT)((lParam >> 16) & 0xFF);

    fire::KeyEvent ev = translator_.Translate((UINT)wParam, scan, kb);

    // shiftChecker 状态机：Chrome 等不调用 OnTest* 的宿主需要在此维护。
    // 记事本等调用 OnTest* 的宿主已在 OnTestKeyDown 中维护，此处 idempotent
    //（ModifierKeyUpChecker::OnKeyDown 有 !modifierDown_ 保护，重复调用安全）。
    translator_.shiftChecker.OnKeyDown((UINT)wParam);

    if (!ShouldEat(ev)) {
        *pfEaten = FALSE;
        FIRE_LOG(L"[WinFire] OnKeyDown: not eaten (ShouldEat=false)\n");
        return S_OK;
    }
    bool eaten = ProcessKeyInEditSession(pic, ev);
    *pfEaten = eaten ? TRUE : FALSE;
    FIRE_LOG(L"[WinFire] OnKeyDown: eaten=%d\n", eaten ? 1 : 0);
    return S_OK;
}

STDMETHODIMP CFireTextService::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM /*lParam*/,
                                       BOOL* pfEaten) {
    *pfEaten = FALSE;
    FIRE_LOG(L"[WinFire] OnKeyUp: wParam=0x%lX [tid=%lu]\n",
             (unsigned long)wParam, GetCurrentThreadId());

    // Shift 单击 → 中英文切换。
    // 两种宿主场景：
    //   1) 记事本等：OnTestKeyUp 已检测单击并置位 pendingShiftToggle_，此处执行切换。
    //   2) Chrome 等：不调用 OnTest*，直接调用 OnKeyUp，这里用 shiftChecker 检测单击。
    // shiftChecker.OnKeyUp idempotent（第一次返回 true，重置 modifierDown_；第二次返回 false）。
    if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
        bool doToggle = false;
        if (pendingShiftToggle_) {
            pendingShiftToggle_ = false;
            doToggle = true;
        } else if (!config_.disable_en_mode && translator_.shiftChecker.OnKeyUp((UINT)wParam)) {
            doToggle = true;
        }
        if (doToggle) {
            FIRE_LOG(L"[WinFire] OnKeyUp: Shift single-click, toggling mode\n");
            fire::KeyEvent ev;
            ev.is_modifier_change = true;
            ev.changed_modifier = fire::SpecialKey::ShiftKey;
            ev.toggle_input_mode_request = true;
            ProcessKeyInEditSession(pic, ev);
            *pfEaten = TRUE;
        }
    }

    // CapsLock 组字上屏：组字时按 CapsLock，上屏大写字符（不切模式）。
    // 同样支持两种宿主场景（记事本经 pendingCapsLockCommit_，Chrome 直接检测组字状态）。
    if (wParam == VK_CAPITAL) {
        bool doCommit = false;
        if (pendingCapsLockCommit_) {
            pendingCapsLockCommit_ = false;
            doCommit = true;
        } else if (engine_ && !engine_->original_string().empty()) {
            doCommit = true;
        }
        if (doCommit) {
            bool capsOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
            FIRE_LOG(L"[WinFire] OnKeyUp: CapsLock commit, capsOn=%d\n", capsOn ? 1 : 0);
            if (capsOn) {  // 仅切到大写时上屏大写字符
                fire::KeyEvent ev;
                ev.is_modifier_change = true;
                ev.changed_modifier = fire::SpecialKey::CapsLock;
                ev.caps_lock = true;
                ProcessKeyInEditSession(pic, ev);
            }
            *pfEaten = TRUE;
        }
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
        FIRE_LOG(L"[WinFire] RunInEditSession: abort (threadMgr=%p engine=%p)\n",
                 (void*)threadMgr_.p, (void*)engine_.get());
        return;
    }
    // 取当前焦点文档的顶层上下文
    CComPtr<ITfDocumentMgr> docMgr;
    if (FAILED(threadMgr_->GetFocus(&docMgr)) || !docMgr) {
        FIRE_LOG(L"[WinFire] RunInEditSession: GetFocus failed\n");
        return;
    }
    CComPtr<ITfContext> ctx;
    if (FAILED(docMgr->GetTop(&ctx)) || !ctx) {
        FIRE_LOG(L"[WinFire] RunInEditSession: GetTop failed\n");
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
        FIRE_LOG(L"[WinFire] ProcessKeyInEditSession: pic is null, returning false\n");
        return false;
    }
    KeyEditSession* session = new KeyEditSession(this, pic, ev);
    HRESULT hrSession = S_OK;
    // TF_ES_SYNC 要求同步执行；读写权限用 TF_ES_READWRITE
    HRESULT hr = pic->RequestEditSession(clientId_, session,
                                         TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    FIRE_LOG(L"[WinFire] ProcessKeyInEditSession: RequestEditSession hr=0x%08lX hrSession=0x%08lX\n",
             (unsigned long)hr, (unsigned long)hrSession);
    bool eaten = false;
    if (SUCCEEDED(hr) && SUCCEEDED(hrSession)) {
        eaten = session->eaten_;
    }
    session->Release();
    FIRE_LOG(L"[WinFire] ProcessKeyInEditSession: eaten=%d\n", eaten ? 1 : 0);
    FIRE_LOG_EXIT();
    return eaten;
}

}  // namespace firewin
