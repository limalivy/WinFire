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

// 业火输入法 Text Service 的 CLSID（部署前请用 guidgen 重新生成，避免与他人冲突）
// {8E9F0B21-3C4D-4E5A-9B7C-1F2A3B4C5D6E}
extern const CLSID CLSID_FireTextService;

// 语言栏/Profile GUID
// {A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}
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
#define FIRE_TEXTSERVICE_DESC L"业火五笔输入法"
#define FIRE_PROFILE_DESC     L"业火五笔"
