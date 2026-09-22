#include "display_attr.h"
#include "jamotong.h"   // JamotongTextService, IMPL_TO_OBJ

// Standard TSF IID for ITfDisplayAttributeProvider (not in this msctf.h).
const IID IID_ITfDisplayAttributeProvider_J =
    { 0xfee47777, 0x163c, 0x4769, { 0x99, 0x6a, 0x6e, 0x9c, 0x50, 0xad, 0x8f, 0x54 } };
// Jamotong composing display attribute id: {7C9C1A20-4F3B-4E8D-9A11-2B3C4D5E6F70}
const GUID GUID_JamotongComposingDA =
    { 0x7c9c1a20, 0x4f3b, 0x4e8d, { 0x9a, 0x11, 0x2b, 0x3c, 0x4d, 0x5e, 0x6f, 0x70 } };

// The visual: a solid underline marked as "input" (composing) — standard IME feedback.
static const TF_DISPLAYATTRIBUTE g_DA = {
    { TF_CT_NONE, { 0 } },   // crText  (app default)
    { TF_CT_NONE, { 0 } },   // crBk    (app default)
    TF_LS_SOLID,             // lsStyle (solid underline)
    FALSE,                   // fBoldLine
    { TF_CT_NONE, { 0 } },   // crLine  (app default)
    TF_ATTR_INPUT            // bAttr   (in-progress input)
};

// ── ITfDisplayAttributeInfo (stateless singleton) ───────────────────────────────
static HRESULT STDMETHODCALLTYPE DAI_QueryInterface(ITfDisplayAttributeInfo *self, REFIID riid, void **ppv) {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfDisplayAttributeInfo)) { *ppv = self; return S_OK; }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE DAI_AddRef(ITfDisplayAttributeInfo *self)  { (void)self; return 1; }
static ULONG STDMETHODCALLTYPE DAI_Release(ITfDisplayAttributeInfo *self) { (void)self; return 1; }
static HRESULT STDMETHODCALLTYPE DAI_GetGUID(ITfDisplayAttributeInfo *self, GUID *pguid) { (void)self; if (!pguid) return E_INVALIDARG; *pguid = GUID_JamotongComposingDA; return S_OK; }
static HRESULT STDMETHODCALLTYPE DAI_GetDescription(ITfDisplayAttributeInfo *self, BSTR *pbstr) { (void)self; if (!pbstr) return E_INVALIDARG; *pbstr = SysAllocString(L"Jamotong Composing"); return *pbstr ? S_OK : E_OUTOFMEMORY; }
static HRESULT STDMETHODCALLTYPE DAI_GetAttributeInfo(ITfDisplayAttributeInfo *self, TF_DISPLAYATTRIBUTE *pda) { (void)self; if (!pda) return E_INVALIDARG; *pda = g_DA; return S_OK; }
static HRESULT STDMETHODCALLTYPE DAI_SetAttributeInfo(ITfDisplayAttributeInfo *self, const TF_DISPLAYATTRIBUTE *pda) { (void)self; (void)pda; return E_NOTIMPL; }  // read-only
static HRESULT STDMETHODCALLTYPE DAI_Reset(ITfDisplayAttributeInfo *self) { (void)self; return S_OK; }

static ITfDisplayAttributeInfoVtbl g_DAIVtbl = {   // non-const: msctf.h's lpVtbl is non-const
    DAI_QueryInterface, DAI_AddRef, DAI_Release,
    DAI_GetGUID, DAI_GetDescription, DAI_GetAttributeInfo, DAI_SetAttributeInfo, DAI_Reset
};
static ITfDisplayAttributeInfo g_DAI = { &g_DAIVtbl };

// ── IEnumTfDisplayAttributeInfo — 호출마다 독립 객체 (RFC-0008 W2-01) ─────────────────
// 예전엔 전역 싱글턴이 위치를 공유해, 두 열거가 겹치면 서로의 위치를 바꿨고 Clone 은 같은 객체를
// 돌려줬다. 이제 열거자마다 자기 위치와 참조 수를 갖고, Clone 은 같은 위치의 새 객체다.
typedef struct DAEnum {
    IEnumTfDisplayAttributeInfoVtbl *lpVtbl;
    LONG refCount;
    ULONG pos;   // 0 = 하나뿐인 정보를 아직 안 줌, 1 = 다 줌
} DAEnum;
static IEnumTfDisplayAttributeInfoVtbl g_DAEVtbl;

static HRESULT DAEnum_Create(ULONG pos, IEnumTfDisplayAttributeInfo **ppEnum) {
    DAEnum *e = (DAEnum*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DAEnum));
    if (!e) { *ppEnum = NULL; return E_OUTOFMEMORY; }
    e->lpVtbl = &g_DAEVtbl; e->refCount = 1; e->pos = pos;
    InterlockedIncrement(&g_DllRefCount);   // 살아 있는 동안 DLL 을 내리지 않는다
    *ppEnum = (IEnumTfDisplayAttributeInfo*)e;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE DAE_QueryInterface(IEnumTfDisplayAttributeInfo *self, REFIID riid, void **ppv) {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumTfDisplayAttributeInfo)) {
        *ppv = self; self->lpVtbl->AddRef(self); return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE DAE_AddRef(IEnumTfDisplayAttributeInfo *self) {
    return (ULONG)InterlockedIncrement(&((DAEnum*)self)->refCount);
}
static ULONG STDMETHODCALLTYPE DAE_Release(IEnumTfDisplayAttributeInfo *self) {
    DAEnum *e = (DAEnum*)self;
    LONG r = InterlockedDecrement(&e->refCount);
    if (r == 0) { HeapFree(GetProcessHeap(), 0, e); InterlockedDecrement(&g_DllRefCount); }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE DAE_Clone(IEnumTfDisplayAttributeInfo *self, IEnumTfDisplayAttributeInfo **ppEnum) {
    if (!ppEnum) return E_INVALIDARG;
    return DAEnum_Create(((DAEnum*)self)->pos, ppEnum);
}
static HRESULT STDMETHODCALLTYPE DAE_Next(IEnumTfDisplayAttributeInfo *self, ULONG ulCount, ITfDisplayAttributeInfo **rgInfo, ULONG *pcFetched) {
    DAEnum *e = (DAEnum*)self; ULONG fetched = 0;
    if (ulCount > 0 && !rgInfo) return E_INVALIDARG;
    if (ulCount > 0 && e->pos == 0) { rgInfo[0] = &g_DAI; g_DAI.lpVtbl->AddRef(&g_DAI); e->pos = 1; fetched = 1; }
    if (pcFetched) *pcFetched = fetched;
    return (fetched == ulCount) ? S_OK : S_FALSE;
}
static HRESULT STDMETHODCALLTYPE DAE_Reset(IEnumTfDisplayAttributeInfo *self) { ((DAEnum*)self)->pos = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE DAE_Skip(IEnumTfDisplayAttributeInfo *self, ULONG ulCount) {
    DAEnum *e = (DAEnum*)self;
    if (ulCount == 0) return S_OK;
    bool had = (e->pos == 0);
    e->pos = 1;
    return (had && ulCount == 1) ? S_OK : S_FALSE;   // 건너뛸 항목이 모자라면 S_FALSE
}

static IEnumTfDisplayAttributeInfoVtbl g_DAEVtbl = {   // non-const: msctf.h's lpVtbl is non-const
    DAE_QueryInterface, DAE_AddRef, DAE_Release,
    DAE_Clone, DAE_Next, DAE_Reset, DAE_Skip
};

// ── ITfDisplayAttributeProvider (implemented on the TIP object) ──────────────────
static HRESULT STDMETHODCALLTYPE DAP_QueryInterface(ITfDisplayAttributeProvider *pThis, REFIID riid, void **ppv) {
    JamotongTextService *obj = IMPL_TO_OBJ(DAP, pThis);
    return obj->lpVtblTIP->QueryInterface((ITfTextInputProcessor*)obj, riid, ppv);
}
static ULONG STDMETHODCALLTYPE DAP_AddRef(ITfDisplayAttributeProvider *pThis) {
    JamotongTextService *obj = IMPL_TO_OBJ(DAP, pThis);
    return obj->lpVtblTIP->AddRef((ITfTextInputProcessor*)obj);
}
static ULONG STDMETHODCALLTYPE DAP_Release(ITfDisplayAttributeProvider *pThis) {
    JamotongTextService *obj = IMPL_TO_OBJ(DAP, pThis);
    return obj->lpVtblTIP->Release((ITfTextInputProcessor*)obj);
}
static HRESULT STDMETHODCALLTYPE DAP_EnumDisplayAttributeInfo(ITfDisplayAttributeProvider *pThis, IEnumTfDisplayAttributeInfo **ppEnum) {
    (void)pThis; if (!ppEnum) return E_INVALIDARG;
    return DAEnum_Create(0, ppEnum);
}
static HRESULT STDMETHODCALLTYPE DAP_GetDisplayAttributeInfo(ITfDisplayAttributeProvider *pThis, REFGUID guid, ITfDisplayAttributeInfo **ppInfo) {
    (void)pThis;
    if (!ppInfo) return E_INVALIDARG;
    if (IsEqualGUID(guid, &GUID_JamotongComposingDA)) { *ppInfo = &g_DAI; return S_OK; }
    *ppInfo = NULL; return E_INVALIDARG;
}
const ITfDisplayAttributeProviderVtbl g_JamotongDAPVtbl = {
    DAP_QueryInterface, DAP_AddRef, DAP_Release,
    DAP_EnumDisplayAttributeInfo, DAP_GetDisplayAttributeInfo
};

// ── helpers ─────────────────────────────────────────────────────────────────────
TfGuidAtom DA_RegisterAtom(ITfThreadMgr *threadMgr) {
    if (!threadMgr) return 0;
    ITfCategoryMgr *pCat = NULL;
    TfGuidAtom atom = 0;
    if (SUCCEEDED(threadMgr->lpVtbl->QueryInterface(threadMgr, &IID_ITfCategoryMgr, (void**)&pCat))) {
        pCat->lpVtbl->RegisterGUID(pCat, &GUID_JamotongComposingDA, &atom);
        pCat->lpVtbl->Release(pCat);
    }
    return atom;
}

void DA_ApplyToRange(ITfContext *context, TfEditCookie ec, ITfRange *range, TfGuidAtom atom) {
    if (!context || !range || atom == 0) return;
    ITfProperty *pProp = NULL;
    if (SUCCEEDED(context->lpVtbl->GetProperty(context, &GUID_PROP_ATTRIBUTE, &pProp))) {
        VARIANT var; VariantInit(&var);
        var.vt = VT_I4; var.lVal = (LONG)atom;
        pProp->lpVtbl->SetValue(pProp, ec, range, &var);
        pProp->lpVtbl->Release(pProp);
    }
}
