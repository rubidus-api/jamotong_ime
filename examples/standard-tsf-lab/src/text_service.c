/* text_service.c — TIP 본체, 키 싱크, 한/영 compartment (RFC-0009 Phase 1)
 *
 * Phase 1 범위: 활성화 · 키 싱크 · 한/영 상태 · 영문 pass-through.
 * 한글 composition은 Phase 2다. 지금은 분류 결과를 eaten으로만 반영한다.
 */
#include "lab_tip.h"
#include <stdlib.h>
#include <stddef.h>

#define FROM_KEY(p)    ((LabTextService*)((char*)(p) - offsetof(LabTextService, key_sink)))
#define FROM_THREAD(p) ((LabTextService*)((char*)(p) - offsetof(LabTextService, thread_sink)))

static STDMETHODIMP SVC_QI(ITfTextInputProcessor *me, REFIID riid, void **ppv);
static STDMETHODIMP_(ULONG) SVC_AddRef(ITfTextInputProcessor *me) {
    return InterlockedIncrement(&((LabTextService*)me)->ref);
}
static STDMETHODIMP_(ULONG) SVC_Release(ITfTextInputProcessor *me) {
    LabTextService *s = (LabTextService*)me;
    LONG n = InterlockedDecrement(&s->ref);
    if (n == 0) { Lab_ReleaseContexts(s); free(s); Lab_DllRelease(); }
    return n;
}

/* ── 한/영 상태: GUID_COMPARTMENT_KEYBOARD_OPENCLOSE (RFC-0009 D4) ── */
static ITfCompartment *GetOpenCloseCompartment(LabTextService *svc) {
    ITfCompartmentMgr *mgr = NULL;
    ITfCompartment *comp = NULL;
    if (!svc->thread_mgr) return NULL;
    if (FAILED(ITfThreadMgr_QueryInterface(svc->thread_mgr, &IID_ITfCompartmentMgr, (void**)&mgr)))
        return NULL;
    ITfCompartmentMgr_GetCompartment(mgr, &GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &comp);
    ITfCompartmentMgr_Release(mgr);
    return comp;
}
bool Lab_IsKeyboardOpen(LabTextService *svc) {
    ITfCompartment *c = GetOpenCloseCompartment(svc);
    if (!c) return false;
    VARIANT v; VariantInit(&v);
    bool open = false;
    if (SUCCEEDED(ITfCompartment_GetValue(c, &v)) && v.vt == VT_I4) open = (v.lVal != 0);
    VariantClear(&v);
    ITfCompartment_Release(c);
    return open;
}
bool Lab_SetKeyboardOpen(LabTextService *svc, bool open) {
    ITfCompartment *c = GetOpenCloseCompartment(svc);
    if (!c) return false;
    VARIANT v; VariantInit(&v);
    v.vt = VT_I4; v.lVal = open ? 1 : 0;
    HRESULT hr = ITfCompartment_SetValue(c, svc->client_id, &v);
    VariantClear(&v);
    ITfCompartment_Release(c);
    return SUCCEEDED(hr);
}
/* 암호 입력란 등에서 IME를 끄는 컨텍스트 플래그 */
bool Lab_IsKeyboardDisabled(LabTextService *svc, ITfContext *ctx) {
    if (!ctx) return true;                       /* empty context = 입력 대상 없음 */
    ITfCompartmentMgr *mgr = NULL;
    ITfCompartment *comp = NULL;
    bool disabled = false;
    if (SUCCEEDED(ITfContext_QueryInterface(ctx, &IID_ITfCompartmentMgr, (void**)&mgr))) {
        if (SUCCEEDED(ITfCompartmentMgr_GetCompartment(mgr, &GUID_COMPARTMENT_KEYBOARD_DISABLED, &comp))) {
            VARIANT v; VariantInit(&v);
            if (SUCCEEDED(ITfCompartment_GetValue(comp, &v)) && v.vt == VT_I4 && v.lVal != 0)
                disabled = true;
            VariantClear(&v);
            ITfCompartment_Release(comp);
        }
        ITfCompartmentMgr_Release(mgr);
    }
    (void)svc;
    return disabled;
}

/* ── 키 snapshot (RFC-0009 §8.1) ── */
static LabKeySnapshot CaptureKey(WPARAM wparam, LPARAM lparam, uint32_t generation) {
    LabKeySnapshot k;
    memset(&k, 0, sizeof k);
    k.virtual_key = (UINT)wparam;
    k.scan_code   = (UINT)((lparam >> 16) & 0xFF);
    k.extended    = ((lparam >> 24) & 1) != 0;
    k.repeat      = ((lparam >> 30) & 1) != 0;
    k.shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
    k.ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    k.alt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;
    k.win   = ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) != 0;
    k.context_generation = generation;
    return k;
}

/* ── 키 싱크 ── */
static STDMETHODIMP KS_QI(ITfKeyEventSink *me, REFIID riid, void **ppv) {
    return SVC_QI((ITfTextInputProcessor*)FROM_KEY(me), riid, ppv);
}
static STDMETHODIMP_(ULONG) KS_AddRef(ITfKeyEventSink *me)  { return SVC_AddRef((ITfTextInputProcessor*)FROM_KEY(me)); }
static STDMETHODIMP_(ULONG) KS_Release(ITfKeyEventSink *me) { return SVC_Release((ITfTextInputProcessor*)FROM_KEY(me)); }
static STDMETHODIMP KS_OnSetFocus(ITfKeyEventSink *me, BOOL granted) { (void)me;(void)granted; return S_OK; }

/* OnTestKeyDown과 OnKeyDown이 같은 순수 함수를 쓴다 (RFC-0009 §8.1) */
static LabKeyAction ClassifyForContext(LabTextService *svc, ITfContext *ctx,
                                       WPARAM wparam, LPARAM lparam) {
    LabContextState *st = Lab_FindContext(svc, ctx);
    LabKeySnapshot key = CaptureKey(wparam, lparam, st ? st->generation : 0);
    bool composing = st && !Hangul2_IsEmpty(&st->hangul);
    return Lab_ClassifyKey(&key, Lab_IsKeyboardOpen(svc), composing,
                           Lab_IsKeyboardDisabled(svc, ctx));
}

static STDMETHODIMP KS_OnTestKeyDown(ITfKeyEventSink *me, ITfContext *ctx,
                                     WPARAM wparam, LPARAM lparam, BOOL *eaten) {
    LabTextService *svc = FROM_KEY(me);
    LabKeyAction a = ClassifyForContext(svc, ctx, wparam, lparam);
    /* FINALIZE_AND_PASS는 eaten=FALSE — 앱이 원래 키를 받는다 (RFC-0009 §8.2, D7).
       Phase 1에는 조합이 없으므로 실제 finalize는 Phase 2에서 붙인다. */
    *eaten = (a == LAB_KEY_HANDLE_HANGUL || a == LAB_KEY_HANDLE_BACKSPACE ||
              a == LAB_KEY_TOGGLE);
    return S_OK;
}
static STDMETHODIMP KS_OnKeyDown(ITfKeyEventSink *me, ITfContext *ctx,
                                 WPARAM wparam, LPARAM lparam, BOOL *eaten) {
    LabTextService *svc = FROM_KEY(me);
    LabKeyAction a = ClassifyForContext(svc, ctx, wparam, lparam);
    *eaten = FALSE;
    switch (a) {
    case LAB_KEY_TOGGLE:
        Lab_SetKeyboardOpen(svc, !Lab_IsKeyboardOpen(svc));
        *eaten = TRUE;
        return S_OK;
    case LAB_KEY_HANDLE_HANGUL:
    case LAB_KEY_HANDLE_BACKSPACE:
        /* Phase 2에서 composition을 붙인다. 지금은 먹기만 하고 아무것도 넣지 않는다 —
           "먹었는데 아무 일도 없다"가 Phase 1의 정상 동작이다. */
        Lab_EnsureContext(svc, ctx);
        *eaten = TRUE;
        return S_OK;
    default:
        return S_OK;   /* PASS / FINALIZE_AND_PASS: 앱이 원래 키를 받는다 */
    }
}
static STDMETHODIMP KS_OnTestKeyUp(ITfKeyEventSink *me, ITfContext *c, WPARAM w, LPARAM l, BOOL *e) {
    (void)me;(void)c;(void)w;(void)l; *e = FALSE; return S_OK;
}
static STDMETHODIMP KS_OnKeyUp(ITfKeyEventSink *me, ITfContext *c, WPARAM w, LPARAM l, BOOL *e) {
    (void)me;(void)c;(void)w;(void)l; *e = FALSE; return S_OK;
}
static STDMETHODIMP KS_OnPreservedKey(ITfKeyEventSink *me, ITfContext *c, REFGUID g, BOOL *e) {
    (void)c;(void)g;
    LabTextService *svc = FROM_KEY(me);
    Lab_SetKeyboardOpen(svc, !Lab_IsKeyboardOpen(svc));
    *e = TRUE;
    return S_OK;
}
/* ★ msctf.h 선언 순서: OnSetFocus, OnTestKeyDown, OnTestKeyUp, OnKeyDown, OnKeyUp, OnPreservedKey.
   OnTestKeyUp이 OnKeyDown보다 앞이다 — 순서를 바꾸면 조용히 엉뚱한 함수가 불린다. */
static const ITfKeyEventSinkVtbl g_key_vtbl = {
    KS_QI, KS_AddRef, KS_Release,
    KS_OnSetFocus, KS_OnTestKeyDown, KS_OnTestKeyUp, KS_OnKeyDown, KS_OnKeyUp, KS_OnPreservedKey
};

/* ── 스레드 매니저 싱크: 컨텍스트 수명 추적 (RFC-0009 §9.4) ── */
static STDMETHODIMP TS_QI(ITfThreadMgrEventSink *me, REFIID riid, void **ppv) {
    return SVC_QI((ITfTextInputProcessor*)FROM_THREAD(me), riid, ppv);
}
static STDMETHODIMP_(ULONG) TS_AddRef(ITfThreadMgrEventSink *me)  { return SVC_AddRef((ITfTextInputProcessor*)FROM_THREAD(me)); }
static STDMETHODIMP_(ULONG) TS_Release(ITfThreadMgrEventSink *me) { return SVC_Release((ITfTextInputProcessor*)FROM_THREAD(me)); }
static STDMETHODIMP TS_OnInitDocumentMgr(ITfThreadMgrEventSink *me, ITfDocumentMgr *d) { (void)me;(void)d; return S_OK; }
static STDMETHODIMP TS_OnUninitDocumentMgr(ITfThreadMgrEventSink *me, ITfDocumentMgr *d) { (void)me;(void)d; return S_OK; }
static STDMETHODIMP TS_OnSetFocus(ITfThreadMgrEventSink *me, ITfDocumentMgr *in, ITfDocumentMgr *out) {
    (void)me;(void)in;(void)out; return S_OK;
}
static STDMETHODIMP TS_OnPushContext(ITfThreadMgrEventSink *me, ITfContext *ctx) {
    Lab_EnsureContext(FROM_THREAD(me), ctx);
    return S_OK;
}
static STDMETHODIMP TS_OnPopContext(ITfThreadMgrEventSink *me, ITfContext *ctx) {
    /* 컨텍스트가 사라지면 그 상태도 버린다. generation이 바뀌므로 stale callback은 무효가 된다. */
    LabTextService *svc = FROM_THREAD(me);
    LabContextState **pp = &svc->contexts;
    while (*pp) {
        if ((*pp)->context == ctx) {
            LabContextState *dead = *pp;
            *pp = dead->next;
            if (dead->composition) ITfComposition_Release(dead->composition);
            if (dead->context)     ITfContext_Release(dead->context);
            free(dead);
            return S_OK;
        }
        pp = &(*pp)->next;
    }
    return S_OK;
}
static const ITfThreadMgrEventSinkVtbl g_thread_vtbl = {
    TS_QI, TS_AddRef, TS_Release,
    TS_OnInitDocumentMgr, TS_OnUninitDocumentMgr, TS_OnSetFocus,
    TS_OnPushContext, TS_OnPopContext
};

/* ── TIP 본체 ── */
static STDMETHODIMP SVC_Activate(ITfTextInputProcessor *me, ITfThreadMgr *tm, TfClientId cid) {
    LabTextService *s = (LabTextService*)me;
    s->thread_mgr = tm; s->client_id = cid;
    ITfThreadMgr_AddRef(tm);

    ITfKeystrokeMgr *km = NULL;
    if (SUCCEEDED(ITfThreadMgr_QueryInterface(tm, &IID_ITfKeystrokeMgr, (void**)&km))) {
        ITfKeystrokeMgr_AdviseKeyEventSink(km, cid, &s->key_sink, TRUE);
        /* Shift+Space를 preserved key로 예약 — 앱보다 먼저 받는다 (RFC-0009 D4) */
        TF_PRESERVEDKEY pk = { VK_SPACE, TF_MOD_SHIFT };
        ITfKeystrokeMgr_PreserveKey(km, cid, &GUID_LabProfile, &pk,
                                    LAB_DISPLAY_NAME, (ULONG)wcslen(LAB_DISPLAY_NAME));
        ITfKeystrokeMgr_Release(km);
    }
    ITfSource *src = NULL;
    if (SUCCEEDED(ITfThreadMgr_QueryInterface(tm, &IID_ITfSource, (void**)&src))) {
        ITfSource_AdviseSink(src, &IID_ITfThreadMgrEventSink,
                             (IUnknown*)&s->thread_sink, &s->thread_sink_cookie);
        ITfSource_Release(src);
    }
    /* 켜진 상태로 시작하지 않는다 — 사용자가 한/영으로 켠다 */
    Lab_SetKeyboardOpen(s, false);
    return S_OK;
}
static STDMETHODIMP SVC_Deactivate(ITfTextInputProcessor *me) {
    LabTextService *s = (LabTextService*)me;
    if (!s->thread_mgr) return S_OK;

    ITfSource *src = NULL;
    if (s->thread_sink_cookie &&
        SUCCEEDED(ITfThreadMgr_QueryInterface(s->thread_mgr, &IID_ITfSource, (void**)&src))) {
        ITfSource_UnadviseSink(src, s->thread_sink_cookie);
        ITfSource_Release(src);
        s->thread_sink_cookie = 0;
    }
    ITfKeystrokeMgr *km = NULL;
    if (SUCCEEDED(ITfThreadMgr_QueryInterface(s->thread_mgr, &IID_ITfKeystrokeMgr, (void**)&km))) {
        TF_PRESERVEDKEY pk = { VK_SPACE, TF_MOD_SHIFT };
        ITfKeystrokeMgr_UnpreserveKey(km, &GUID_LabProfile, &pk);
        ITfKeystrokeMgr_UnadviseKeyEventSink(km, s->client_id);
        ITfKeystrokeMgr_Release(km);
    }
    Lab_ReleaseContexts(s);
    ITfThreadMgr_Release(s->thread_mgr);
    s->thread_mgr = NULL;
    return S_OK;
}
static const ITfTextInputProcessorVtbl g_tip_vtbl = {
    SVC_QI, SVC_AddRef, SVC_Release, SVC_Activate, SVC_Deactivate
};

static STDMETHODIMP SVC_QI(ITfTextInputProcessor *me, REFIID riid, void **ppv) {
    LabTextService *s = (LabTextService*)me;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfTextInputProcessor))
        *ppv = &s->tip;
    else if (IsEqualIID(riid, &IID_ITfKeyEventSink))
        *ppv = &s->key_sink;
    else if (IsEqualIID(riid, &IID_ITfThreadMgrEventSink))
        *ppv = &s->thread_sink;
    else { *ppv = NULL; return E_NOINTERFACE; }
    SVC_AddRef(me);
    return S_OK;
}

LabTextService *Lab_CreateService(void) {
    LabTextService *s = (LabTextService*)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->tip.lpVtbl         = (ITfTextInputProcessorVtbl*)&g_tip_vtbl;
    s->key_sink.lpVtbl    = (ITfKeyEventSinkVtbl*)&g_key_vtbl;
    s->thread_sink.lpVtbl = (ITfThreadMgrEventSinkVtbl*)&g_thread_vtbl;
    s->ref = 1;
    Lab_DllAddRef();
    return s;
}
