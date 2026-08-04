//
//  DllMain.cpp — DLL 入口、类厂、TSF 注册（Category + Profile）
//
#include <windows.h>
#include <msctf.h>
#include <ctffunc.h>
#include <atlbase.h>
#include <atlcom.h>
#include <string>

#include "Globals.h"
#include "DebugLog.h"
#include "TextService.h"

using namespace firewin;

// ---- ATL 类厂：把 CFireTextService 暴露为可 CoCreate 的 COM 对象 ----
class CFireClassFactory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
        FIRE_LOG(L"[WinFire] ClassFactory::CreateInstance [tid=%lu]\n", GetCurrentThreadId());
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        CComObject<CFireTextService>* p = nullptr;
        HRESULT hr = CComObject<CFireTextService>::CreateInstance(&p);
        if (FAILED(hr)) {
            FIRE_LOG(L"[WinFire] ClassFactory: CreateInstance FAILED hr=0x%08lX\n", (unsigned long)hr);
            return hr;
        }
        p->AddRef();
        hr = p->QueryInterface(riid, ppv);
        FIRE_LOG(L"[WinFire] ClassFactory: QI hr=0x%08lX ppv=%p\n", (unsigned long)hr, ppv ? *ppv : nullptr);
        p->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) DllAddRef(); else DllRelease();
        return S_OK;
    }
private:
    LONG ref_ = 1;
};

// ---- DLL 导出 ----
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    FIRE_LOG(L"[WinFire] DllGetClassObject [tid=%lu]\n", GetCurrentThreadId());
    if (rclsid == CLSID_FireTextService) {
        CFireClassFactory* f = new CFireClassFactory();
        HRESULT hr = f->QueryInterface(riid, ppv);
        f->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() {
    return (g_cRefDll == 0) ? S_OK : S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_hInst = hInst;
            DisableThreadLibraryCalls(hInst);
            FIRE_LOG(L"[WinFire] DllMain: DLL_PROCESS_ATTACH hInst=%p [tid=%lu]\n",
                     (void*)hInst, GetCurrentThreadId());
            // ATL Module 初始化（初始化 _pAtlModule，供 CComObject<>::CreateInstance 使用）
            if (FAILED(FireAtlModuleInit(hInst))) {
                FIRE_LOG(L"[WinFire] DllMain: FireAtlModuleInit FAILED\n");
                return FALSE;
            }
            FIRE_LOG(L"[WinFire] DllMain: ATL Module Init OK\n");
            break;
        case DLL_PROCESS_DETACH:
            FIRE_LOG(L"[WinFire] DllMain: DLL_PROCESS_DETACH [tid=%lu]\n", GetCurrentThreadId());
            // ATL Module 终止化
            FireAtlModuleTerm();
            g_hInst = nullptr;
            break;
        default:
            break;
    }
    return TRUE;
}

// ---- 注册辅助 ----
static void GuidToString(REFGUID guid, wchar_t* out /*[40]*/) {
    StringFromGUID2(guid, out, 40);
}

// ---- 用户级输入法安装（使输入法出现在「替代默认输入法」下拉）----
// 通过 input.dll!InstallLayoutOrTip 把 TIP 写入当前用户输入法列表。
// 仅系统级注册（HKLM 的 CTF\TIP）不够，下拉枚举的是用户级列表。
#ifndef ILOT_UNINSTALL
#define ILOT_UNINSTALL 0x00000001
#endif
typedef HRESULT(WINAPI* PTF_INSTALLLAYOUTORTIP)(LPCWSTR psz, DWORD dwFlags);

// 构造 InstallLayoutOrTip 所需的字符串 "<langid十六进制小写>:<CLSID{带大括号}><Profile{带大括号}>"。
// CLSID/Profile 经 GuidToString(StringFromGUID2) 输出已自带大括号，与 weasel 字面量格式一致。
static void InstallLayoutOrTipForUser(REFCLSID clsid, REFGUID profile, DWORD dwFlags) {
    wchar_t clsidStr[40] = {0};
    wchar_t profileStr[40] = {0};
    wchar_t langidStr[8] = {0};
    GuidToString(clsid, clsidStr);
    GuidToString(profile, profileStr);
    wsprintfW(langidStr, L"%04x", FIRE_LANGID);
    std::wstring psz = std::wstring(langidStr) + L":" + clsidStr + profileStr;

    HMODULE hInput = LoadLibraryW(L"input.dll");
    if (!hInput) return;  // 老系统/Server 无此 DLL，静默失败不阻断系统级注册
    auto pfn = (PTF_INSTALLLAYOUTORTIP)GetProcAddress(hInput, "InstallLayoutOrTip");
    if (pfn) {
        (*pfn)(psz.c_str(), dwFlags);
    }
    FreeLibrary(hInput);
}

// 检查 CLSID 是否匹配 WinFire 基 GUID 模式。
// CLSID 最后 3 字节由版本号派生，前 13 字节为哨兵值，用于识别所有版本的 WinFire。
static bool IsWinFireBaseClsid(REFCLSID clsid) {
    return clsid.Data1 == 0x8e9f0b21 &&
           clsid.Data2 == 0x3c4d &&
           clsid.Data3 == 0x4e5a &&
           clsid.Data4[0] == 0x9b &&
           clsid.Data4[1] == 0x7c &&
           clsid.Data4[2] == 0x1f &&
           clsid.Data4[3] == 0x2a &&
           clsid.Data4[4] == 0x3b;
}

static bool IsWinFireBaseProfileGuid(REFGUID guid) {
    return guid.Data1 == 0xa1b2c3d4 &&
           guid.Data2 == 0xe5f6 &&
           guid.Data3 == 0x4a7b &&
           guid.Data4[0] == 0x8c &&
           guid.Data4[1] == 0x9d &&
           guid.Data4[2] == 0x0e &&
           guid.Data4[3] == 0x1f &&
           guid.Data4[4] == 0x2a;
}

static bool IsEqualGuid(REFGUID a, REFGUID b) {
    return memcmp(&a, &b, sizeof(GUID)) == 0;
}

// 删除指定 CLSID 的 COM 注册表树（HKCR\CLSID\{...}）
static void UnregisterServerKeyFor(REFCLSID clsid) {
    wchar_t s[40];
    GuidToString(clsid, s);
    std::wstring key = std::wstring(L"CLSID\\") + s;
    RegDeleteTreeW(HKEY_CLASSES_ROOT, key.c_str());
}

// 枚举 zh-CN 下所有 TSF Profile，清理匹配 WinFire 基 GUID 但不是当前版本的残留注册。
// 解决旧版 DLL 文件已被删除导致 install.ps1/winfire.iss 的基于文件名的清理失效的问题。
static void CleanupStaleRegistrations() {
    CComPtr<ITfInputProcessorProfiles> profiles;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_ITfInputProcessorProfiles, (void**)&profiles);
    if (FAILED(hr)) return;

    CComPtr<IEnumTfLanguageProfiles> pEnum;
    hr = profiles->EnumLanguageProfiles(FIRE_LANGID, &pEnum);
    if (FAILED(hr)) return;

    TF_LANGUAGEPROFILE lp;
    while (pEnum->Next(1, &lp, nullptr) == S_OK) {
        // 只处理匹配 WinFire 基 GUID 模式的 profile
        if (!IsWinFireBaseClsid(lp.clsid) && !IsWinFireBaseProfileGuid(lp.guidProfile))
            continue;

        // 跳过当前版本（正在注册的这个）
        if (IsEqualGuid(lp.clsid, CLSID_FireTextService) &&
            IsEqualGuid(lp.guidProfile, GUID_FireProfile))
            continue;

        // 旧版本：移除语言 profile + 反注册 text service + 清 COM 注册表
        profiles->RemoveLanguageProfile(lp.clsid, FIRE_LANGID, lp.guidProfile);
        // 对称清除旧版本在用户级输入法列表中的残留
        InstallLayoutOrTipForUser(lp.clsid, lp.guidProfile, ILOT_UNINSTALL);
        if (!IsEqualGuid(lp.clsid, CLSID_FireTextService)) {
            profiles->Unregister(lp.clsid);
            UnregisterServerKeyFor(lp.clsid);
        }
    }
}

static bool RegisterServerKey() {
    // HKCR\CLSID\{CLSID}\InprocServer32 = 本 DLL 路径
    wchar_t clsidStr[40];
    GuidToString(CLSID_FireTextService, clsidStr);
    std::wstring key = std::wstring(L"CLSID\\") + clsidStr + L"\\InprocServer32";

    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(g_hInst, path, MAX_PATH);

    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr,
                        &hKey, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)path,
                   (DWORD)((wcslen(path) + 1) * sizeof(wchar_t)));
    const wchar_t* threading = L"Apartment";
    RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (const BYTE*)threading,
                   (DWORD)((wcslen(threading) + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    // 描述
    std::wstring descKey = std::wstring(L"CLSID\\") + clsidStr;
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, descKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr,
                        &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)FIRE_TEXTSERVICE_DESC,
                       (DWORD)((wcslen(FIRE_TEXTSERVICE_DESC) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
    return true;
}

static void UnregisterServerKey() {
    wchar_t clsidStr[40];
    GuidToString(CLSID_FireTextService, clsidStr);
    std::wstring key = std::wstring(L"CLSID\\") + clsidStr;
    RegDeleteTreeW(HKEY_CLASSES_ROOT, key.c_str());
}

STDAPI DllRegisterServer() {
    // 先清理安装目录中可能已不存在的旧版本残留 TSF 注册（防止出现两个输入法）
    CleanupStaleRegistrations();

    if (!RegisterServerKey()) return E_FAIL;

    // 注册 TIP + 语言 Profile
    CComPtr<ITfInputProcessorProfiles> profiles;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITfInputProcessorProfiles, (void**)&profiles);
    if (FAILED(hr)) return hr;

    hr = profiles->Register(CLSID_FireTextService);
    if (FAILED(hr)) return hr;

    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(g_hInst, path, MAX_PATH);

    hr = profiles->AddLanguageProfile(CLSID_FireTextService, FIRE_LANGID, GUID_FireProfile,
                                      FIRE_PROFILE_DESC, (ULONG)wcslen(FIRE_PROFILE_DESC),
                                      path, (ULONG)wcslen(path), 0);
    if (FAILED(hr)) return hr;

    // 注册为键盘类别的 TIP
    CComPtr<ITfCategoryMgr> catMgr;
    hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ITfCategoryMgr, (void**)&catMgr);
    if (FAILED(hr)) return hr;

    const GUID categories[] = {
        GUID_TFCAT_TIP_KEYBOARD,
        GUID_TFCAT_TIPCAP_UIELEMENTENABLED,       // 自绘 UI（候选窗）
        GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,       // 沉浸式/UWP 应用支持
        GUID_TFCAT_TIPCAP_SECUREMODE,
        GUID_TFCAT_TIPCAP_COMLESS,
    };
    for (const GUID& cat : categories) {
        catMgr->RegisterCategory(CLSID_FireTextService, cat, CLSID_FireTextService);
    }

    // 用户级安装：写入当前用户输入法列表，使本输入法出现在
    // 「设置 → 替代默认输入法」下拉（系统级注册不足以让下拉枚举到）。
    profiles->EnableLanguageProfile(CLSID_FireTextService, FIRE_LANGID, GUID_FireProfile, TRUE);
    profiles->EnableLanguageProfileByDefault(CLSID_FireTextService, FIRE_LANGID, GUID_FireProfile, TRUE);
    InstallLayoutOrTipForUser(CLSID_FireTextService, GUID_FireProfile, 0);
    return S_OK;
}

STDAPI DllUnregisterServer() {
    CComPtr<ITfInputProcessorProfiles> profiles;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfInputProcessorProfiles, (void**)&profiles))) {
        // 用户级卸载：从当前用户输入法列表移除（与注册对称）
        profiles->RemoveLanguageProfile(CLSID_FireTextService, FIRE_LANGID, GUID_FireProfile);
        InstallLayoutOrTipForUser(CLSID_FireTextService, GUID_FireProfile, ILOT_UNINSTALL);
        profiles->Unregister(CLSID_FireTextService);
    }
    CComPtr<ITfCategoryMgr> catMgr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfCategoryMgr, (void**)&catMgr))) {
        const GUID categories[] = {
            GUID_TFCAT_TIP_KEYBOARD,
            GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
            GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
            GUID_TFCAT_TIPCAP_SECUREMODE,
            GUID_TFCAT_TIPCAP_COMLESS,
        };
        for (const GUID& cat : categories) {
            catMgr->UnregisterCategory(CLSID_FireTextService, cat, CLSID_FireTextService);
        }
    }
    UnregisterServerKey();
    return S_OK;
}
