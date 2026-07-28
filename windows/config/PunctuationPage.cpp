//
//  PunctuationPage.cpp
//
#include "ConfigApp.h"
#include "PunctuationPage.h"

#include "fire/types.h"

IMPLEMENT_DYNCREATE(CPunctuationPage, CPropertyPage)

BEGIN_MESSAGE_MAP(CPunctuationPage, CPropertyPage)
    ON_CBN_SELCHANGE(IDC_CMB_PUNCT_MODE, &CPunctuationPage::OnPunctModeChanged)
    ON_BN_CLICKED(IDC_BTN_PUNCT_SET, &CPunctuationPage::OnSetPunctValue)
    ON_BN_CLICKED(IDC_BTN_PUNCT_RESET, &CPunctuationPage::OnResetPunct)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST_CUSTOM_PUNCT, &CPunctuationPage::OnListItemChanged)
END_MESSAGE_MAP()

CPunctuationPage::CPunctuationPage() : CPropertyPage(IDD_PAGE_PUNCT) {
    m_punctMode     = (int)g_config.punctuation_mode;
    m_dotAfterNum   = g_config.enable_dot_after_number ? TRUE : FALSE;
    m_punctCommit   = g_config.enable_punctuation_commit ? TRUE : FALSE;
    m_disableEn     = g_config.disable_en_mode ? TRUE : FALSE;
    m_disableTempEn = g_config.disable_temp_en_mode ? TRUE : FALSE;
    m_wsZhEn        = g_config.enable_whitespace_between_zh_en ? TRUE : FALSE;

    // 初始映射：已有自定义配置则用之，否则用默认中文标点作为可编辑起点。
    if (!g_config.custom_punctuation_settings.empty()) {
        m_customPunct = g_config.custom_punctuation_settings;
    } else {
        for (const auto& kv : fire::default_punctuation()) m_customPunct[kv.first] = kv.second;
    }
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
    if (auto* lc = (CListCtrl*)GetDlgItem(IDC_LIST_CUSTOM_PUNCT)) {
        lc->SetExtendedStyle(lc->GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        lc->InsertColumn(0, _T("原字符"), LVCFMT_LEFT, 90);
        lc->InsertColumn(1, _T("输出"), LVCFMT_LEFT, 120);
    }
    ReloadList();
    UpdateCustomUIEnabled();
    return TRUE;
}

void CPunctuationPage::ReloadList() {
    auto* lc = (CListCtrl*)GetDlgItem(IDC_LIST_CUSTOM_PUNCT);
    if (!lc) return;
    lc->DeleteAllItems();
    int row = 0;
    for (const auto& kv : m_customPunct) {
        CString k(CA2W(kv.first.c_str(), CP_UTF8).m_psz);
        CString v(CA2W(kv.second.c_str(), CP_UTF8).m_psz);
        lc->InsertItem(row, k);
        lc->SetItemText(row, 1, v);
        ++row;
    }
}

// 仅在“自定义”模式下允许编辑自定义标点。
void CPunctuationPage::UpdateCustomUIEnabled() {
    bool custom = (m_punctMode == (int)fire::PunctuationMode::Custom);
    if (auto* w = GetDlgItem(IDC_LIST_CUSTOM_PUNCT)) w->EnableWindow(custom);
    if (auto* w = GetDlgItem(IDC_EDIT_PUNCT_VALUE)) w->EnableWindow(custom);
    if (auto* w = GetDlgItem(IDC_BTN_PUNCT_SET)) w->EnableWindow(custom);
    if (auto* w = GetDlgItem(IDC_BTN_PUNCT_RESET)) w->EnableWindow(custom);
}

void CPunctuationPage::OnPunctModeChanged() {
    if (auto* cb = (CComboBox*)GetDlgItem(IDC_CMB_PUNCT_MODE)) {
        int sel = cb->GetCurSel();
        if (sel >= 0) m_punctMode = sel;
    }
    UpdateCustomUIEnabled();
}

// 选中列表某行时，把其输出值填入编辑框，方便修改。
void CPunctuationPage::OnListItemChanged(NMHDR* pNMHDR, LRESULT* pResult) {
    auto* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
    if ((p->uChanged & LVIF_STATE) && (p->uNewState & LVIS_SELECTED)) {
        auto* lc = (CListCtrl*)GetDlgItem(IDC_LIST_CUSTOM_PUNCT);
        if (lc) SetDlgItemText(IDC_EDIT_PUNCT_VALUE, lc->GetItemText(p->iItem, 1));
    }
    *pResult = 0;
}

// 把编辑框内容写回选中行对应的原字符映射。
void CPunctuationPage::OnSetPunctValue() {
    auto* lc = (CListCtrl*)GetDlgItem(IDC_LIST_CUSTOM_PUNCT);
    if (!lc) return;
    POSITION pos = lc->GetFirstSelectedItemPosition();
    if (!pos) {
        MessageBox(_T("请先在列表中选择一个原字符"), _T("提示"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    int sel = lc->GetNextSelectedItem(pos);
    CString key = lc->GetItemText(sel, 0);
    CString val;
    GetDlgItemText(IDC_EDIT_PUNCT_VALUE, val);

    std::string k = CT2A(key, CP_UTF8).m_psz;
    std::string v = CT2A(val, CP_UTF8).m_psz;
    m_customPunct[k] = v;
    lc->SetItemText(sel, 1, val);
}

// 恢复为默认中文标点映射。
void CPunctuationPage::OnResetPunct() {
    m_customPunct.clear();
    for (const auto& kv : fire::default_punctuation()) m_customPunct[kv.first] = kv.second;
    ReloadList();
}

void CPunctuationPage::SyncToConfig() {
    g_config.punctuation_mode = (fire::PunctuationMode)m_punctMode;
    g_config.enable_dot_after_number = m_dotAfterNum != FALSE;
    g_config.enable_punctuation_commit = m_punctCommit != FALSE;
    g_config.disable_en_mode = m_disableEn != FALSE;
    g_config.disable_temp_en_mode = m_disableTempEn != FALSE;
    g_config.enable_whitespace_between_zh_en = m_wsZhEn != FALSE;
    g_config.custom_punctuation_settings = m_customPunct;
}

BOOL CPunctuationPage::OnApply() {
    if (!UpdateData(TRUE)) return FALSE;
    SyncToConfig();
    return CPropertyPage::OnApply();
}
