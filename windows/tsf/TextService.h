//
//  TextService.h — 微火输入法 TSF Text Input Processor（ATL 纯原生 COM）
//
//  实现的 TSF 接口：
//    ITfTextInputProcessorEx  —— 激活/停用
//    ITfKeyEventSink          —— 按键钩子（核心）
//    ITfThreadMgrEventSink    —— 焦点/文档切换
//    ITfCompositionSink       —— 组字被外部终止的回调
//
#pragma once

#include <windows.h>
#include <msctf.h>
#include <atlbase.h>
#include <atlcom.h>
#include <functional>
#include <memory>
#include <string>

#include "Globals.h"
#include "KeyEventTranslator.h"
#include "TsfInputClient.h"

#include "fire/config.h"
#include "fire/dict_service.h"
#include "fire/input_engine.h"

namespace firewin {

class CandidateWindowController;
class CFireLangBarButton;

class ATL_NO_VTABLE CFireTextService
    : public CComObjectRootEx<CComSingleThreadModel>,
      public CComCoClass<CFireTextService, &CLSID_FireTextService>,
      public ITfTextInputProcessorEx,
      public ITfThreadMgrEventSink,
      public ITfKeyEventSink,
      public ITfCompositionSink {
public:
    CFireTextService();
    ~CFireTextService();

    // ATL COM map
    BEGIN_COM_MAP(CFireTextService)
        COM_INTERFACE_ENTRY(ITfTextInputProcessor)
        COM_INTERFACE_ENTRY(ITfTextInputProcessorEx)
        COM_INTERFACE_ENTRY(ITfThreadMgrEventSink)
        COM_INTERFACE_ENTRY(ITfKeyEventSink)
        COM_INTERFACE_ENTRY(ITfCompositionSink)
    END_COM_MAP()

    DECLARE_NO_REGISTRY()  // 注册走自定义 DllRegisterServer

    // ---- ITfTextInputProcessor / Ex ----
    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
    STDMETHODIMP Deactivate() override;
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) override;

    // ---- ITfThreadMgrEventSink ----
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* pdimPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext*) override { return S_OK; }
    STDMETHODIMP OnPopContext(ITfContext*) override { return S_OK; }

    // ---- ITfKeyEventSink ----
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

    // ---- ITfCompositionSink ----
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) override;

    // ---- 供语言栏按钮（CFireLangBarButton）调用 ----
    void ToggleInputModeFromLangBar();
    void SetInputModeFromLangBar(fire::InputMode mode);
    fire::InputMode CurrentInputMode() const;

private:
    // 引擎/词库/配置（每个 TIP 实例一份）
    fire::Config config_;
    // 查字/统计服务：经 IPC 转发给 fire_dictd.exe（DictIpcProxy）。
    // 后台不可用时引擎降级透传（不再回退到本进程直接查库）。
    std::unique_ptr<fire::IDictService> dictService_;
    std::unique_ptr<fire::InputEngine> engine_;
    TsfInputClient inputClient_;
    KeyEventTranslator translator_;
    std::unique_ptr<CandidateWindowController> candWindow_;

    CComPtr<ITfThreadMgr> threadMgr_;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    DWORD threadMgrCookie_ = TF_INVALID_COOKIE;

    // 语言栏按钮（中/英状态）
    CFireLangBarButton* langBar_ = nullptr;

    // per-app 输入模式：记录上一个获得焦点的应用标识，失焦时保存
    std::string currentAppId_;

    // Shift 单击切换中英文：shiftChecker 状态机在 OnTestKeyDown/OnTestKeyUp 中
    // 维护（OnTest* 每次按键必被调用，而 OnKeyDown/OnKeyUp 仅在 eat=TRUE 时才调用，
    // 裸 Shift 的 ShouldEat=FALSE → OnKeyDown 收不到）。检测到单击后置位此标志，
    // 在（因吃掉 Shift 抬起而必被调用的）OnKeyUp 中执行真正切换。
    bool pendingShiftToggle_ = false;

    // CapsLock 组字上屏：组字时按 CapsLock，OnTestKeyUp 置位此标志，
    // OnKeyUp 中发送 caps_lock 事件给引擎上屏大写字符（不切模式）。
    bool pendingCapsLockCommit_ = false;

    // config 收敛到 dictd：DLL 不再 stat config.json（消除定时轮询）。
    // 仅 Activate 时 LoadConfigFromDisk 一次 bootstrap 兜底；之后 config 经 IPC 拿
    //（CacheValidate 响应里 dictd 回传全量 config_json，token 不一致时才有值）。
    // 上次执行 config IPC 校验的时刻（ms），用于节流：两次校验间隔至少
    // kConfigCheckIntervalMs，避免快速打字时每键一次 IPC 往返（仍是零磁盘 IO）。
    ULONGLONG lastConfigCheckTick_ = 0;

    void InitEngine();
    void LoadConfigFromDisk();  // 读取 config.json（仅 Activate bootstrap 用一次）
    // 配置热加载（零轮询）：节流到期后直接 ValidateCache（IPC，不读盘）。
    // dictd 比对 config_token，不一致时回传全量 config_json，回调里原地填 config_。
    // 在 OnKeyDown 入口调用，作为「用户长时间不切应用连续打字」期间的兜底。
    void MaybeReloadConfig();
    // 切应用即时同步 config（绕过节流）：config.exe 是独立窗口，改完设置切回
    // 目标应用时 OnSetFocus(appChanged) 必然触发，复用节流会让此场景被跳过而失效。
    void ReloadConfigNow();
    void RegisterLangBarButton();
    void UnregisterLangBarButton();
    void RefreshLangBar();

    // 在一次 EditSession 中处理按键（真正修改文档必须在 EditSession 内）
    bool ProcessKeyInEditSession(ITfContext* pic, const fire::KeyEvent& ev);

    // 在当前焦点上下文的读写 EditSession 内执行一个动作（用于候选窗点击/翻页等
    // 非按键路径：这些路径不在任何 EditSession 里，直接改文档会用到失效 cookie）。
    void RunInEditSession(std::function<void()> action);

    // 判断是否需要吃掉该键（用于 OnTestKeyDown）：中文模式下的可见字符/功能键
    bool ShouldEat(const fire::KeyEvent& ev) const;

    friend class KeyEditSession;    // 内部按键 EditSession 实现
    friend class ActionEditSession;  // 内部通用动作 EditSession 实现
    friend bool InitEngineSafe(CFireTextService* svc);  // SEH 崩溃保护包装器
};

}  // namespace firewin
