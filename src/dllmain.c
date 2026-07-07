#include <initguid.h>
#include "jamotong.h"

LONG g_DllRefCount = 0;
HINSTANCE g_hInst = NULL;
CRITICAL_SECTION g_configLock;   // live config 접근 직렬화 (입력 스레드 ↔ 설정 스레드)

static const WCHAR c_szInfoKeyPrefix[] = L"CLSID\\{C471BCF2-343F-4187-A103-24151C3E20B9}";
static const WCHAR c_szInprocServer32[] = L"InprocServer32";
static const WCHAR c_szModelName[] = L"Apartment";
static const WCHAR c_szDescription[] = L"Jamotong IME Text Service";

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hInst = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        InitializeCriticalSection(&g_configLock);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        // 동적 언로드(FreeLibrary)일 때만 우리가 등록한 윈도 클래스를 해제한다(MSDN: DLL이 등록한
        // 클래스는 언로드 시 자동 해제되지 않음). 안 하면 WndProc이 언로드된 코드를 가리키는 낡은
        // 클래스가 남아, DLL이 다른 주소로 재로드된 뒤 RegisterClassW가 조용히 실패하고
        // CreateWindow가 댕글링 WndProc을 불러 크래시한다. (프로세스 종료 시엔 OS가 정리 — 생략)
        if (lpvReserved == NULL) {
            UnregisterClassW(L"JamotongCandidateUI", hinstDLL);
            UnregisterClassW(L"JamotongSettingsClass", hinstDLL);
            UnregisterClassW(L"JamotongCaptureClass", hinstDLL);
            UnregisterClassW(L"JamotongPreeditOverlay", hinstDLL);
            UnregisterClassW(L"JamotongCodeInput", hinstDLL);
        }
        DeleteCriticalSection(&g_configLock);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow(void) {
    return (g_DllRefCount == 0) ? S_OK : S_FALSE;
}

// ------------------------------------------------------------------
// Class Factory Implementation
// ------------------------------------------------------------------

static HRESULT STDMETHODCALLTYPE CF_QueryInterface(IClassFactory *pThis, REFIID riid, void **ppvObject) {
    if (!ppvObject) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) {
        *ppvObject = pThis;
        pThis->lpVtbl->AddRef(pThis);
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE CF_AddRef(IClassFactory *pThis) {
    JamotongClassFactory *factory = (JamotongClassFactory*)pThis;
    InterlockedIncrement(&g_DllRefCount);
    return InterlockedIncrement(&factory->refCount);
}

static ULONG STDMETHODCALLTYPE CF_Release(IClassFactory *pThis) {
    JamotongClassFactory *factory = (JamotongClassFactory*)pThis;
    ULONG res = InterlockedDecrement(&factory->refCount);
    InterlockedDecrement(&g_DllRefCount);
    if (res == 0) {
        HeapFree(GetProcessHeap(), 0, factory);
    }
    return res;
}

static HRESULT STDMETHODCALLTYPE CF_CreateInstance(IClassFactory *pThis, IUnknown *pUnkOuter, REFIID riid, void **ppvObject) {
    (void)pThis;
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    
    return JamotongTextService_Create(pUnkOuter, riid, ppvObject);
}

static HRESULT STDMETHODCALLTYPE CF_LockServer(IClassFactory *pThis, BOOL fLock) {
    (void)pThis;
    if (fLock) {
        InterlockedIncrement(&g_DllRefCount);
    } else {
        InterlockedDecrement(&g_DllRefCount);
    }
    return S_OK;
}

static IClassFactoryVtbl ClassFactoryVtbl = {
    CF_QueryInterface,
    CF_AddRef,
    CF_Release,
    CF_CreateInstance,
    CF_LockServer
};

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv) {
    if (!IsEqualIID(rclsid, &CLSID_JamotongIME)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    JamotongClassFactory *factory = (JamotongClassFactory*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(JamotongClassFactory));
    if (!factory) return E_OUTOFMEMORY;

    factory->lpVtbl = &ClassFactoryVtbl;
    factory->refCount = 1;
    InterlockedIncrement(&g_DllRefCount);

    HRESULT hr = factory->lpVtbl->QueryInterface((IClassFactory*)factory, riid, ppv);
    factory->lpVtbl->Release((IClassFactory*)factory);
    return hr;
}

// ------------------------------------------------------------------
// DLL Registration
// ------------------------------------------------------------------

STDAPI DllRegisterServer(void) {
    HKEY hKey = NULL;
    HKEY hSubKey = NULL;
    WCHAR szModule[MAX_PATH];

    if (!GetModuleFileNameW(g_hInst, szModule, MAX_PATH)) return E_FAIL;

    // Register CLSID
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, c_szInfoKeyPrefix, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) return E_FAIL;
    RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)c_szDescription, (DWORD)(wcslen(c_szDescription) + 1) * sizeof(WCHAR));

    if (RegCreateKeyExW(hKey, c_szInprocServer32, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hSubKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hSubKey, NULL, 0, REG_SZ, (const BYTE*)szModule, (DWORD)(wcslen(szModule) + 1) * sizeof(WCHAR));
        RegSetValueExW(hSubKey, L"ThreadingModel", 0, REG_SZ, (const BYTE*)c_szModelName, (DWORD)(wcslen(c_szModelName) + 1) * sizeof(WCHAR));
        RegCloseKey(hSubKey);
    }
    RegCloseKey(hKey);

    RegisterProfiles();
    RegisterCategories();

    return S_OK;
}

STDAPI DllUnregisterServer(void) {
    UnregisterProfiles();
    UnregisterCategories();

    // Unregister CLSID
    RegDeleteTreeW(HKEY_CLASSES_ROOT, c_szInfoKeyPrefix);
    
    return S_OK;
}
