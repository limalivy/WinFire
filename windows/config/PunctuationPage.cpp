//
//  PunctuationPage.cpp — 纯 Win32 实现
//
#include "ConfigApp.h"
#include "PunctuationPage.h"

#include "fire/types.h"

CPunctuationPage::CPunctuationPage() {
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

void CPunctuationPage::OnInitDialog() {
    // 复选框
    firecfg::SetCheck(hwnd, IDC_CHK_DOT_AFTER_NUM, m_dotAfterNum);
    firecfg::SetCheck(hwnd, IDC_CHK_PUNCT_COMMIT, m_punctCommit);
    firecfg::SetCheck(hwnd, IDC_CHK_DISABLE_EN, m_disableEn);
    firecfg::SetCheck(hwnd, IDC_CHK_DISABLE_TEMP_EN, m_disableTempEn);
    firecfg::SetCheck(hwnd, IDC_CHK_WS_ZH_EN, m_wsZhEn);

    // 标点模式下拉框
    firecfg::CbReset(hwnd, IDC_CMB_PUNCT_MODE);
    firecfg::CbAdd(hwnd, IDC_CMB_PUNCT_MODE, L"半角");
    firecfg::CbAdd(hwnd, IDC_CMB_PUNCT_MODE, L"全角");
    firecfg::CbAdd(hwnd, IDC_CMB_PUNCT_MODE, L"自定义");
    firecfg::CbSetSel(hwnd, IDC_CMB_PUNCT_MODE, m_punctMode);

    // 自定义标点列表列
    firecfg::LvInitColumns(hwnd, IDC_LIST_CUSTOM_PUNCT);
    firecfg::LvAddColumn(hwnd, IDC_LIST_CUSTOM_PUNCT, 0, L"原字符", 90);
    firecfg::LvAddColumn(hwnd, IDC_LIST_CUSTOM_PUNCT, 1, L"输出", 120);

    ReloadList();
    UpdateCustomUIEnabled();
}

void CPunctuationPage::ReloadList() {
    firecfg::LvClear(hwnd, IDC_LIST_CUSTOM_PUNCT);
    int row = 0;
    for (const auto& kv : m_customPunct) {
        std::wstring k = firecfg::Utf8ToWide(kv.first);
        std::wstring v = firecfg::Utf8ToWide(kv.second);
        firecfg::LvInsertItem(hwnd, IDC_LIST_CUSTOM_PUNCT, row, k.c_str());
        firecfg::LvSetItem(hwnd, IDC_LIST_CUSTOM_PUNCT, row, 1, v.c_str());
        ++row;
    }
}

// 仅在"自定义"模式下允许编辑自定义标点。
void CPunctuationPage::UpdateCustomUIEnabled() {
    bool custom = (m_punctMode == (int)fire::PunctuationMode::Custom);
    Enable(IDC_LIST_CUSTOM_PUNCT, custom);
    Enable(IDC_EDIT_PUNCT_VALUE, custom);
    Enable(IDC_BTN_PUNCT_SET, custom);
    Enable(IDC_BTN_PUNCT_RESET, custom);
}

void CPunctuationPage::OnCommand(WPARAM wParam, LPARAM /*lParam*/) {
    int code = HIWORD(wParam);
    int id = LOWORD(wParam);
    if (id == IDC_CMB_PUNCT_MODE && code == CBN_SELCHANGE) {
        int sel = firecfg::CbGetSel(hwnd, IDC_CMB_PUNCT_MODE);
        if (sel >= 0) m_punctMode = sel;
        UpdateCustomUIEnabled();
    } else if (id == IDC_BTN_PUNCT_SET && code == BN_CLICKED) {
        // 把编辑框内容写回选中行对应的原字符映射。
        int sel = firecfg::LvGetSelItem(hwnd, IDC_LIST_CUSTOM_PUNCT);
        if (sel < 0) {
            MsgBox(L"请先在列表中选择一个原字符", L"提示", MB_OK | MB_ICONINFORMATION);
            return;
        }
        std::wstring key = firecfg::LvGetItemText(hwnd, IDC_LIST_CUSTOM_PUNCT, sel, 0);
        std::wstring val = firecfg::GetText(hwnd, IDC_EDIT_PUNCT_VALUE);
        m_customPunct[firecfg::WideToUtf8(key)] = firecfg::WideToUtf8(val);
        firecfg::LvSetItem(hwnd, IDC_LIST_CUSTOM_PUNCT, sel, 1, val.c_str());
    } else if (id == IDC_BTN_PUNCT_RESET && code == BN_CLICKED) {
        // 恢复为默认中文标点映射。
        m_customPunct.clear();
        for (const auto& kv : fire::default_punctuation()) m_customPunct[kv.first] = kv.second;
        ReloadList();
    }
}

void CPunctuationPage::OnNotify(LPNMHDR nm, LRESULT* pResult) {
    if (nm->idFrom == IDC_LIST_CUSTOM_PUNCT && nm->code == LVN_ITEMCHANGED) {
        auto* p = reinterpret_cast<NMLISTVIEW*>(nm);
        if ((p->uChanged & LVIF_STATE) && (p->uNewState & LVIS_SELECTED)) {
            // 选中列表某行时，把其输出值填入编辑框，方便修改。
            std::wstring v = firecfg::LvGetItemText(hwnd, IDC_LIST_CUSTOM_PUNCT, p->iItem, 1);
            firecfg::SetText(hwnd, IDC_EDIT_PUNCT_VALUE, v);
        }
    }
    *pResult = 0;
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

bool CPunctuationPage::OnApply() {
    m_dotAfterNum   = firecfg::GetCheck(hwnd, IDC_CHK_DOT_AFTER_NUM);
    m_punctCommit   = firecfg::GetCheck(hwnd, IDC_CHK_PUNCT_COMMIT);
    m_disableEn     = firecfg::GetCheck(hwnd, IDC_CHK_DISABLE_EN);
    m_disableTempEn = firecfg::GetCheck(hwnd, IDC_CHK_DISABLE_TEMP_EN);
    m_wsZhEn        = firecfg::GetCheck(hwnd, IDC_CHK_WS_ZH_EN);
    m_punctMode     = firecfg::CbGetSel(hwnd, IDC_CMB_PUNCT_MODE);
    if (m_punctMode < 0) m_punctMode = 1;
    SyncToConfig();
    return true;
}

HPROPSHEETPAGE CreatePunctuationPage(CPunctuationPage& page) {
    PROPSHEETPAGEW psp = {0};
    psp.dwSize = sizeof(psp);
    psp.dwFlags = PSP_DEFAULT;
    psp.hInstance = GetModuleHandleW(nullptr);
    psp.pszTemplate = MAKEINTRESOURCEW(IDD_PAGE_PUNCT);
    psp.pfnDlgProc = PageDlgProc;
    psp.lParam = (LPARAM)&page;
    return CreatePropertySheetPageW(&psp);
}
