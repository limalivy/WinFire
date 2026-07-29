//
//  AppModePage.h — "按应用输入模式"属性页（纯 Win32）
//
//  对应 macOS 版 ApplicationPane.swift：为不同宿主应用（Windows 用进程 exe 名）
//  设置固定输入模式，并可开启"保持应用最后使用的输入模式"。
//
#pragma once
#ifndef RC_INVOKED
#include "UiBase.h"
#endif

#define IDD_PAGE_APPMODE         2004
#define IDC_CHK_KEEP_APP_MODE    2401
#define IDC_CMB_TIP_SHOW_TIME    2402
#define IDC_LIST_APPS            2403
#define IDC_EDIT_APP_ID          2404
#define IDC_CMB_APP_MODE         2405
#define IDC_BTN_APP_ADD          2406
#define IDC_BTN_APP_REMOVE       2407

class CAppModePage : public PageBase {
public:
    CAppModePage();

    BOOL m_keepAppMode = FALSE;
    int  m_tipShowTime = 0;   // 0=OnlyChanged 1=Always 2=None

protected:
    void OnInitDialog() override;
    bool OnApply() override;
    void OnCommand(WPARAM wParam, LPARAM lParam) override;
    void SyncToConfig();

private:
    void ReloadList();
};

HPROPSHEETPAGE CreateAppModePage(CAppModePage& page);
