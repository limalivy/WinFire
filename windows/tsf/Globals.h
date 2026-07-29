//
//  Globals.h — TSF TIP 全局 GUID / CLSID 声明
//
#pragma once

#include <windows.h>
#include <msctf.h>

// ATL Module 初始化/终止化（实现在 Globals.cpp）
// DllMain 调用这两个函数，避免在头文件暴露 ATL Module 完整定义。
HRESULT FireAtlModuleInit(HINSTANCE hInst);
void FireAtlModuleTerm();

// ---------------------------------------------------------------------------
// 版本号：CLSID / Profile GUID 按版本派生（见下），使每个发行版拥有唯一的
// TSF 注册标识，从而支持「侧载升级」——新版用新 CLSID/Profile 注册，旧版反注册，
// 旧 DLL 文件由仍在运行的宿主进程持有直至释放（配合安装器的版本化文件名 +
// 旧文件延迟删除），避免 in-process DLL 被占用导致无法覆盖/更新。
//
// 版本号「单一来源」：仓库根目录的 VERSION 文件（形如 0.1.0）。
// FIRE_VER_MAJOR/MINOR/PATCH 与 FIRE_VER_STRING 由构建期从 VERSION 生成的
// Version.h 提供（见 fire_tsf.vcxproj 的 GenerateVersionHeader 目标）。
// 发版/测试更新版本时只改 VERSION 一处，切勿在此手写版本号（详见 AGENTS.md）。
// ---------------------------------------------------------------------------
#include "Version.h"

// 微火输入法 Text Service 的 CLSID（正式发布前请把下方 Globals.cpp 中的基 GUID
// 用 guidgen 重新生成为你自己的唯一值；最后 3 字节由版本号派生，勿手动占用）。
// 基 GUID {8E9F0B21-3C4D-4E5A-9B7C-1F2A3B4C????}
extern const CLSID CLSID_FireTextService;

// 语言栏/Profile GUID（同样按版本派生，随 CLSID 一起侧载）
// 基 GUID {A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B????}
extern const GUID GUID_FireProfile;

// 组字显示属性 GUID（用于 marked text 高亮）
extern const GUID GUID_FireDisplayAttributeInput;

// 语言栏按钮 GUID（中/英状态栏按钮）
// {C3D4E5F6-A7B8-4C9D-0E1F-2A3B4C5D6E7F}
extern const GUID GUID_FireLangBarButton;

// 语言（简体中文）
#define FIRE_LANGID MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)

extern HINSTANCE g_hInst;      // DLL 实例句柄
extern LONG g_cRefDll;         // DLL 引用计数（DllCanUnloadNow 用）

inline void DllAddRef() { InterlockedIncrement(&g_cRefDll); }
inline void DllRelease() { InterlockedDecrement(&g_cRefDll); }

// TSF TIP 描述、注册用字符串
#define FIRE_TEXTSERVICE_DESC L"微火五笔输入法"
#define FIRE_PROFILE_DESC     L"微火五笔"
