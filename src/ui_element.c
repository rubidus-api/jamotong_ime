#include "ui_element.h"

// ── mingw msctf.h 에 없는 것들 (출처: Windows SDK msctf.h — 시그니처 원문 대조 2026-09-02) ──
// ITfCandidateListUIElement IID = MIDL_INTERFACE("ea1ea138-19df-11d7-a6d2-00065b84435c")
static const IID kIID_ITfCandidateListUIElement =
    { 0xea1ea138, 0x19df, 0x11d7, { 0xa6, 0xd2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c } };

#ifndef TF_CLUIE_DOCUMENTMGR   /* SDK msctf.h 값 그대로 */
#define TF_CLUIE_DOCUMENTMGR 0x1
#define TF_CLUIE_COUNT       0x2
#define TF_CLUIE_SELECTION   0x4
#define TF_CLUIE_STRING      0x8
#define TF_CLUIE_PAGEINDEX   0x10
#define TF_CLUIE_CURRENTPAGE 0x20
#endif

typedef struct ITfCandidateListUIElementJ ITfCandidateListUIElementJ;
typedef struct {   // ★상속 vtbl 순서 고정: IUnknown 3 + ITfUIElement 4 + 자기 8 (SDK 원문과 1:1)
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ITfCandidateListUIElementJ*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(ITfCandidateListUIElementJ*);
    ULONG   (STDMETHODCALLTYPE *Release)(ITfCandidateListUIElementJ*);
    HRESULT (STDMETHODCALLTYPE *GetDescription)(ITfCandidateListUIElementJ*, BSTR*);
    HRESULT (STDMETHODCALLTYPE *GetGUID)(ITfCandidateListUIElementJ*, GUID*);
    HRESULT (STDMETHODCALLTYPE *Show)(ITfCandidateListUIElementJ*, BOOL);
    HRESULT (STDMETHODCALLTYPE *IsShown)(ITfCandidateListUIElementJ*, BOOL*);
    HRESULT (STDMETHODCALLTYPE *GetUpdatedFlags)(ITfCandidateListUIElementJ*, DWORD*);
    HRESULT (STDMETHODCALLTYPE *GetDocumentMgr)(ITfCandidateListUIElementJ*, ITfDocumentMgr**);
    HRESULT (STDMETHODCALLTYPE *GetCount)(ITfCandidateListUIElementJ*, UINT*);
    HRESULT (STDMETHODCALLTYPE *GetSelection)(ITfCandidateListUIElementJ*, UINT*);
    HRESULT (STDMETHODCALLTYPE *GetString)(ITfCandidateListUIElementJ*, UINT, BSTR*);
    HRESULT (STDMETHODCALLTYPE *GetPageIndex)(ITfCandidateListUIElementJ*, UINT*, UINT, UINT*);
    HRESULT (STDMETHODCALLTYPE *SetPageIndex)(ITfCandidateListUIElementJ*, UINT*, UINT);
    HRESULT (STDMETHODCALLTYPE *GetCurrentPage)(ITfCandidateListUIElementJ*, UINT*);
} ITfCandidateListUIElementJVtbl;
struct ITfCandidateListUIElementJ { const ITfCandidateListUIElementJVtbl *lpVtbl; };

// 요소 식별 GUID (우리 것 — GetGUID 로 호스트에 알려주는 값)
static const GUID GUID_JamoCandElement = { 0x4b7e19a3, 0x62d0, 0x4c11, { 0x8f, 0x02, 0x33, 0x9a, 0x51, 0x0e, 0x27, 0x61 } };
static const GUID GUID_JamoChipElement = { 0x4b7e19a4, 0x62d0, 0x4c11, { 0x8f, 0x02, 0x33, 0x9a, 0x51, 0x0e, 0x27, 0x61 } };
static const GUID GUID_JamoCodeElement = { 0x4b7e19a5, 0x62d0, 0x4c11, { 0x8f, 0x02, 0x33, 0x9a, 0x51, 0x0e, 0x27, 0x61 } };

// ── 모듈 상태 (TIP 은 스레드당 1개 — candidate_ui 와 같은 전역 규율) ─────────────────
static JamotongTextService *g_uio = NULL;
static DWORD g_candId, g_chipId, g_codeId;
static BOOL  g_candBegan, g_chipBegan, g_codeBegan;
static DWORD g_candLastFlags = TF_CLUIE_DOCUMENTMGR|TF_CLUIE_COUNT|TF_CLUIE_SELECTION|
                               TF_CLUIE_STRING|TF_CLUIE_PAGEINDEX|TF_CLUIE_CURRENTPAGE;

static BOOL Enabled(void) {
    if (!g_uio || !g_uio->uiElemMgr) return FALSE;
    BOOL on;
    EnterCriticalSection(&g_configLock);
    on = g_uio->config.options.useUIElements;
    LeaveCriticalSection(&g_configLock);
    return on;
}

// ── 후보 목록 요소 (ITfUIElement + ITfCandidateListUIElement, 정적 싱글턴) ─────────────
static HRESULT STDMETHODCALLTYPE CE_QI(ITfCandidateListUIElementJ *me, REFIID riid, void **ppv) {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfUIElement) ||
        IsEqualIID(riid, &kIID_ITfCandidateListUIElement)) { *ppv = me; return S_OK; }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE CE_AddRef(ITfCandidateListUIElementJ *me) { (void)me; return 2; }   // 정적 수명
static ULONG STDMETHODCALLTYPE CE_Release(ITfCandidateListUIElementJ *me) { (void)me; return 1; }
static HRESULT STDMETHODCALLTYPE CE_GetDescription(ITfCandidateListUIElementJ *me, BSTR *p) {
    (void)me; if (!p) return E_INVALIDARG;
    *p = SysAllocString(L"Jamotong hanja candidates");
    return *p ? S_OK : E_OUTOFMEMORY;
}
static HRESULT STDMETHODCALLTYPE CE_GetGUID(ITfCandidateListUIElementJ *me, GUID *p) {
    (void)me; if (!p) return E_INVALIDARG; *p = GUID_JamoCandElement; return S_OK;
}
static HRESULT STDMETHODCALLTYPE CE_Show(ITfCandidateListUIElementJ *me, BOOL bShow) {
    (void)me; CandidateUI_ElemHostShow(bShow); return S_OK;   // 호스트가 우리 창 표시를 제어
}
static HRESULT STDMETHODCALLTYPE CE_IsShown(ITfCandidateListUIElementJ *me, BOOL *p) {
    (void)me; if (!p) return E_INVALIDARG; *p = CandidateUI_ElemIsShown(); return S_OK;
}
static HRESULT STDMETHODCALLTYPE CE_GetUpdatedFlags(ITfCandidateListUIElementJ *me, DWORD *p) {
    (void)me; if (!p) return E_INVALIDARG; *p = g_candLastFlags; return S_OK;
}
static HRESULT STDMETHODCALLTYPE CE_GetDocumentMgr(ITfCandidateListUIElementJ *me, ITfDocumentMgr **pp) {
    (void)me; if (!pp) return E_INVALIDARG; *pp = NULL;
    if (g_uio && g_uio->threadMgr) return g_uio->threadMgr->lpVtbl->GetFocus(g_uio->threadMgr, pp);
    return E_FAIL;
}
static HRESULT STDMETHODCALLTYPE CE_GetCount(ITfCandidateListUIElementJ *me, UINT *p) {
    (void)me; if (!p) return E_INVALIDARG; *p = (UINT)CandidateUI_ElemCount(); return S_OK;
}
static HRESULT STDMETHODCALLTYPE CE_GetSelection(ITfCandidateListUIElementJ *me, UINT *p) {
    (void)me; if (!p) return E_INVALIDARG;
    int s = CandidateUI_ElemSelection();
    if (s < 0) return S_FALSE;   // 선택 없음 규약 (uiless-mode 문서)
    *p = (UINT)s; return S_OK;
}
static HRESULT STDMETHODCALLTYPE CE_GetString(ITfCandidateListUIElementJ *me, UINT idx, BSTR *p) {
    (void)me; if (!p) return E_INVALIDARG;
    const wchar_t *s = CandidateUI_ElemString((int)idx);
    if (!s) { *p = NULL; return E_INVALIDARG; }
    *p = SysAllocString(s);
    return *p ? S_OK : E_OUTOFMEMORY;
}
static HRESULT STDMETHODCALLTYPE CE_GetPageIndex(ITfCandidateListUIElementJ *me, UINT *pIndex, UINT uSize, UINT *puPageCnt) {
    (void)me; if (!puPageCnt) return E_INVALIDARG;
    int per = CandidateUI_ElemPerPage(); if (per <= 0) per = 9;
    int count = CandidateUI_ElemCount();
    UINT pages = (UINT)((count + per - 1) / per); if (pages == 0) pages = 1;
    *puPageCnt = pages;
    if (pIndex) {
        for (UINT i = 0; i < uSize && i < pages; i++) pIndex[i] = i * (UINT)per;   // 각 페이지의 시작 인덱스
        if (uSize < pages) return S_FALSE;   // 배열이 모자람 — 호스트가 다시 요청
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE CE_SetPageIndex(ITfCandidateListUIElementJ *me, UINT *pIndex, UINT uPageCnt) {
    (void)me; (void)pIndex; (void)uPageCnt;
    return E_NOTIMPL;   // 페이지 크기는 고정(균등 9) — 재배치 미지원을 정직하게 알린다
}
static HRESULT STDMETHODCALLTYPE CE_GetCurrentPage(ITfCandidateListUIElementJ *me, UINT *p) {
    (void)me; if (!p) return E_INVALIDARG; *p = (UINT)CandidateUI_ElemPage(); return S_OK;
}
static const ITfCandidateListUIElementJVtbl g_CandElemVtbl = {
    CE_QI, CE_AddRef, CE_Release,
    CE_GetDescription, CE_GetGUID, CE_Show, CE_IsShown,
    CE_GetUpdatedFlags, CE_GetDocumentMgr, CE_GetCount, CE_GetSelection,
    CE_GetString, CE_GetPageIndex, CE_SetPageIndex, CE_GetCurrentPage
};
static ITfCandidateListUIElementJ g_CandElem = { &g_CandElemVtbl };

// ── 커스텀 요소 (칩·코드입력): 최소 ITfUIElement ────────────────────────────────────
typedef struct { ITfUIElementVtbl *lpVtbl; const GUID *guid; const wchar_t *desc; BOOL shown; } SimpleElem;
static HRESULT STDMETHODCALLTYPE SE_QI(ITfUIElement *me, REFIID riid, void **ppv) {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfUIElement)) { *ppv = me; return S_OK; }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE SE_AddRef(ITfUIElement *me) { (void)me; return 2; }
static ULONG STDMETHODCALLTYPE SE_Release(ITfUIElement *me) { (void)me; return 1; }
static HRESULT STDMETHODCALLTYPE SE_GetDescription(ITfUIElement *me, BSTR *p) {
    if (!p) return E_INVALIDARG;
    *p = SysAllocString(((SimpleElem*)me)->desc);
    return *p ? S_OK : E_OUTOFMEMORY;
}
static HRESULT STDMETHODCALLTYPE SE_GetGUID(ITfUIElement *me, GUID *p) {
    if (!p) return E_INVALIDARG;
    *p = *((SimpleElem*)me)->guid;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE SE_Show(ITfUIElement *me, BOOL bShow) { ((SimpleElem*)me)->shown = bShow; return S_OK; }
static HRESULT STDMETHODCALLTYPE SE_IsShown(ITfUIElement *me, BOOL *p) {
    if (!p) return E_INVALIDARG;
    *p = ((SimpleElem*)me)->shown;
    return S_OK;
}
static ITfUIElementVtbl g_SimpleVtbl = { SE_QI, SE_AddRef, SE_Release, SE_GetDescription, SE_GetGUID, SE_Show, SE_IsShown };
static SimpleElem g_ChipElem = { &g_SimpleVtbl, &GUID_JamoChipElement, L"Jamotong composition preview", TRUE };
static SimpleElem g_CodeElem = { &g_SimpleVtbl, &GUID_JamoCodeElement, L"Jamotong codepoint input", TRUE };

// ── 글루 ─────────────────────────────────────────────────────────────────────
void UiElem_Attach(JamotongTextService *obj) {
    g_uio = obj;
    obj->uiElemMgr = NULL;
    g_candBegan = g_chipBegan = g_codeBegan = FALSE;
    if (obj->threadMgr)
        obj->threadMgr->lpVtbl->QueryInterface(obj->threadMgr, &IID_ITfUIElementMgr, (void**)&obj->uiElemMgr);
}

void UiElem_Detach(JamotongTextService *obj) {
    if (g_candBegan || g_chipBegan || g_codeBegan) {   // 방어: 열린 요소 정리
        UiElem_EndCandidate(); UiElem_EndChip(); UiElem_EndCode();
    }
    if (obj->uiElemMgr) { obj->uiElemMgr->lpVtbl->Release(obj->uiElemMgr); obj->uiElemMgr = NULL; }
    if (g_uio == obj) g_uio = NULL;
}

static BOOL BeginOne(ITfUIElement *elem, DWORD *idOut, BOOL *beganOut) {
    if (!Enabled()) { *beganOut = FALSE; return TRUE; }   // 킬스위치/mgr 없음 → 기존 동작
    BOOL show = TRUE;
    HRESULT hr = g_uio->uiElemMgr->lpVtbl->BeginUIElement(g_uio->uiElemMgr, elem, &show, idOut);
    if (FAILED(hr)) { *beganOut = FALSE; return TRUE; }   // 실패 = 관여 안 함 → 우리 창
    *beganOut = TRUE;
    return show;   // FALSE = 호스트가 그린다/숨긴다
}
static void EndOne(DWORD id, BOOL *began) {
    if (*began && g_uio && g_uio->uiElemMgr)
        g_uio->uiElemMgr->lpVtbl->EndUIElement(g_uio->uiElemMgr, id);
    *began = FALSE;
}

BOOL UiElem_BeginCandidate(void) {
    g_candLastFlags = TF_CLUIE_DOCUMENTMGR|TF_CLUIE_COUNT|TF_CLUIE_SELECTION|
                      TF_CLUIE_STRING|TF_CLUIE_PAGEINDEX|TF_CLUIE_CURRENTPAGE;   // 첫 Update = 전체
    return BeginOne((ITfUIElement*)&g_CandElem, &g_candId, &g_candBegan);
}
void UiElem_UpdateCandidate(DWORD tfCluieFlags) {
    if (!g_candBegan || !g_uio || !g_uio->uiElemMgr) return;
    g_candLastFlags = tfCluieFlags;
    g_uio->uiElemMgr->lpVtbl->UpdateUIElement(g_uio->uiElemMgr, g_candId);
}
void UiElem_EndCandidate(void) { EndOne(g_candId, &g_candBegan); }

BOOL UiElem_BeginChip(void)  {
    static BOOL lastAnswer = TRUE;
    if (g_chipBegan) return lastAnswer;   // 칩은 갱신마다 Show 가 다시 불린다 — 세션당 1회만 Begin
    lastAnswer = BeginOne((ITfUIElement*)&g_ChipElem, &g_chipId, &g_chipBegan);
    return lastAnswer;
}
void UiElem_EndChip(void)    { EndOne(g_chipId, &g_chipBegan); }
BOOL UiElem_BeginCode(void)  {
    static BOOL lastAnswer = TRUE;
    if (g_codeBegan) return lastAnswer;
    lastAnswer = BeginOne((ITfUIElement*)&g_CodeElem, &g_codeId, &g_codeBegan);
    return lastAnswer;
}
void UiElem_EndCode(void)    { EndOne(g_codeId, &g_codeBegan); }
