//
//  LangBarButton.cpp
//
#include "LangBarButton.h"
#include "DebugLog.h"
#include "TextService.h"

#include <olectl.h>

namespace firewin {

CFireLangBarButton::CFireLangBarButton(CFireTextService* service) : service_(service) {
    FIRE_LOG_ENTER();
    FIRE_LOG(L"[FireIME] LangBarButton ctor: service=%p [tid=%lu]\n",
             (void*)service, GetCurrentThreadId());
    if (service_) service_->AddRef();
    info_.clsidService = CLSID_FireTextService;
    info_.guidItem = GUID_FireLangBarButton;
    info_.dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
    info_.ulSort = 0;
    lstrcpynW(info_.szDescription, FIRE_TEXTSERVICE_DESC, ARRAYSIZE(info_.szDescription));
    DllAddRef();
    FIRE_LOG_EXIT();
}

CFireLangBarButton::~CFireLangBarButton() {
    FIRE_LOG_ENTER();
    if (sink_) {
        sink_->Release();
        sink_ = nullptr;
    }
    if (service_) service_->Release();
    DllRelease();
    FIRE_LOG_EXIT();
}

// ---- IUnknown ----
STDMETHODIMP CFireLangBarButton::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_ITfLangBarItem || riid == IID_ITfLangBarItemButton) {
        *ppv = static_cast<ITfLangBarItemButton*>(this);
    } else if (riid == IID_ITfSource) {
        *ppv = static_cast<ITfSource*>(this);
    }
    if (*ppv) {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CFireLangBarButton::AddRef() { return InterlockedIncrement(&ref_); }
STDMETHODIMP_(ULONG) CFireLangBarButton::Release() {
    LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return r;
}

// ---- ITfLangBarItem ----
STDMETHODIMP CFireLangBarButton::GetInfo(TF_LANGBARITEMINFO* pInfo) {
    if (!pInfo) return E_INVALIDARG;
    *pInfo = info_;
    return S_OK;
}

STDMETHODIMP CFireLangBarButton::GetStatus(DWORD* pdwStatus) {
    if (!pdwStatus) return E_INVALIDARG;
    *pdwStatus = 0;  // 可见、可用
    return S_OK;
}

STDMETHODIMP CFireLangBarButton::Show(BOOL /*fShow*/) { return S_OK; }

STDMETHODIMP CFireLangBarButton::GetTooltipString(BSTR* pbstrToolTip) {
    if (!pbstrToolTip) return E_INVALIDARG;
    *pbstrToolTip = SysAllocString(L"业火五笔：点击切换中/英文");
    return *pbstrToolTip ? S_OK : E_OUTOFMEMORY;
}

// ---- ITfLangBarItemButton ----
STDMETHODIMP CFireLangBarButton::OnClick(TfLBIClick click, POINT /*pt*/, const RECT* /*prcArea*/) {
    FIRE_LOG(L"[FireIME] LangBar OnClick: click=%d service=%p\n", (int)click, (void*)service_);
    if (click == TF_LBI_CLK_LEFT && service_) {
        service_->ToggleInputModeFromLangBar();
    }
    return S_OK;
}

STDMETHODIMP CFireLangBarButton::InitMenu(ITfMenu* pMenu) {
    FIRE_LOG_ENTER();
    if (!pMenu) return E_INVALIDARG;
    pMenu->AddMenuItem(kMenuZh, 0, nullptr, nullptr, L"中文", 2, nullptr);
    pMenu->AddMenuItem(kMenuEn, 0, nullptr, nullptr, L"英文", 2, nullptr);
    FIRE_LOG_EXIT();
    return S_OK;
}

STDMETHODIMP CFireLangBarButton::OnMenuSelect(UINT wID) {
    FIRE_LOG(L"[FireIME] LangBar OnMenuSelect: wID=%u service=%p\n", wID, (void*)service_);
    if (!service_) return S_OK;
    if (wID == kMenuZh) {
        service_->SetInputModeFromLangBar(fire::InputMode::ZhHans);
    } else if (wID == kMenuEn) {
        service_->SetInputModeFromLangBar(fire::InputMode::EnUS);
    }
    return S_OK;
}

STDMETHODIMP CFireLangBarButton::GetIcon(HICON* phIcon) {
    if (!phIcon) return E_INVALIDARG;
    *phIcon = nullptr;  // 无自定义图标，使用文本
    return S_OK;
}

STDMETHODIMP CFireLangBarButton::GetText(BSTR* pbstrText) {
    FIRE_LOG_ENTER();
    if (!pbstrText) return E_INVALIDARG;
    std::wstring t = CurrentModeText();
    *pbstrText = SysAllocString(t.c_str());
    FIRE_LOG(L"[FireIME] LangBar GetText: text='%ws'\n", t.c_str());
    FIRE_LOG_EXIT();
    return *pbstrText ? S_OK : E_OUTOFMEMORY;
}

// ---- ITfSource ----
STDMETHODIMP CFireLangBarButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) {
    FIRE_LOG_ENTER();
    if (!punk || !pdwCookie) return E_INVALIDARG;
    if (riid != IID_ITfLangBarItemSink) return CONNECT_E_CANNOTCONNECT;
    if (sink_) return CONNECT_E_ADVISELIMIT;
    if (FAILED(punk->QueryInterface(IID_ITfLangBarItemSink, (void**)&sink_))) {
        sink_ = nullptr;
        return E_NOINTERFACE;
    }
    // 用非 0 的固定 cookie，避免与 TF_INVALID_COOKIE(0) 混淆，使 UnadviseSink 校验严格。
    sinkCookie_ = 1;
    *pdwCookie = sinkCookie_;
    FIRE_LOG(L"[FireIME] LangBar AdviseSink: OK cookie=%lu\n", (unsigned long)sinkCookie_);
    FIRE_LOG_EXIT();
    return S_OK;
}

STDMETHODIMP CFireLangBarButton::UnadviseSink(DWORD dwCookie) {
    FIRE_LOG_ENTER();
    if (dwCookie != sinkCookie_ || !sink_) return CONNECT_E_NOCONNECTION;
    sink_->Release();
    sink_ = nullptr;
    sinkCookie_ = TF_INVALID_COOKIE;
    FIRE_LOG_EXIT();
    return S_OK;
}

void CFireLangBarButton::UpdateModeText() {
    FIRE_LOG_ENTER();
    // 通知语言栏刷新按钮文本
    if (sink_) {
        sink_->OnUpdate(TF_LBI_TEXT | TF_LBI_ICON);
    }
    FIRE_LOG_EXIT();
}

std::wstring CFireLangBarButton::CurrentModeText() const {
    if (service_ && service_->CurrentInputMode() == fire::InputMode::EnUS) {
        return L"英";
    }
    return L"中";
}

}  // namespace firewin
