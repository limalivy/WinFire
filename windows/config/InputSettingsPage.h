//
//  InputSettingsPage.h — "输入设置"属性页（纯 Win32）
//
#pragma once
#ifndef RC_INVOKED
#include "UiBase.h"
#endif

// 控件 ID（对应 .rc 中的资源）
#define IDD_PAGE_INPUT           2001
#define IDC_CHK_WORD_INPUT       2101
#define IDC_CHK_DYNAMIC_FREQ     2102
#define IDC_CHK_Z_KEY_QUERY      2103
#define IDC_CHK_SHOW_CODE        2104
#define IDC_EDIT_CAND_COUNT      2105
#define IDC_CMB_CODE_MODE        2106
#define IDC_CMB_CAND_DIRECTION   2107
#define IDC_CMB_WUBI_DING        2108
#define IDC_CHK_WUBI_CODE_TIP    2109
#define IDC_CHK_WUBI_AUTO_COMMIT 2110
#define IDC_CMB_TOGGLE_KEY       2111

class CInputSettingsPage : public PageBase {
public:
    CInputSettingsPage();

    BOOL m_wordInput = TRUE;
    BOOL m_dynamicFreq = FALSE;
    BOOL m_zKeyQuery = TRUE;
    BOOL m_showCode = TRUE;
    int  m_candCount = 5;
    int  m_codeMode = 2;       // 0=wubi 1=pinyin 2=wubiPinyin
    int  m_candDirection = 1;  // 0=vertical 1=horizontal 2=none
    int  m_wubiDing = 0;       // 0=none 1=35 2=52 3=53
    BOOL m_wubiCodeTip = TRUE;
    BOOL m_wubiAutoCommit = FALSE;
    int  m_toggleKey = 0;      // 0=Shift 1=Control 2=Command(Win) 3=Option(Alt)

protected:
    void OnInitDialog() override;
    bool OnApply() override;
    void SyncToConfig();
};

// 工厂：创建属性页句柄（供 PropertySheet 注册）
HPROPSHEETPAGE CreateInputSettingsPage(CInputSettingsPage& page);
