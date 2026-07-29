//
//  PunctuationPage.h — "标点与中英文"属性页（纯 Win32）
//
#pragma once
#ifndef RC_INVOKED
#include "UiBase.h"
#endif
#include <string>
#include <unordered_map>

#define IDD_PAGE_PUNCT           2002
#define IDC_CMB_PUNCT_MODE       2201
#define IDC_CHK_DOT_AFTER_NUM    2202
#define IDC_CHK_PUNCT_COMMIT     2203
#define IDC_CHK_DISABLE_EN       2204
#define IDC_CHK_DISABLE_TEMP_EN  2205
#define IDC_CHK_WS_ZH_EN         2206
// 自定义标点编辑
#define IDC_LIST_CUSTOM_PUNCT    2207
#define IDC_EDIT_PUNCT_VALUE     2208
#define IDC_BTN_PUNCT_SET        2209
#define IDC_BTN_PUNCT_RESET      2210

class CPunctuationPage : public PageBase {
public:
    CPunctuationPage();

    int  m_punctMode = 1;   // 0=enUs 1=zhhans 2=custom
    BOOL m_dotAfterNum = TRUE;
    BOOL m_punctCommit = TRUE;
    BOOL m_disableEn = FALSE;
    BOOL m_disableTempEn = FALSE;
    BOOL m_wsZhEn = TRUE;

protected:
    void OnInitDialog() override;
    bool OnApply() override;
    void OnCommand(WPARAM wParam, LPARAM lParam) override;
    void OnNotify(LPNMHDR nm, LRESULT* pResult) override;
    void SyncToConfig();

private:
    // 当前编辑中的自定义标点映射（原字符 -> 输出），Apply 时写回 g_config。
    std::unordered_map<std::string, std::string> m_customPunct;
    void ReloadList();
    void UpdateCustomUIEnabled();
};

HPROPSHEETPAGE CreatePunctuationPage(CPunctuationPage& page);
