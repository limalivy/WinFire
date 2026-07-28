//
//  PunctuationPage.h — “标点与中英文”属性页
//
#pragma once
#include <afxdlgs.h>
#include <map>
#include <string>

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

class CPunctuationPage : public CPropertyPage {
    DECLARE_DYNCREATE(CPunctuationPage)
public:
    CPunctuationPage();

    int  m_punctMode = 1;   // 0=enUs 1=zhhans 2=custom
    BOOL m_dotAfterNum = TRUE;
    BOOL m_punctCommit = TRUE;
    BOOL m_disableEn = FALSE;
    BOOL m_disableTempEn = FALSE;
    BOOL m_wsZhEn = TRUE;

protected:
    BOOL OnInitDialog() override;
    void DoDataExchange(CDataExchange* pDX) override;
    BOOL OnApply() override;
    void SyncToConfig();

    afx_msg void OnPunctModeChanged();
    afx_msg void OnSetPunctValue();
    afx_msg void OnResetPunct();
    afx_msg void OnListItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
    DECLARE_MESSAGE_MAP()

private:
    // 当前编辑中的自定义标点映射（原字符 -> 输出），Apply 时写回 g_config。
    std::map<std::string, std::string> m_customPunct;
    void ReloadList();
    void UpdateCustomUIEnabled();
};
