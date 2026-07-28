//
//  LangBarButton.h — Windows 语言栏按钮（ITfLangBarItemButton）
//
//  替代 macOS 版的状态栏图标（StatusBarController.swift）：
//  在系统语言栏显示「中/英」状态，点击可切换中英文输入模式。
//
#pragma once

#include <windows.h>
#include <msctf.h>
#include <ctffunc.h>
#include <string>

namespace firewin {

class CFireTextService;

// 语言栏按钮：显示当前输入模式（中/英），左键点击切换。
class CFireLangBarButton : public ITfLangBarItemButton, public ITfSource {
public:
    explicit CFireLangBarButton(CFireTextService* service);
    ~CFireLangBarButton();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo) override;
    STDMETHODIMP GetStatus(DWORD* pdwStatus) override;
    STDMETHODIMP Show(BOOL fShow) override;
    STDMETHODIMP GetTooltipString(BSTR* pbstrToolTip) override;

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) override;
    STDMETHODIMP InitMenu(ITfMenu* pMenu) override;
    STDMETHODIMP OnMenuSelect(UINT wID) override;
    STDMETHODIMP GetIcon(HICON* phIcon) override;
    STDMETHODIMP GetText(BSTR* pbstrText) override;

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) override;
    STDMETHODIMP UnadviseSink(DWORD dwCookie) override;

    // 由 TextService 在输入模式变化后调用，刷新按钮文本
    void UpdateModeText();

private:
    // 菜单项 ID
    enum { kMenuZh = 1, kMenuEn = 2 };

    LONG ref_ = 1;
    CFireTextService* service_ = nullptr;

    TF_LANGBARITEMINFO info_{};
    ITfLangBarItemSink* sink_ = nullptr;
    DWORD sinkCookie_ = TF_INVALID_COOKIE;

    std::wstring CurrentModeText() const;  // L"中" / L"英"
};

}  // namespace firewin
