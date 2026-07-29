//
//  DictPage.h — "词库管理"属性页（导入码表 / 生成 sqlite / 编辑用户词库）（纯 Win32）
//
#pragma once
#ifndef RC_INVOKED
#include "UiBase.h"
#endif

#define IDD_PAGE_DICT            2003
#define IDC_EDIT_WB_TABLE        2301
#define IDC_EDIT_PY_TABLE        2302
#define IDC_BTN_BROWSE_WB        2303
#define IDC_BTN_BROWSE_PY        2304
#define IDC_BTN_BUILD_DICT       2305
#define IDC_BTN_EDIT_USERDICT    2306
#define IDC_STATIC_BUILD_STATUS  2307

class CDictPage : public PageBase {
public:
    CDictPage();

    std::wstring m_wbTablePath;
    std::wstring m_pyTablePath;

protected:
    void OnInitDialog() override;
    void OnCommand(WPARAM wParam, LPARAM lParam) override;
};

HPROPSHEETPAGE CreateDictPage(CDictPage& page);
