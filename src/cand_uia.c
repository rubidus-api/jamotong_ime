// 후보창 UI 자동화 제공자 — IRawElementProviderSimple 하나 (B10).
//   더 붙이지 않는다: 목록 항목까지 제공자로 내면 우리 그리기와 두 벌 관리가 된다. 지금 필요한 것은
//   "후보창이 떴다 / 지금 이것이 골라졌다"를 읽기 도구가 알 수 있게 하는 것뿐이다.
#include "cand_uia.h"
#include <uiautomation.h>

typedef struct CandProv {
    IRawElementProviderSimpleVtbl *lpVtbl;
    LONG    ref;
    HWND    hwnd;
    wchar_t name[256];
} CandProv;

static CandProv *g_prov = NULL;

static HRESULT STDMETHODCALLTYPE P_QueryInterface(IRawElementProviderSimple *pThis, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IRawElementProviderSimple)) {
        *ppv = pThis;
        pThis->lpVtbl->AddRef(pThis);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE P_AddRef(IRawElementProviderSimple *pThis) {
    CandProv *p = (CandProv*)pThis;
    return (ULONG)InterlockedIncrement(&p->ref);
}
static ULONG STDMETHODCALLTYPE P_Release(IRawElementProviderSimple *pThis) {
    CandProv *p = (CandProv*)pThis;
    LONG n = InterlockedDecrement(&p->ref);
    if (n == 0) { if (g_prov == p) g_prov = NULL; free(p); }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE P_GetProviderOptions(IRawElementProviderSimple *pThis, enum ProviderOptions *opt) {
    (void)pThis;
    if (!opt) return E_POINTER;
    *opt = ProviderOptions_ServerSideProvider;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE P_GetPatternProvider(IRawElementProviderSimple *pThis, PATTERNID id, IUnknown **ret) {
    (void)pThis; (void)id;
    if (!ret) return E_POINTER;
    *ret = NULL;                 // 패턴은 대지 않는다 — 고르기는 키보드로만 한다
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE P_GetPropertyValue(IRawElementProviderSimple *pThis, PROPERTYID id, VARIANT *ret) {
    CandProv *p = (CandProv*)pThis;
    if (!ret) return E_POINTER;
    VariantInit(ret);
    switch (id) {
        case UIA_ControlTypePropertyId:
            ret->vt = VT_I4; ret->lVal = UIA_ListControlTypeId; return S_OK;
        case UIA_NamePropertyId:
            ret->vt = VT_BSTR; ret->bstrVal = SysAllocString(p->name[0] ? p->name : L"Jamotong candidates"); return S_OK;
        case UIA_AutomationIdPropertyId:
            ret->vt = VT_BSTR; ret->bstrVal = SysAllocString(L"JamotongCandidateList"); return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            ret->vt = VT_BOOL; ret->boolVal = VARIANT_TRUE; return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
        case UIA_HasKeyboardFocusPropertyId:
            ret->vt = VT_BOOL; ret->boolVal = VARIANT_FALSE; return S_OK;   // 포커스는 앱이 쥔다
        default:
            return S_OK;   // 빈 VARIANT = "모름" (UIA 가 기본값을 쓴다)
    }
}
static HRESULT STDMETHODCALLTYPE P_GetHostRawElementProvider(IRawElementProviderSimple *pThis, IRawElementProviderSimple **ret) {
    CandProv *p = (CandProv*)pThis;
    if (!ret) return E_POINTER;
    return UiaHostProviderFromHwnd(p->hwnd, ret);   // 창의 기본 정보(자리·크기)는 호스트가 준다
}
static IRawElementProviderSimpleVtbl g_provVtbl = {
    P_QueryInterface, P_AddRef, P_Release,
    P_GetProviderOptions, P_GetPatternProvider, P_GetPropertyValue, P_GetHostRawElementProvider
};

static CandProv *EnsureProvider(HWND hwnd) {
    if (g_prov && g_prov->hwnd == hwnd) return g_prov;
    if (g_prov) { g_prov->hwnd = hwnd; return g_prov; }
    CandProv *p = (CandProv*)calloc(1, sizeof *p);
    if (!p) return NULL;
    p->lpVtbl = &g_provVtbl;
    p->ref = 1;
    p->hwnd = hwnd;
    g_prov = p;
    return p;
}

LRESULT CandUia_OnGetObject(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    if ((DWORD)lParam != (DWORD)UiaRootObjectId) return 0;
    CandProv *p = EnsureProvider(hwnd);
    if (!p) return 0;
    return UiaReturnRawElementProvider(hwnd, wParam, lParam, (IRawElementProviderSimple*)p);
}

void CandUia_SetName(HWND hwnd, const wchar_t *name) {
    if (!hwnd || !name) return;
    CandProv *p = EnsureProvider(hwnd);
    if (!p) return;
    if (wcscmp(p->name, name) == 0) return;
    lstrcpynW(p->name, name, 256);
    // 읽기 도구에 "이름이 바뀌었다"고 알린다 — 듣는 쪽이 없으면 아무 일도 없다.
    if (UiaClientsAreListening()) {
        VARIANT vOld, vNew;
        VariantInit(&vOld); VariantInit(&vNew);
        vNew.vt = VT_BSTR; vNew.bstrVal = SysAllocString(p->name);
        UiaRaiseAutomationPropertyChangedEvent((IRawElementProviderSimple*)p, UIA_NamePropertyId, vOld, vNew);
        VariantClear(&vNew);
    }
}

void CandUia_Release(void) {
    if (!g_prov) return;
    UiaReturnRawElementProvider(g_prov->hwnd, 0, 0, NULL);   // 캐시 정리 (창이 사라진다)
    IRawElementProviderSimple *p = (IRawElementProviderSimple*)g_prov;
    g_prov = NULL;
    p->lpVtbl->Release(p);
}
