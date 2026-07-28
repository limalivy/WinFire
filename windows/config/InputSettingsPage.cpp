//
//  InputSettingsPage.cpp
//
#include "ConfigApp.h"
#include "InputSettingsPage.h"

IMPLEMENT_DYNCREATE(CInputSettingsPage, CPropertyPage)

BEGIN_MESSAGE_MAP(CInputSettingsPage, CPropertyPage)
END_MESSAGE_MAP()

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

CInputSettingsPage::CInputSettingsPage() : CPropertyPage(IDD_PAGE_INPUT) {
    // 从全局配置初始化控件变量
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

void CInputSettingsPage::DoDataExchange(CDataExchange* pDX) {
    CPropertyPage::DoDataExchange(pDX);
    DDX_Check(pDX, IDC_CHK_WORD_INPUT, m_wordInput);
    DDX_Check(pDX, IDC_CHK_DYNAMIC_FREQ, m_dynamicFreq);
    DDX_Check(pDX, IDC_CHK_Z_KEY_QUERY, m_zKeyQuery);
    DDX_Check(pDX, IDC_CHK_SHOW_CODE, m_showCode);
    DDX_Check(pDX, IDC_CHK_WUBI_CODE_TIP, m_wubiCodeTip);
    DDX_Check(pDX, IDC_CHK_WUBI_AUTO_COMMIT, m_wubiAutoCommit);
    DDX_Text(pDX, IDC_EDIT_CAND_COUNT, m_candCount);
    DDV_MinMaxInt(pDX, m_candCount, 1, 9);
    DDX_CBIndex(pDX, IDC_CMB_CODE_MODE, m_codeMode);
    DDX_CBIndex(pDX, IDC_CMB_CAND_DIRECTION, m_candDirection);
    DDX_CBIndex(pDX, IDC_CMB_WUBI_DING, m_wubiDing);
    DDX_CBIndex(pDX, IDC_CMB_TOGGLE_KEY, m_toggleKey);
}

BOOL CInputSettingsPage::OnInitDialog() {
    CPropertyPage::OnInitDialog();
    // 填充下拉框
    if (auto* cb = (CComboBox*)GetDlgItem(IDC_CMB_CODE_MODE)) {
        cb->ResetContent();
        cb->AddString(_T("纯五笔"));
        cb->AddString(_T("纯拼音"));
        cb->AddString(_T("五笔拼音混输"));
        cb->SetCurSel(m_codeMode);
    }
    if (auto* cb = (CComboBox*)GetDlgItem(IDC_CMB_CAND_DIRECTION)) {
        cb->ResetContent();
        cb->AddString(_T("竖排"));
        cb->AddString(_T("横排"));
        cb->AddString(_T("不显示"));
        cb->SetCurSel(m_candDirection);
    }
    if (auto* cb = (CComboBox*)GetDlgItem(IDC_CMB_WUBI_DING)) {
        cb->ResetContent();
        cb->AddString(_T("关闭"));
        cb->AddString(_T("35 顶"));
        cb->AddString(_T("52 顶"));
        cb->AddString(_T("53 顶"));
        cb->SetCurSel(m_wubiDing);
    }
    if (auto* cb = (CComboBox*)GetDlgItem(IDC_CMB_TOGGLE_KEY)) {
        cb->ResetContent();
        cb->AddString(_T("Shift"));
        cb->AddString(_T("Ctrl"));
        cb->AddString(_T("Win"));
        cb->AddString(_T("Alt"));
        cb->SetCurSel(m_toggleKey);
    }
    return TRUE;
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

BOOL CInputSettingsPage::OnApply() {
    if (!UpdateData(TRUE)) return FALSE;
    SyncToConfig();
    return CPropertyPage::OnApply();
}
