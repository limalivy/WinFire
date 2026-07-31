//
//  InputSettingsPage.cpp — 纯 Win32 实现
//
#include "ConfigApp.h"
#include "InputSettingsPage.h"

namespace {
// 组合框下标 <-> fire::ModifierKey 的映射（只暴露常用 4 项）
const fire::ModifierKey kToggleKeys[] = {
    fire::ModifierKey::Shift,
    fire::ModifierKey::Control,
    fire::ModifierKey::Command,
    fire::ModifierKey::Option,
};
int ToggleKeyToIndex(fire::ModifierKey k) {
    for (int i = 0; i < 4; ++i) if (kToggleKeys[i] == k) return i;
    return 0;
}
}  // namespace

CInputSettingsPage::CInputSettingsPage() {
    // 从全局配置初始化成员变量
    m_wordInput   = g_config.enable_word_input ? TRUE : FALSE;
    m_dynamicFreq = g_config.enable_dynamic_frequency ? TRUE : FALSE;
    m_zKeyQuery   = g_config.z_key_query ? TRUE : FALSE;
    m_showCode    = g_config.show_code_in_window ? TRUE : FALSE;
    m_candCount   = g_config.candidate_count;
    m_codeMode    = (int)g_config.code_mode;
    m_candDirection = (int)g_config.candidates_direction;
    m_wubiDing    = (int)g_config.wubi_ding_mode;
    m_wubiCodeTip = g_config.wubi_code_tip ? TRUE : FALSE;
    m_wubiAutoCommit = g_config.wubi_auto_commit ? TRUE : FALSE;
    m_toggleKey   = ToggleKeyToIndex(g_config.toggle_input_mode_key);
}

void CInputSettingsPage::OnInitDialog() {
    // 复选框：成员变量 -> UI
    firecfg::SetCheck(hwnd, IDC_CHK_WORD_INPUT, m_wordInput);
    firecfg::SetCheck(hwnd, IDC_CHK_DYNAMIC_FREQ, m_dynamicFreq);
    firecfg::SetCheck(hwnd, IDC_CHK_Z_KEY_QUERY, m_zKeyQuery);
    firecfg::SetCheck(hwnd, IDC_CHK_SHOW_CODE, m_showCode);
    firecfg::SetCheck(hwnd, IDC_CHK_WUBI_CODE_TIP, m_wubiCodeTip);
    firecfg::SetCheck(hwnd, IDC_CHK_WUBI_AUTO_COMMIT, m_wubiAutoCommit);
    // 数字编辑框
    SetDlgItemInt(hwnd, IDC_EDIT_CAND_COUNT, m_candCount, FALSE);

    // 填充下拉框
    firecfg::CbReset(hwnd, IDC_CMB_CODE_MODE);
    firecfg::CbAdd(hwnd, IDC_CMB_CODE_MODE, L"纯五笔");
    firecfg::CbAdd(hwnd, IDC_CMB_CODE_MODE, L"纯拼音");
    firecfg::CbAdd(hwnd, IDC_CMB_CODE_MODE, L"五笔拼音混输");
    firecfg::CbSetSel(hwnd, IDC_CMB_CODE_MODE, m_codeMode);

    firecfg::CbReset(hwnd, IDC_CMB_CAND_DIRECTION);
    firecfg::CbAdd(hwnd, IDC_CMB_CAND_DIRECTION, L"竖排");
    firecfg::CbAdd(hwnd, IDC_CMB_CAND_DIRECTION, L"横排");
    firecfg::CbAdd(hwnd, IDC_CMB_CAND_DIRECTION, L"不显示");
    firecfg::CbSetSel(hwnd, IDC_CMB_CAND_DIRECTION, m_candDirection);

    firecfg::CbReset(hwnd, IDC_CMB_WUBI_DING);
    firecfg::CbAdd(hwnd, IDC_CMB_WUBI_DING, L"关闭");
    firecfg::CbAdd(hwnd, IDC_CMB_WUBI_DING, L"35 顶");
    firecfg::CbAdd(hwnd, IDC_CMB_WUBI_DING, L"52 顶");
    firecfg::CbAdd(hwnd, IDC_CMB_WUBI_DING, L"53 顶");
    firecfg::CbSetSel(hwnd, IDC_CMB_WUBI_DING, m_wubiDing);

    firecfg::CbReset(hwnd, IDC_CMB_TOGGLE_KEY);
    firecfg::CbAdd(hwnd, IDC_CMB_TOGGLE_KEY, L"Shift");
    firecfg::CbAdd(hwnd, IDC_CMB_TOGGLE_KEY, L"Ctrl");
    firecfg::CbAdd(hwnd, IDC_CMB_TOGGLE_KEY, L"Win");
    firecfg::CbAdd(hwnd, IDC_CMB_TOGGLE_KEY, L"Alt");
    firecfg::CbSetSel(hwnd, IDC_CMB_TOGGLE_KEY, m_toggleKey);

    // 顶字提示初始可见性（编码方案非纯五笔时显示）
    UpdateDingHintVisibility();
}

void CInputSettingsPage::OnCommand(WPARAM wParam, LPARAM lParam) {
    // 编码方案下拉变化时，实时刷新顶字提示可见性
    if (LOWORD(wParam) == IDC_CMB_CODE_MODE && HIWORD(wParam) == CBN_SELCHANGE) {
        UpdateDingHintVisibility();
    }
}

void CInputSettingsPage::UpdateDingHintVisibility() {
    // 纯五笔 = index 0；其它方案（纯拼音 / 混输）下顶字不生效，显示提示。
    int sel = firecfg::CbGetSel(hwnd, IDC_CMB_CODE_MODE);
    HWND hint = GetDlgItem(hwnd, IDC_STATIC_DING_HINT);
    if (hint) {
        ShowWindow(hint, sel == 0 ? SW_HIDE : SW_SHOW);
    }
}

bool CInputSettingsPage::OnApply() {
    // UI -> 成员变量
    m_wordInput   = firecfg::GetCheck(hwnd, IDC_CHK_WORD_INPUT);
    m_dynamicFreq = firecfg::GetCheck(hwnd, IDC_CHK_DYNAMIC_FREQ);
    m_zKeyQuery   = firecfg::GetCheck(hwnd, IDC_CHK_Z_KEY_QUERY);
    m_showCode    = firecfg::GetCheck(hwnd, IDC_CHK_SHOW_CODE);
    m_wubiCodeTip = firecfg::GetCheck(hwnd, IDC_CHK_WUBI_CODE_TIP);
    m_wubiAutoCommit = firecfg::GetCheck(hwnd, IDC_CHK_WUBI_AUTO_COMMIT);
    m_candCount   = (int)GetDlgItemInt(hwnd, IDC_EDIT_CAND_COUNT, nullptr, FALSE);
    if (m_candCount < 1) m_candCount = 1;
    if (m_candCount > 9) m_candCount = 9;  // 对应原 DDV_MinMaxInt(1,9)
    m_codeMode      = firecfg::CbGetSel(hwnd, IDC_CMB_CODE_MODE);
    m_candDirection = firecfg::CbGetSel(hwnd, IDC_CMB_CAND_DIRECTION);
    m_wubiDing      = firecfg::CbGetSel(hwnd, IDC_CMB_WUBI_DING);
    m_toggleKey     = firecfg::CbGetSel(hwnd, IDC_CMB_TOGGLE_KEY);
    SyncToConfig();
    return true;
}

void CInputSettingsPage::SyncToConfig() {
    g_config.enable_word_input = m_wordInput != FALSE;
    g_config.enable_dynamic_frequency = m_dynamicFreq != FALSE;
    g_config.z_key_query = m_zKeyQuery != FALSE;
    g_config.show_code_in_window = m_showCode != FALSE;
    g_config.candidate_count = m_candCount;
    g_config.code_mode = (fire::CodeMode)m_codeMode;
    g_config.candidates_direction = (fire::CandidatesDirection)m_candDirection;
    g_config.wubi_ding_mode = (fire::WubiDingMode)m_wubiDing;
    g_config.wubi_code_tip = m_wubiCodeTip != FALSE;
    g_config.wubi_auto_commit = m_wubiAutoCommit != FALSE;
    if (m_toggleKey >= 0 && m_toggleKey < 4) {
        g_config.toggle_input_mode_key = kToggleKeys[m_toggleKey];
    }
    // 兼容历史布尔开关
    g_config.wubi35_ding = (m_wubiDing == 1);
}

HPROPSHEETPAGE CreateInputSettingsPage(CInputSettingsPage& page) {
    PROPSHEETPAGEW psp = {0};
    psp.dwSize = sizeof(psp);
    psp.dwFlags = PSP_DEFAULT;
    psp.hInstance = GetModuleHandleW(nullptr);
    psp.pszTemplate = MAKEINTRESOURCEW(IDD_PAGE_INPUT);
    psp.pfnDlgProc = PageDlgProc;
    psp.lParam = (LPARAM)&page;
    return CreatePropertySheetPageW(&psp);
}
