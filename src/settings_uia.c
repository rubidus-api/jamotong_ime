// 설정창 UI 자동화 제공자 — IRawElementProviderSimple 하나.
//   설정창은 블록을 직접 그린다 — 자식 컨트롤이 없으니 읽기 도구가 볼 것이 없다. 후보창의 제공자(cand_uia.c)와 같은 최소 방식으로,
//   창 하나가 "지금 포커스를 가진 블록의 글(이름 + 켜짐/꺼짐)"을 이름으로 알리고 포커스가 옮겨 갈 때 이벤트를 낸다.
//   (후보창 제공자와 상태를 나누지 않는다 — 두 창이 한 프로세스에 함께 있을 수 있다.)
#include "settings_uia.h"
#include <uiautomation.h>

typedef struct SetProv {
    IRawElementProviderSimpleVtbl *lpVtbl;
    LONG    ref;
    HWND    hwnd;
    wchar_t name[256];
} SetProv;

static SetProv *g_prov = NULL;

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
    SetProv *p = (SetProv*)pThis;
    return (ULONG)InterlockedIncrement(&p->ref);
}
static ULONG STDMETHODCALLTYPE P_Release(IRawElementProviderSimple *pThis) {
    SetProv *p = (SetProv*)pThis;
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
    SetProv *p = (SetProv*)pThis;
    if (!ret) return E_POINTER;
    VariantInit(ret);
    switch (id) {
        case UIA_ControlTypePropertyId:
            ret->vt = VT_I4; ret->lVal = UIA_PaneControlTypeId; return S_OK;
        case UIA_NamePropertyId:
            ret->vt = VT_BSTR; ret->bstrVal = SysAllocString(p->name[0] ? p->name : L"Jamotong settings"); return S_OK;
        case UIA_AutomationIdPropertyId:
            ret->vt = VT_BSTR; ret->bstrVal = SysAllocString(L"JamotongSettingsPage"); return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            ret->vt = VT_BOOL; ret->boolVal = VARIANT_TRUE; return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            ret->vt = VT_BOOL; ret->boolVal = VARIANT_TRUE; return S_OK;
        case UIA_HasKeyboardFocusPropertyId:
            ret->vt = VT_BOOL; ret->boolVal = (GetFocus() == p->hwnd) ? VARIANT_TRUE : VARIANT_FALSE; return S_OK;
        default:
            return S_OK;   // 빈 VARIANT = "모름" (UIA 가 기본값을 쓴다)
    }
}
static HRESULT STDMETHODCALLTYPE P_GetHostRawElementProvider(IRawElementProviderSimple *pThis, IRawElementProviderSimple **ret) {
    SetProv *p = (SetProv*)pThis;
    if (!ret) return E_POINTER;
    return UiaHostProviderFromHwnd(p->hwnd, ret);   // 창의 기본 정보(자리·크기)는 호스트가 준다
}
static IRawElementProviderSimpleVtbl g_provVtbl = {
    P_QueryInterface, P_AddRef, P_Release,
    P_GetProviderOptions, P_GetPatternProvider, P_GetPropertyValue, P_GetHostRawElementProvider
};

static SetProv *EnsureProvider(HWND hwnd) {
    if (g_prov && g_prov->hwnd == hwnd) return g_prov;
    if (g_prov) { g_prov->hwnd = hwnd; return g_prov; }
    SetProv *p = (SetProv*)calloc(1, sizeof *p);
    if (!p) return NULL;
    p->lpVtbl = &g_provVtbl;
    p->ref = 1;
    p->hwnd = hwnd;
    g_prov = p;
    return p;
}

LRESULT SettingsUia_OnGetObject(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    if ((DWORD)lParam != (DWORD)UiaRootObjectId) return 0;
    SetProv *p = EnsureProvider(hwnd);
    if (!p) return 0;
    return UiaReturnRawElementProvider(hwnd, wParam, lParam, (IRawElementProviderSimple*)p);
}

void SettingsUia_SetName(HWND hwnd, const wchar_t *name) {
    if (!hwnd || !name) return;
    SetProv *p = EnsureProvider(hwnd);
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
        if (GetFocus() == hwnd)   // 포커스가 옮겨 갔다 — 새 블록의 이름을 읽게 한다
            UiaRaiseAutomationEvent((IRawElementProviderSimple*)p, UIA_AutomationFocusChangedEventId);
    }
}

void SettingsUia_Release(void) {
    if (!g_prov) return;
    UiaReturnRawElementProvider(g_prov->hwnd, 0, 0, NULL);   // 캐시 정리 (창이 사라진다)
    IRawElementProviderSimple *p = (IRawElementProviderSimple*)g_prov;
    g_prov = NULL;
    p->lpVtbl->Release(p);
}
