#include "display_attr.h"
#include "jamotong.h"   // JamotongTextService, IMPL_TO_OBJ

// Standard TSF IID for ITfDisplayAttributeProvider (not in this msctf.h).
const IID IID_ITfDisplayAttributeProvider_J =
    { 0x8ded7393, 0x5db1, 0x475c, { 0x9e, 0x71, 0xa3, 0x91, 0x11, 0xb0, 0xff, 0x67 } };
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

// ── IEnumTfDisplayAttributeInfo (singleton; position reset on each Enum call) ────
static int g_enumPos;   // 0 = the single info not yet returned, 1 = exhausted

static HRESULT STDMETHODCALLTYPE DAE_QueryInterface(IEnumTfDisplayAttributeInfo *self, REFIID riid, void **ppv) {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumTfDisplayAttributeInfo)) { *ppv = self; return S_OK; }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE DAE_AddRef(IEnumTfDisplayAttributeInfo *self)  { (void)self; return 1; }
static ULONG STDMETHODCALLTYPE DAE_Release(IEnumTfDisplayAttributeInfo *self) { (void)self; return 1; }
static HRESULT STDMETHODCALLTYPE DAE_Clone(IEnumTfDisplayAttributeInfo *self, IEnumTfDisplayAttributeInfo **ppEnum) { if (!ppEnum) return E_INVALIDARG; *ppEnum = self; return S_OK; }
static HRESULT STDMETHODCALLTYPE DAE_Next(IEnumTfDisplayAttributeInfo *self, ULONG ulCount, ITfDisplayAttributeInfo **rgInfo, ULONG *pcFetched) {
    (void)self; ULONG fetched = 0;
    if (ulCount > 0 && rgInfo && g_enumPos == 0) { rgInfo[0] = &g_DAI; g_enumPos = 1; fetched = 1; }
    if (pcFetched) *pcFetched = fetched;
    return (fetched == ulCount) ? S_OK : S_FALSE;
}
static HRESULT STDMETHODCALLTYPE DAE_Reset(IEnumTfDisplayAttributeInfo *self) { (void)self; g_enumPos = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE DAE_Skip(IEnumTfDisplayAttributeInfo *self, ULONG ulCount) { (void)self; if (ulCount) g_enumPos = 1; return S_OK; }

static IEnumTfDisplayAttributeInfoVtbl g_DAEVtbl = {   // non-const: msctf.h's lpVtbl is non-const
    DAE_QueryInterface, DAE_AddRef, DAE_Release,
    DAE_Clone, DAE_Next, DAE_Reset, DAE_Skip
};
static IEnumTfDisplayAttributeInfo g_DAE = { &g_DAEVtbl };

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
    g_enumPos = 0; *ppEnum = &g_DAE; return S_OK;
}
static HRESULT STDMETHODCALLTYPE DAP_GetDisplayAttributeInfo(ITfDisplayAttributeProvider *pThis, REFGUID guid, ITfDisplayAttributeInfo **ppInfo, TfGuidAtom *pAtom) {
    (void)pThis; (void)pAtom;
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
