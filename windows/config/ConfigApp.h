//
//  ConfigApp.h — 纯 Win32 配置界面入口（微火五笔 Windows 设置）
//
#pragma once

#include "fire/config.h"

// 全局配置对象（各页共享，OK 时统一保存）
extern fire::Config g_config;
