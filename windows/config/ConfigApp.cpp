//
//  ConfigApp.cpp — 纯 Win32 应用入口 + 属性表 + 通用对话框过程
//
#include "ConfigApp.h"
#include "UiBase.h"
#include "InputSettingsPage.h"
#include "PunctuationPage.h"
#include "AppModePage.h"
#include "StatisticsPage.h"
#include "DictPage.h"
#include "ConfigStore.h"

#include <commctrl.h>
#include <prsht.h>

fire::Config g_config;

// ---- 通用属性页对话框过程 ----
// 通过 PROPSHEETPAGE.lParam 携带 PageBase*，在 WM_INITDIALOG 时存到 DWLP_USER，
// 后续消息从 DWLP_USER 取回并转发给派生类的 OnXxx 钩子。
INT_PTR CALLBACK PageDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    PageBase* page = nullptr;
    if (msg == WM_INITDIALOG) {
        auto* psp = reinterpret_cast<LPPROPSHEETPAGEW>(lParam);
        page = reinterpret_cast<PageBase*>(psp->lParam);
        SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)page);
        page->hwnd = hDlg;
        page->OnInitDialog();
        return TRUE;
    }
    page = reinterpret_cast<PageBase*>(GetWindowLongPtrW(hDlg, DWLP_USER));
    if (!page) return FALSE;

    switch (msg) {
        case WM_COMMAND:
            page->OnCommand(wParam, lParam);
            return FALSE;
        case WM_NOTIFY: {
            LPNMHDR nm = reinterpret_cast<LPNMHDR>(lParam);
            // PSN_APPLY：用户点 OK 或 Apply，统一回调 OnApply。
            // OK 时 lParam->lParam==0，Apply 时非零；PSH_NOAPPLYNOW 下只有 OK 触发。
            if (nm->code == PSN_APPLY) {
                bool ok = page->OnApply();
                SetWindowLongPtrW(hDlg, DWLP_MSGRESULT,
                                  ok ? PSNRET_NOERROR : PSNRET_INVALID);
                return TRUE;
            }
            LRESULT result = 0;
            page->OnNotify(nm, &result);
            SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, result);
            return TRUE;
        }
        default:
            return FALSE;
    }
}

// ---- 应用入口 ----
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // 启用 Common Controls（PropertySheet / ListView / ComboBox 需要）
    INITCOMMONCONTROLSEX icc = {0};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    // 载入磁盘配置（不存在则用默认值）
    firecfg::ConfigStore::Load(g_config);

    // 各页对象：必须在 PropertySheet 返回前保持存活（模态调用期间）
    CInputSettingsPage pageInput;
    CPunctuationPage pagePunct;
    CAppModePage pageAppMode;
    CStatisticsPage pageStats;
    CDictPage pageDict;

    HPROPSHEETPAGE pages[5] = {0};
    pages[0] = CreateInputSettingsPage(pageInput);
    pages[1] = CreatePunctuationPage(pagePunct);
    pages[2] = CreateAppModePage(pageAppMode);
    pages[3] = CreateStatisticsPage(pageStats);
    pages[4] = CreateDictPage(pageDict);

    PROPSHEETHEADERW psh = {0};
    psh.dwSize = sizeof(psh);
    // 用 phpage 数组（已 CreatePropertySheetPage 创建的句柄），不能带 PSH_PROPSHEETPAGE
    // （该标志表示用 ppsp 结构数组，会让 PropertySheet 把 phpage 当指针解引用导致访问违例）
    psh.dwFlags = PSH_NOAPPLYNOW;  // 隐藏"应用"按钮（用 OK 统一保存）
    psh.hInstance = hInstance;
    psh.hwndParent = nullptr;
    psh.pszCaption = L"微火五笔输入法 设置";
    psh.nPages = 5;
    psh.nStartPage = 0;
    psh.phpage = pages;

    INT_PTR result = PropertySheetW(&psh);
    // PropertySheet 返回 IDOK(1) / IDCANCEL(2)。OK 时统一保存。
    if (result > 0 && result != IDCANCEL) {
        firecfg::ConfigStore::Save(g_config);
    }
    return 0;
}
