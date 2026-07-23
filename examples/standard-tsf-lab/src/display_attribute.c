/* display_attribute.c — 미확정 구간 인라인 표시 (RFC-0009 §10)
 *
 * composition range에 GUID_PROP_ATTRIBUTE property로 우리 속성 GUID를 건다.
 * 앱은 그 GUID를 우리 ITfDisplayAttributeProvider에 물어 실제 색/밑줄을 얻는다.
 */
#include "lab_tip.h"
#include <stddef.h>

/* MinGW에 없는 IID를 직접 정의 (Microsoft msctf.h와 같은 값) */
const GUID IID_ITfDisplayAttributeProvider_Lab =
    { 0xfee47777, 0x163c, 0x4769, { 0x99, 0x6a, 0x6e, 0x9c, 0x50, 0xad, 0x8f, 0x54 } };

/* Every side-by-side build uses an independent display-attribute GUID. */
#if defined(LAB_AKEL_META_R2_CONTROL_BUILD)
const GUID GUID_LabDisplayAttributeInput =
    { 0x1fec08c0, 0xd019, 0x4d2d, { 0xaf, 0x1d, 0x34, 0xaa, 0x84, 0xbb, 0xe6, 0x2a } };
#elif defined(LAB_AKEL_META_R2_LANGID_BUILD)
const GUID GUID_LabDisplayAttributeInput =
    { 0xd2b404dd, 0xbac3, 0x4de1, { 0x8a, 0x1d, 0xa3, 0x3e, 0xed, 0x76, 0x88, 0x9b } };
#elif defined(LAB_AKEL_META_R2_READING_BUILD)
const GUID GUID_LabDisplayAttributeInput =
    { 0x60104353, 0xa740, 0x4807, { 0x9c, 0xca, 0xe6, 0x30, 0x11, 0xda, 0x75, 0xe5 } };
#elif defined(LAB_AKEL_META_R2_BOTH_BUILD)
const GUID GUID_LabDisplayAttributeInput =
    { 0x78a5eb24, 0xd986, 0x455a, { 0xbd, 0x19, 0x49, 0x47, 0xf3, 0x1b, 0x9c, 0xc5 } };
#elif defined(LAB_AKEL_META_CONTROL_BUILD)
const GUID GUID_LabDisplayAttributeInput =
    { 0x9698159e, 0xe147, 0x4546, { 0xa0, 0xcd, 0xe9, 0xe2, 0x47, 0x03, 0xbe, 0x5b } };
#elif defined(LAB_AKEL_META_LANGID_BUILD)
const GUID GUID_LabDisplayAttributeInput =
    { 0xd96d8bfd, 0x286c, 0x4f51, { 0xad, 0x1e, 0x43, 0xde, 0x87, 0xc3, 0x63, 0x01 } };
#elif defined(LAB_AKEL_META_READING_BUILD)
const GUID GUID_LabDisplayAttributeInput =
    { 0x5b2bdab1, 0xbc77, 0x4264, { 0xa5, 0xbf, 0x2d, 0x23, 0x71, 0x48, 0xe7, 0x6d } };
#elif defined(LAB_AKEL_META_BOTH_BUILD)
const GUID GUID_LabDisplayAttributeInput =
    { 0x3dcc0e0b, 0x9e59, 0x42fd, { 0x94, 0x78, 0x87, 0x8c, 0xd4, 0xac, 0x58, 0xf4 } };
#elif defined(LAB_AKEL_CONTROL_BUILD)
const GUID GUID_LabDisplayAttributeInput =
    { 0x30475901, 0xae96, 0x4a2e, { 0xb9, 0x16, 0x98, 0x11, 0x68, 0x71, 0xc9, 0x99 } };
#elif defined(LAB_AKEL_AE_NONE_BUILD)
const GUID GUID_LabDisplayAttributeInput =
    { 0x1825edcf, 0x1c72, 0x4c3b, { 0xac, 0x27, 0x97, 0x44, 0x1a, 0xa7, 0x8d, 0xe0 } };
#elif defined(LAB_AKEL_INSERT_FIRST_BUILD)
const GUID GUID_LabDisplayAttributeInput =
    { 0xa08c7b05, 0x1a9f, 0x414c, { 0xb7, 0xa4, 0xe3, 0xa4, 0x11, 0x40, 0x7c, 0xed } };
#elif defined(LAB_AKEL_NO_SELECTION_BUILD)
const GUID GUID_LabDisplayAttributeInput =
    { 0xcd8159b6, 0xc185, 0x4b07, { 0xb0, 0x4d, 0x98, 0x47, 0xa7, 0xee, 0x1d, 0xc1 } };
#elif defined(LAB_TRACE_BUILD)
const GUID GUID_LabDisplayAttributeInput =
    { 0x216fcb02, 0x8417, 0x4393, { 0x8d, 0x4e, 0x8f, 0xa8, 0xeb, 0xa9, 0x5c, 0x90 } };
#else
const GUID GUID_LabDisplayAttributeInput =
    { 0x2f9b6c11, 0x74ad, 0x4b0e, { 0x9c, 0x33, 0x5a, 0x71, 0x0e, 0x24, 0x88, 0x10 } };
#endif

/* ── ITfDisplayAttributeInfo: 실제 모양을 돌려주는 객체 ── */
typedef struct {
    ITfDisplayAttributeInfo base;
    LONG ref;
} LabAttrInfo;

static STDMETHODIMP AI_QI(ITfDisplayAttributeInfo *me, REFIID riid, void **ppv) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfDisplayAttributeInfo)) {
        *ppv = me; ((LabAttrInfo*)me)->ref++; return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static STDMETHODIMP_(ULONG) AI_AddRef(ITfDisplayAttributeInfo *me) {
    return (ULONG)InterlockedIncrement(&((LabAttrInfo*)me)->ref);
}
static STDMETHODIMP_(ULONG) AI_Release(ITfDisplayAttributeInfo *me) {
    LONG n = InterlockedDecrement(&((LabAttrInfo*)me)->ref);
    return (ULONG)(n < 0 ? 0 : n);          /* 정적 인스턴스라 free하지 않는다 */
}
static STDMETHODIMP AI_GetGUID(ITfDisplayAttributeInfo *me, GUID *guid) {
    (void)me; *guid = GUID_LabDisplayAttributeInput; return S_OK;
}
static STDMETHODIMP AI_GetDescription(ITfDisplayAttributeInfo *me, BSTR *desc) {
    (void)me;
    *desc = SysAllocString(L"Lab input");
    return *desc ? S_OK : E_OUTOFMEMORY;
}
static void FillAttribute(TF_DISPLAYATTRIBUTE *a) {
    ZeroMemory(a, sizeof(*a));
    a->crText.type = TF_CT_NONE;
    a->crBk.type   = TF_CT_NONE;
    a->lsStyle     = TF_LS_SOLID;    /* 실선 밑줄 = 미확정 */
    a->fBoldLine   = FALSE;
    a->crLine.type = TF_CT_NONE;
    a->bAttr       = TF_ATTR_INPUT;
}
static STDMETHODIMP AI_GetAttributeInfo(ITfDisplayAttributeInfo *me, TF_DISPLAYATTRIBUTE *a) {
    (void)me; FillAttribute(a); return S_OK;
}
static STDMETHODIMP AI_SetAttributeInfo(ITfDisplayAttributeInfo *me, const TF_DISPLAYATTRIBUTE *a) {
    (void)me;(void)a; return E_NOTIMPL;      /* 사용자 변경은 지원하지 않는다 */
}
static STDMETHODIMP AI_Reset(ITfDisplayAttributeInfo *me) { (void)me; return S_OK; }

static const ITfDisplayAttributeInfoVtbl g_attr_info_vtbl = {
    AI_QI, AI_AddRef, AI_Release,
    AI_GetGUID, AI_GetDescription, AI_GetAttributeInfo, AI_SetAttributeInfo, AI_Reset
};
static LabAttrInfo g_attr_info = { { (ITfDisplayAttributeInfoVtbl*)&g_attr_info_vtbl }, 1 };

/* ── IEnumTfDisplayAttributeInfo: 우리가 가진 속성은 하나뿐 ── */
typedef struct { IEnumTfDisplayAttributeInfo base; LONG ref; ULONG pos; } LabAttrEnum;

static STDMETHODIMP AE_QI(IEnumTfDisplayAttributeInfo *me, REFIID riid, void **ppv) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumTfDisplayAttributeInfo)) {
        *ppv = me; InterlockedIncrement(&((LabAttrEnum*)me)->ref); return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static STDMETHODIMP_(ULONG) AE_AddRef(IEnumTfDisplayAttributeInfo *me) {
    return (ULONG)InterlockedIncrement(&((LabAttrEnum*)me)->ref);
}
static STDMETHODIMP_(ULONG) AE_Release(IEnumTfDisplayAttributeInfo *me) {
    LabAttrEnum *e = (LabAttrEnum*)me;
    LONG n = InterlockedDecrement(&e->ref);
    if (n == 0) CoTaskMemFree(e);
    return (ULONG)(n < 0 ? 0 : n);
}
static STDMETHODIMP AE_Clone(IEnumTfDisplayAttributeInfo *me, IEnumTfDisplayAttributeInfo **out);
static STDMETHODIMP AE_Next(IEnumTfDisplayAttributeInfo *me, ULONG count,
                            ITfDisplayAttributeInfo **info, ULONG *fetched) {
    LabAttrEnum *e = (LabAttrEnum*)me;
    ULONG n = 0;
    if (count > 0 && e->pos == 0) {
        info[0] = &g_attr_info.base;
        AI_AddRef(info[0]);
        e->pos = 1; n = 1;
    }
    if (fetched) *fetched = n;
    return n == count ? S_OK : S_FALSE;
}
static STDMETHODIMP AE_Reset(IEnumTfDisplayAttributeInfo *me) {
    ((LabAttrEnum*)me)->pos = 0; return S_OK;
}
static STDMETHODIMP AE_Skip(IEnumTfDisplayAttributeInfo *me, ULONG count) {
    LabAttrEnum *e = (LabAttrEnum*)me;
    if (count == 0) return S_OK;
    if (e->pos == 0) { e->pos = 1; return count == 1 ? S_OK : S_FALSE; }
    return S_FALSE;
}
static const IEnumTfDisplayAttributeInfoVtbl g_attr_enum_vtbl = {
    AE_QI, AE_AddRef, AE_Release, AE_Clone, AE_Next, AE_Reset, AE_Skip
};
static IEnumTfDisplayAttributeInfo *NewAttrEnum(void) {
    LabAttrEnum *e = (LabAttrEnum*)CoTaskMemAlloc(sizeof(*e));
    if (!e) return NULL;
    e->base.lpVtbl = (IEnumTfDisplayAttributeInfoVtbl*)&g_attr_enum_vtbl;
    e->ref = 1; e->pos = 0;
    return &e->base;
}
static STDMETHODIMP AE_Clone(IEnumTfDisplayAttributeInfo *me, IEnumTfDisplayAttributeInfo **out) {
    (void)me;
    *out = NewAttrEnum();
    return *out ? S_OK : E_OUTOFMEMORY;
}

/* ── ITfDisplayAttributeProvider: 서비스 객체에 얹힌다 ── */
#define FROM_PROVIDER(p) ((LabTextService*)((char*)(p) - offsetof(LabTextService, attr_provider)))

static STDMETHODIMP DP_QI(ITfDisplayAttributeProvider *me, REFIID riid, void **ppv) {
    LabTextService *svc = FROM_PROVIDER(me);
    return svc->tip.lpVtbl->QueryInterface(&svc->tip, riid, ppv);
}
static STDMETHODIMP_(ULONG) DP_AddRef(ITfDisplayAttributeProvider *me) {
    LabTextService *svc = FROM_PROVIDER(me);
    return svc->tip.lpVtbl->AddRef(&svc->tip);
}
static STDMETHODIMP_(ULONG) DP_Release(ITfDisplayAttributeProvider *me) {
    LabTextService *svc = FROM_PROVIDER(me);
    return svc->tip.lpVtbl->Release(&svc->tip);
}
static STDMETHODIMP DP_EnumDisplayAttributeInfo(ITfDisplayAttributeProvider *me,
                                                IEnumTfDisplayAttributeInfo **out) {
    (void)me;
    *out = NewAttrEnum();
    return *out ? S_OK : E_OUTOFMEMORY;
}
static STDMETHODIMP DP_GetDisplayAttributeInfo(ITfDisplayAttributeProvider *me,
                                               REFGUID guid, ITfDisplayAttributeInfo **info) {
    (void)me;
    if (!IsEqualGUID(guid, &GUID_LabDisplayAttributeInput)) {
        *info = NULL; return E_INVALIDARG;
    }
    *info = &g_attr_info.base;
    AI_AddRef(*info);
    return S_OK;
}
static const ITfDisplayAttributeProviderVtbl g_provider_vtbl = {
    DP_QI, DP_AddRef, DP_Release, DP_EnumDisplayAttributeInfo, DP_GetDisplayAttributeInfo
};

void Lab_InitDisplayAttribute(LabTextService *svc) {
    svc->attr_provider.lpVtbl = (ITfDisplayAttributeProviderVtbl*)&g_provider_vtbl;
}

/* composition range에 속성을 건다. 실패해도 조합 자체는 성립하므로
   호출자는 이 실패로 트랜잭션을 되돌리지 않는다(로그만 남긴다). */
HRESULT Lab_ApplyInputAttribute(LabTextService *svc, TfEditCookie ec,
                                ITfContext *ctx, ITfRange *range) {
    ITfProperty *prop = NULL;
    ITfCategoryMgr *cat = NULL;
    HRESULT hr = ITfContext_GetProperty(ctx, &GUID_PROP_ATTRIBUTE, &prop);
    if (FAILED(hr)) return hr;

    TfGuidAtom atom = TF_INVALID_GUIDATOM;
    hr = CoCreateInstance(&CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITfCategoryMgr, (void**)&cat);
    if (SUCCEEDED(hr)) {
        hr = ITfCategoryMgr_RegisterGUID(cat, &GUID_LabDisplayAttributeInput, &atom);
        ITfCategoryMgr_Release(cat);
    }
    if (SUCCEEDED(hr) && atom != TF_INVALID_GUIDATOM) {
        VARIANT v; VariantInit(&v);
        v.vt = VT_I4; v.lVal = (LONG)atom;
        hr = ITfProperty_SetValue(prop, ec, range, &v);
        VariantClear(&v);
    }
    ITfProperty_Release(prop);
    (void)svc;
    return hr;
}

/* 표시 속성 제공자로 등록해야 앱이 우리에게 모양을 물어본다 */
HRESULT Lab_RegisterDisplayAttributeCategory(bool add) {
    ITfCategoryMgr *cat = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_ITfCategoryMgr, (void**)&cat);
    if (FAILED(hr)) return hr;
    if (add)
        hr = ITfCategoryMgr_RegisterCategory(cat, &CLSID_LabService,
                &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, &CLSID_LabService);
    else
        hr = ITfCategoryMgr_UnregisterCategory(cat, &CLSID_LabService,
                &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, &CLSID_LabService);
    ITfCategoryMgr_Release(cat);
    return hr;
}
