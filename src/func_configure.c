// ITfFunctionProvider + ITfFnConfigure 구현.
//   Windows 언어 설정: [설정 → 언어 → 한국어 → 키보드 → Jamotong IME → "옵션"] 버튼을 누르면
//   프레임워크가 이 CLSID를 만들어 ITfFunctionProvider를 QI → GetFunction(ITfFnConfigure) →
//   ITfFnConfigure::Show() 를 호출한다. Show에서 우리 설정창을 연다.
//   (데스크톱 언어바를 켜지 않아도 설정에 접근 가능해짐.)
#include "jamotong.h"
#include "settings_ui.h"

// 표준 TSF IID (이 msctf.h엔 없어 직접 정의)
const GUID IID_ITfFunction_J    = { 0x101d6610, 0x0990, 0x11d3, { 0x8d, 0xf0, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 } };
const GUID IID_ITfFnConfigure_J = { 0x88f567c6, 0x1757, 0x49f8, { 0xa1, 0xb2, 0x89, 0x23, 0x4c, 0x1e, 0xef, 0xf9 } };

// ── ITfFunctionProvider (TIP 객체에 구현) ────────────────────────────────────────
static HRESULT STDMETHODCALLTYPE FP_QueryInterface(ITfFunctionProvider *p, REFIID riid, void **ppv) {
    JamotongTextService *obj = IMPL_TO_OBJ(FuncProv, p);
    return obj->lpVtblTIP->QueryInterface((ITfTextInputProcessor*)obj, riid, ppv);
}
static ULONG STDMETHODCALLTYPE FP_AddRef(ITfFunctionProvider *p) {
    JamotongTextService *obj = IMPL_TO_OBJ(FuncProv, p);
    return obj->lpVtblTIP->AddRef((ITfTextInputProcessor*)obj);
}
static ULONG STDMETHODCALLTYPE FP_Release(ITfFunctionProvider *p) {
    JamotongTextService *obj = IMPL_TO_OBJ(FuncProv, p);
    return obj->lpVtblTIP->Release((ITfTextInputProcessor*)obj);
}
static HRESULT STDMETHODCALLTYPE FP_GetType(ITfFunctionProvider *p, GUID *pguid) {
    (void)p; if (!pguid) return E_INVALIDARG; *pguid = CLSID_JamotongIME; return S_OK;
}
static HRESULT STDMETHODCALLTYPE FP_GetDescription(ITfFunctionProvider *p, BSTR *pbstr) {
    (void)p; if (!pbstr) return E_INVALIDARG;
    *pbstr = SysAllocString(L"Jamotong IME"); return *pbstr ? S_OK : E_OUTOFMEMORY;
}
static HRESULT STDMETHODCALLTYPE FP_GetFunction(ITfFunctionProvider *p, REFGUID rguid, REFIID riid, IUnknown **ppunk) {
    JamotongTextService *obj = IMPL_TO_OBJ(FuncProv, p);
    (void)rguid;
    if (!ppunk) return E_INVALIDARG;
    *ppunk = NULL;
    if (IsEqualIID(riid, &IID_ITfFnConfigure_J) || IsEqualIID(riid, &IID_ITfFunction_J)) {
        *ppunk = (IUnknown*)&obj->lpVtblFnConfig;
        obj->lpVtblTIP->AddRef((ITfTextInputProcessor*)obj);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ITfFunctionProviderVtbl g_FuncProvVtbl = {
    FP_QueryInterface, FP_AddRef, FP_Release, FP_GetType, FP_GetDescription, FP_GetFunction
};

// ── ITfFnConfigure (TIP 객체에 구현) ─────────────────────────────────────────────
static HRESULT STDMETHODCALLTYPE FC_QueryInterface(ITfFnConfigure *p, REFIID riid, void **ppv) {
    JamotongTextService *obj = IMPL_TO_OBJ(FnConfig, p);
    return obj->lpVtblTIP->QueryInterface((ITfTextInputProcessor*)obj, riid, ppv);
}
static ULONG STDMETHODCALLTYPE FC_AddRef(ITfFnConfigure *p) {
    JamotongTextService *obj = IMPL_TO_OBJ(FnConfig, p);
    return obj->lpVtblTIP->AddRef((ITfTextInputProcessor*)obj);
}
static ULONG STDMETHODCALLTYPE FC_Release(ITfFnConfigure *p) {
    JamotongTextService *obj = IMPL_TO_OBJ(FnConfig, p);
    return obj->lpVtblTIP->Release((ITfTextInputProcessor*)obj);
}
static HRESULT STDMETHODCALLTYPE FC_GetDisplayName(ITfFnConfigure *p, BSTR *pbstr) {
    (void)p; if (!pbstr) return E_INVALIDARG;
    *pbstr = SysAllocString(L"Jamotong IME Settings"); return *pbstr ? S_OK : E_OUTOFMEMORY;
}
static HRESULT STDMETHODCALLTYPE FC_Show(ITfFnConfigure *p, HWND hwndParent, LANGID langid, REFGUID rguidProfile) {
    JamotongTextService *obj = IMPL_TO_OBJ(FnConfig, p);
    (void)hwndParent; (void)langid; (void)rguidProfile;
    SettingsUI_Show(&obj->config);   // 설정창 (Apply 시 사용자 config 파일에 저장됨)
    return S_OK;
}
static const ITfFnConfigureVtbl g_FnConfigVtbl = {
    FC_QueryInterface, FC_AddRef, FC_Release, FC_GetDisplayName, FC_Show
};

// ── 공개 함수 ─────────────────────────────────────────────────────────────────────
void FuncConfig_Init(JamotongTextService *obj) {
    obj->lpVtblFuncProv = &g_FuncProvVtbl;
    obj->lpVtblFnConfig = &g_FnConfigVtbl;
}

HRESULT FuncConfig_Advise(JamotongTextService *obj) {
    if (!obj->threadMgr) return E_FAIL;
    ITfSourceSingle *pSS = NULL;
    HRESULT hr = obj->threadMgr->lpVtbl->QueryInterface(obj->threadMgr, &IID_ITfSourceSingle, (void**)&pSS);
    if (SUCCEEDED(hr) && pSS) {
        hr = pSS->lpVtbl->AdviseSingleSink(pSS, obj->clientId, &IID_ITfFunctionProvider,
                                           (IUnknown*)&obj->lpVtblFuncProv);
        pSS->lpVtbl->Release(pSS);
    }
    return hr;
}

void FuncConfig_Unadvise(JamotongTextService *obj) {
    if (!obj->threadMgr) return;
    ITfSourceSingle *pSS = NULL;
    if (SUCCEEDED(obj->threadMgr->lpVtbl->QueryInterface(obj->threadMgr, &IID_ITfSourceSingle, (void**)&pSS)) && pSS) {
        pSS->lpVtbl->UnadviseSingleSink(pSS, obj->clientId, &IID_ITfFunctionProvider);
        pSS->lpVtbl->Release(pSS);
    }
}
