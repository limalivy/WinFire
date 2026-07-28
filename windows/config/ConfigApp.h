//
//  ConfigApp.h — MFC 配置界面应用（业火五笔 Windows 设置）
//
#pragma once

#include <afxwin.h>
#include <afxdlgs.h>   // CPropertySheet / CPropertyPage
#include "fire/config.h"

// 全局配置对象（各页共享，OK 时统一保存）
extern fire::Config g_config;

class CFireConfigApp : public CWinApp {
public:
    CFireConfigApp() = default;
    BOOL InitInstance() override;
};
