//
//  ConfigApp.cpp — MFC 应用入口 + 属性表
//
#include "ConfigApp.h"
#include "InputSettingsPage.h"
#include "PunctuationPage.h"
#include "AppModePage.h"
#include "StatisticsPage.h"
#include "DictPage.h"
#include "ConfigStore.h"

fire::Config g_config;

CFireConfigApp theApp;

BOOL CFireConfigApp::InitInstance() {
    CWinApp::InitInstance();

    // 载入磁盘配置（不存在则用默认值）
    firecfg::ConfigStore::Load(g_config);

    CPropertySheet sheet(_T("业火五笔输入法 设置"));
    CInputSettingsPage pageInput;
    CPunctuationPage pagePunct;
    CAppModePage pageAppMode;
    CStatisticsPage pageStats;
    CDictPage pageDict;

    sheet.AddPage(&pageInput);
    sheet.AddPage(&pagePunct);
    sheet.AddPage(&pageAppMode);
    sheet.AddPage(&pageStats);
    sheet.AddPage(&pageDict);
    sheet.m_psh.dwFlags |= PSH_NOAPPLYNOW;  // 隐藏“应用”按钮（用 OK 统一保存）

    m_pMainWnd = &sheet;
    INT_PTR result = sheet.DoModal();
    if (result == IDOK) {
        firecfg::ConfigStore::Save(g_config);
    }
    return FALSE;  // 对话框结束即退出
}
