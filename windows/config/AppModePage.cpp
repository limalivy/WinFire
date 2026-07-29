//
//  AppModePage.cpp — 按应用输入模式页（纯 Win32）
//
#include "ConfigApp.h"
#include "AppModePage.h"

namespace {
const wchar_t* kModeNames[] = { L"中文", L"英文", L"最近使用" };
}  // namespace

CAppModePage::CAppModePage() {
    m_keepAppMode = g_config.keep_app_input_mode ? TRUE : FALSE;
    m_tipShowTime = (int)g_config.app_input_mode_tip_show_time;
}

void CAppModePage::OnInitDialog() {
    firecfg::SetCheck(hwnd, IDC_CHK_KEEP_APP_MODE, m_keepAppMode);

    // 模式提示时机下拉框
    firecfg::CbReset(hwnd, IDC_CMB_TIP_SHOW_TIME);
    firecfg::CbAdd(hwnd, IDC_CMB_TIP_SHOW_TIME, L"仅在变化时显示");
    firecfg::CbAdd(hwnd, IDC_CMB_TIP_SHOW_TIME, L"总是显示");
    firecfg::CbAdd(hwnd, IDC_CMB_TIP_SHOW_TIME, L"不显示");
    firecfg::CbSetSel(hwnd, IDC_CMB_TIP_SHOW_TIME, m_tipShowTime);

    // 新增行的模式选择框
    firecfg::CbReset(hwnd, IDC_CMB_APP_MODE);
    for (const auto* n : kModeNames) firecfg::CbAdd(hwnd, IDC_CMB_APP_MODE, n);
    firecfg::CbSetSel(hwnd, IDC_CMB_APP_MODE, 0);

    // 列表：应用ID + 模式
    firecfg::LvInitColumns(hwnd, IDC_LIST_APPS);
    firecfg::LvAddColumn(hwnd, IDC_LIST_APPS, 0, L"应用（进程名）", 170);
    firecfg::LvAddColumn(hwnd, IDC_LIST_APPS, 1, L"输入模式", 80);

    ReloadList();
}

void CAppModePage::ReloadList() {
    firecfg::LvClear(hwnd, IDC_LIST_APPS);
    int row = 0;
    for (const auto& kv : g_config.app_settings) {
        std::wstring app = firecfg::Utf8ToWide(kv.first);
        int mode = (int)kv.second;
        if (mode < 0 || mode > 2) mode = 0;
        firecfg::LvInsertItem(hwnd, IDC_LIST_APPS, row, app.c_str());
        firecfg::LvSetItem(hwnd, IDC_LIST_APPS, row, 1, kModeNames[mode]);
        ++row;
    }
}

void CAppModePage::OnCommand(WPARAM wParam, LPARAM /*lParam*/) {
    int code = HIWORD(wParam);
    int id = LOWORD(wParam);
    if (id == IDC_BTN_APP_ADD && code == BN_CLICKED) {
        std::wstring appId = firecfg::GetText(hwnd, IDC_EDIT_APP_ID);
        // trim
        while (!appId.empty() && (appId.front() == L' ' || appId.front() == L'\t')) appId.erase(0, 1);
        while (!appId.empty() && (appId.back() == L' ' || appId.back() == L'\t')) appId.pop_back();
        if (appId.empty()) {
            MsgBox(L"请先填写应用进程名，例如 notepad.exe", L"提示", MB_OK | MB_ICONINFORMATION);
            return;
        }
        int mode = firecfg::CbGetSel(hwnd, IDC_CMB_APP_MODE);
        if (mode < 0) mode = 0;
        g_config.app_settings[firecfg::WideToUtf8(appId)] = (fire::InputModeSetting)mode;
        firecfg::SetText(hwnd, IDC_EDIT_APP_ID, L"");
        ReloadList();
    } else if (id == IDC_BTN_APP_REMOVE && code == BN_CLICKED) {
        int sel = firecfg::LvGetSelItem(hwnd, IDC_LIST_APPS);
        if (sel < 0) return;
        std::wstring app = firecfg::LvGetItemText(hwnd, IDC_LIST_APPS, sel, 0);
        g_config.app_settings.erase(firecfg::WideToUtf8(app));
        ReloadList();
    }
}

void CAppModePage::SyncToConfig() {
    g_config.keep_app_input_mode = m_keepAppMode != FALSE;
    g_config.app_input_mode_tip_show_time = (fire::AppInputModeTipShowTime)m_tipShowTime;
    // app_settings 已在增删时直接写入 g_config
}

bool CAppModePage::OnApply() {
    m_keepAppMode = firecfg::GetCheck(hwnd, IDC_CHK_KEEP_APP_MODE);
    m_tipShowTime = firecfg::CbGetSel(hwnd, IDC_CMB_TIP_SHOW_TIME);
    if (m_tipShowTime < 0) m_tipShowTime = 0;
    SyncToConfig();
    return true;
}

HPROPSHEETPAGE CreateAppModePage(CAppModePage& page) {
    PROPSHEETPAGEW psp = {0};
    psp.dwSize = sizeof(psp);
    psp.dwFlags = PSP_DEFAULT;
    psp.hInstance = GetModuleHandleW(nullptr);
    psp.pszTemplate = MAKEINTRESOURCEW(IDD_PAGE_APPMODE);
    psp.pfnDlgProc = PageDlgProc;
    psp.lParam = (LPARAM)&page;
    return CreatePropertySheetPageW(&psp);
}
