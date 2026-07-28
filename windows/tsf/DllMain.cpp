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
        GUID_TFCAT_TIPCAP_UIELEMENTENABLED,   // 自绘 UI（候选窗）
        GUID_TFCAT_TIPCAP_SECUREMODE,
        GUID_TFCAT_TIPCAP_COMLESS,
    };
    for (const GUID& cat : categories) {
        catMgr->RegisterCategory(CLSID_FireTextService, cat, CLSID_FireTextService);
    }
    return S_OK;
}

STDAPI DllUnregisterServer() {
    CComPtr<ITfInputProcessorProfiles> profiles;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfInputProcessorProfiles, (void**)&profiles))) {
        profiles->Unregister(CLSID_FireTextService);
    }
    CComPtr<ITfCategoryMgr> catMgr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfCategoryMgr, (void**)&catMgr))) {
        const GUID categories[] = {
            GUID_TFCAT_TIP_KEYBOARD,
            GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
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
