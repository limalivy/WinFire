//
//  PunctuationPage.cpp
//
#include "ConfigApp.h"
#include "PunctuationPage.h"

IMPLEMENT_DYNCREATE(CPunctuationPage, CPropertyPage)

BEGIN_MESSAGE_MAP(CPunctuationPage, CPropertyPage)
END_MESSAGE_MAP()

CPunctuationPage::CPunctuationPage() : CPropertyPage(IDD_PAGE_PUNCT) {
    m_punctMode     = (int)g_config.punctuation_mode;
    m_dotAfterNum   = g_config.enable_dot_after_number ? TRUE : FALSE;
    m_punctCommit   = g_config.enable_punctuation_commit ? TRUE : FALSE;
    m_disableEn     = g_config.disable_en_mode ? TRUE : FALSE;
    m_disableTempEn = g_config.disable_temp_en_mode ? TRUE : FALSE;
    m_wsZhEn        = g_config.enable_whitespace_between_zh_en ? TRUE : FALSE;
}

void CPunctuationPage::DoDataExchange(CDataExchange* pDX) {
    CPropertyPage::DoDataExchange(pDX);
    DDX_CBIndex(pDX, IDC_CMB_PUNCT_MODE, m_punctMode);
    DDX_Check(pDX, IDC_CHK_DOT_AFTER_NUM, m_dotAfterNum);
    DDX_Check(pDX, IDC_CHK_PUNCT_COMMIT, m_punctCommit);
    DDX_Check(pDX, IDC_CHK_DISABLE_EN, m_disableEn);
    DDX_Check(pDX, IDC_CHK_DISABLE_TEMP_EN, m_disableTempEn);
    DDX_Check(pDX, IDC_CHK_WS_ZH_EN, m_wsZhEn);
}

BOOL CPunctuationPage::OnInitDialog() {
    CPropertyPage::OnInitDialog();
    if (auto* cb = (CComboBox*)GetDlgItem(IDC_CMB_PUNCT_MODE)) {
        cb->ResetContent();
        cb->AddString(_T("半角"));
        cb->AddString(_T("全角"));
        cb->AddString(_T("自定义"));
        cb->SetCurSel(m_punctMode);
    }
    return TRUE;
}

void CPunctuationPage::SyncToConfig() {
    g_config.punctuation_mode = (fire::PunctuationMode)m_punctMode;
    g_config.enable_dot_after_number = m_dotAfterNum != FALSE;
    g_config.enable_punctuation_commit = m_punctCommit != FALSE;
    g_config.disable_en_mode = m_disableEn != FALSE;
    g_config.disable_temp_en_mode = m_disableTempEn != FALSE;
    g_config.enable_whitespace_between_zh_en = m_wsZhEn != FALSE;
}

BOOL CPunctuationPage::OnApply() {
    if (!UpdateData(TRUE)) return FALSE;
    SyncToConfig();
    return CPropertyPage::OnApply();
}
