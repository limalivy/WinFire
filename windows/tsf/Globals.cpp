//
//  Globals.cpp — GUID 定义与全局变量
//
#include "Globals.h"
#include <atlbase.h>
#include <atlcom.h>

// 注意：这些为示例「基 GUID」，正式发布前务必用 guidgen/uuidgen 生成唯一值。
// CLSID / Profile GUID 的最后 3 字节由版本号 (major, minor, patch) 派生，
// 使每个发行版拥有独立的 TSF 注册标识，从而支持侧载升级（新版侧载注册、旧版
// 反注册，旧 DLL 由仍在运行的宿主进程持有直至自然释放）。
// 约束：major/minor/patch 各自 <= 255（单字节），否则派生会截断产生冲突。
static_assert(FIRE_VER_MAJOR <= 255 && FIRE_VER_MINOR <= 255 && FIRE_VER_PATCH <= 255,
              "version component out of byte range for GUID derivation");

const CLSID CLSID_FireTextService =
    { 0x8e9f0b21, 0x3c4d, 0x4e5a,
      { 0x9b, 0x7c, 0x1f, 0x2a, 0x3b, FIRE_VER_MAJOR, FIRE_VER_MINOR, FIRE_VER_PATCH } };

const GUID GUID_FireProfile =
    { 0xa1b2c3d4, 0xe5f6, 0x4a7b,
      { 0x8c, 0x9d, 0x0e, 0x1f, 0x2a, FIRE_VER_MAJOR, FIRE_VER_MINOR, FIRE_VER_PATCH } };

const GUID GUID_FireDisplayAttributeInput =
    { 0xb2c3d4e5, 0xf6a7, 0x4b8c, { 0x9d, 0x0e, 0x1f, 0x2a, 0x3b, 0x4c, 0x5d, 0x6f } };

const GUID GUID_FireLangBarButton =
    { 0xc3d4e5f6, 0xa7b8, 0x4c9d, { 0x0e, 0x1f, 0x2a, 0x3b, 0x4c, 0x5d, 0x6e, 0x7f } };

HINSTANCE g_hInst = nullptr;
LONG g_cRefDll = 0;

// 静态 ATL 必需：必须有一个全局 CAtlDllModuleT 实例来初始化 _pAtlModule，
// 否则 CComObject<>::CreateInstance 会因 _pAtlModule 为 nullptr 而崩溃。
// 全局对象构造时自动初始化 _pAtlModule，无需在 DllMain 里调用 Init/Term。
class CFireAtlModule : public CAtlDllModuleT<CFireAtlModule> {};
CFireAtlModule g_AtlModule;

// DllMain 调用的 ATL Module 初始化/终止化包装（空实现，保留接口以便未来扩展）
HRESULT FireAtlModuleInit(HINSTANCE /*hInst*/) { return S_OK; }
void FireAtlModuleTerm() {}
