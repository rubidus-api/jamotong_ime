/* dllmain.c — COM 진입점과 클래스 팩토리 (RFC-0009 Phase 1) */
#define INITGUID
#include <initguid.h>
#include "lab_tip.h"
#include <olectl.h>

/* 실험체 전용 GUID — 제품과 겹치지 않게 새로 생성한 값 (RFC-0009 §3) */
#ifdef LAB_TRACE_BUILD
/* Trace variant: also distinct from the normal standard lab for side-by-side testing. */
DEFINE_GUID(CLSID_LabService, 0x130651f1,0xfbc3,0x43bb,0x85,0xa4,0x85,0x92,0x59,0x9f,0xdf,0x43);
DEFINE_GUID(GUID_LabProfile,  0xfb13ff32,0xffd0,0x4241,0xb5,0xb7,0x5c,0x53,0xf5,0x86,0xbd,0x5a);
#else
DEFINE_GUID(CLSID_LabService, 0x703090af,0x5c21,0x43d1,0xaf,0x61,0x33,0x5d,0x23,0x07,0xda,0x83);
DEFINE_GUID(GUID_LabProfile,  0xda036654,0x9b87,0x4609,0x9e,0xcc,0x6e,0x58,0x0e,0xeb,0xfa,0x5f);
#endif

HINSTANCE g_lab_instance = NULL;
static LONG g_dll_ref = 0;

void Lab_DllAddRef(void)  { InterlockedIncrement(&g_dll_ref); }
void Lab_DllRelease(void) { InterlockedDecrement(&g_dll_ref); }

static STDMETHODIMP CF_QI(IClassFactory *me, REFIID riid, void **ppv) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) {
        *ppv = me; return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static STDMETHODIMP_(ULONG) CF_AddRef(IClassFactory *me)  { (void)me; return 2; }
static STDMETHODIMP_(ULONG) CF_Release(IClassFactory *me) { (void)me; return 1; }
static STDMETHODIMP CF_CreateInstance(IClassFactory *me, IUnknown *outer, REFIID riid, void **ppv) {
    (void)me;
    if (outer) return CLASS_E_NOAGGREGATION;
    LabTextService *svc = Lab_CreateService();
    if (!svc) return E_OUTOFMEMORY;
    /* ITfTextInputProcessor의 vtbl을 직접 쓴다. IUnknown*로 캐스팅해 역참조하면
       -Werror=strict-aliasing에 걸린다(MinGW GCC 16). */
    HRESULT hr = svc->tip.lpVtbl->QueryInterface(&svc->tip, riid, ppv);
    svc->tip.lpVtbl->Release(&svc->tip);
    return hr;
}
static STDMETHODIMP CF_LockServer(IClassFactory *me, BOOL lock) {
    (void)me;
    if (lock) Lab_DllAddRef(); else Lab_DllRelease();
    return S_OK;
}
static const IClassFactoryVtbl g_cf_vtbl = {
    CF_QI, CF_AddRef, CF_Release, CF_CreateInstance, CF_LockServer
};
static IClassFactory g_factory = { (IClassFactoryVtbl*)&g_cf_vtbl };

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_lab_instance = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv) {
    if (!IsEqualCLSID(rclsid, &CLSID_LabService)) return CLASS_E_CLASSNOTAVAILABLE;
    return CF_QI(&g_factory, riid, ppv);
}
STDAPI DllCanUnloadNow(void)      { return g_dll_ref == 0 ? S_OK : S_FALSE; }
STDAPI DllRegisterServer(void)    { return Lab_RegisterServer(); }
STDAPI DllUnregisterServer(void)  { return Lab_UnregisterServer(); }
