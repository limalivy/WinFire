//
//  PunctuationPage.h — “标点与中英文”属性页
//
#pragma once
#include <afxdlgs.h>

#define IDD_PAGE_PUNCT           2002
#define IDC_CMB_PUNCT_MODE       2201
#define IDC_CHK_DOT_AFTER_NUM    2202
#define IDC_CHK_PUNCT_COMMIT     2203
#define IDC_CHK_DISABLE_EN       2204
#define IDC_CHK_DISABLE_TEMP_EN  2205
#define IDC_CHK_WS_ZH_EN         2206

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
    DECLARE_MESSAGE_MAP()
};
