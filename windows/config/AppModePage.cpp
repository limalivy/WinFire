//
//  AppModePage.cpp — 按应用输入模式页
//
#include "ConfigApp.h"
#include "AppModePage.h"

IMPLEMENT_DYNCREATE(CAppModePage, CPropertyPage)

BEGIN_MESSAGE_MAP(CAppModePage, CPropertyPage)
    ON_BN_CLICKED(IDC_BTN_APP_ADD, &CAppModePage::OnAdd)
    ON_BN_CLICKED(IDC_BTN_APP_REMOVE, &CAppModePage::OnRemove)
END_MESSAGE_MAP()

namespace {
const TCHAR* kModeNames[] = { _T("中文"), _T("英文"), _T("最近使用") };
}  // namespace

CAppModePage::CAppModePage() : CPropertyPage(IDD_PAGE_APPMODE) {
    m_keepAppMode = g_config.keep_app_input_mode ? TRUE : FALSE;
    m_tipShowTime = (int)g_config.app_input_mode_tip_show_time;
}

void CAppModePage::DoDataExchange(CDataExchange* pDX) {
    CPropertyPage::DoDataExchange(pDX);
    DDX_Check(pDX, IDC_CHK_KEEP_APP_MODE, m_keepAppMode);
    DDX_CBIndex(pDX, IDC_CMB_TIP_SHOW_TIME, m_tipShowTime);
}

BOOL CAppModePage::OnInitDialog() {
    CPropertyPage::OnInitDialog();

    if (auto* cb = (CComboBox*)GetDlgItem(IDC_CMB_TIP_SHOW_TIME)) {
        cb->ResetContent();
        cb->AddString(_T("仅在变化时显示"));
        cb->AddString(_T("总是显示"));
        cb->AddString(_T("不显示"));
        cb->SetCurSel(m_tipShowTime);
    }
    // 新增行的模式选择框
    if (auto* cb = (CComboBox*)GetDlgItem(IDC_CMB_APP_MODE)) {
        cb->ResetContent();
        for (const auto* n : kModeNames) cb->AddString(n);
        cb->SetCurSel(0);
    }
    // 列表：应用ID + 模式
    if (auto* lc = (CListCtrl*)GetDlgItem(IDC_LIST_APPS)) {
        lc->SetExtendedStyle(lc->GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        lc->InsertColumn(0, _T("应用（进程名）"), LVCFMT_LEFT, 170);
        lc->InsertColumn(1, _T("输入模式"), LVCFMT_LEFT, 80);
    }
    ReloadList();
    return TRUE;
}

void CAppModePage::ReloadList() {
    auto* lc = (CListCtrl*)GetDlgItem(IDC_LIST_APPS);
    if (!lc) return;
    lc->DeleteAllItems();
    int row = 0;
    for (const auto& kv : g_config.app_settings) {
        CString app(kv.first.c_str());
        lc->InsertItem(row, app);
        int mode = (int)kv.second;
        if (mode < 0 || mode > 2) mode = 0;
        lc->SetItemText(row, 1, kModeNames[mode]);
        ++row;
    }
}

void CAppModePage::OnAdd() {
    CString appId;
    GetDlgItemText(IDC_EDIT_APP_ID, appId);
    appId.Trim();
    if (appId.IsEmpty()) {
        MessageBox(_T("请先填写应用进程名，例如 notepad.exe"), _T("提示"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    int mode = 0;
    if (auto* cb = (CComboBox*)GetDlgItem(IDC_CMB_APP_MODE)) mode = cb->GetCurSel();
    if (mode < 0) mode = 0;

    std::string key = CT2A(appId, CP_UTF8).m_psz;
    g_config.app_settings[key] = (fire::InputModeSetting)mode;
    SetDlgItemText(IDC_EDIT_APP_ID, _T(""));
    ReloadList();
}

void CAppModePage::OnRemove() {
    auto* lc = (CListCtrl*)GetDlgItem(IDC_LIST_APPS);
    if (!lc) return;
    POSITION pos = lc->GetFirstSelectedItemPosition();
    if (!pos) return;
    int sel = lc->GetNextSelectedItem(pos);
    CString app = lc->GetItemText(sel, 0);
    std::string key = CT2A(app, CP_UTF8).m_psz;
    g_config.app_settings.erase(key);
    ReloadList();
}

void CAppModePage::SyncToConfig() {
    g_config.keep_app_input_mode = m_keepAppMode != FALSE;
    g_config.app_input_mode_tip_show_time = (fire::AppInputModeTipShowTime)m_tipShowTime;
    // app_settings 已在增删时直接写入 g_config
}

BOOL CAppModePage::OnApply() {
    if (!UpdateData(TRUE)) return FALSE;
    SyncToConfig();
    return CPropertyPage::OnApply();
}
