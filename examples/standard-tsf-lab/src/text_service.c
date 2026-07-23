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
#define FROM_TEXT_EDIT(p) ((LabTextService*)((char*)(p) - offsetof(LabTextService, text_edit_sink)))

#ifdef LAB_ALWAYS_TRACE
/* MinGW declares this property but does not provide it in libuuid. */
static const GUID kPropComposing =
    { 0xe12ac060, 0xaf15, 0x11d2,
      { 0xaf, 0xc5, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 } };
static const GUID kPropLangId =
    { 0x3280ce20, 0x8032, 0x11d2,
      { 0xb6, 0x03, 0x00, 0xc0, 0x4f, 0x93, 0xd0, 0x15 } };
static const GUID kPropReading =
    { 0x5463f7c0, 0x8e31, 0x11d3,
      { 0xa9, 0xf3, 0x00, 0x80, 0x5f, 0x8e, 0xff, 0xf8 } };
#endif

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
    if (!c) return svc->fallback_open;          /* compartment 자체를 못 얻는 호스트 */
    VARIANT v; VariantInit(&v);
    bool open = svc->fallback_open;
    HRESULT hr = ITfCompartment_GetValue(c, &v);
    /* VT_EMPTY = 아직 아무도 쓴 적 없음. 실패도 아니고 false도 아니다 —
       이걸 false로 읽어버리면 우리가 켠 상태를 스스로 지운다. */
    if (SUCCEEDED(hr) && v.vt == VT_I4) open = (v.lVal != 0);
    else if (SUCCEEDED(hr) && v.vt == VT_EMPTY) open = svc->fallback_open;
    VariantClear(&v);
    ITfCompartment_Release(c);
    return open;
}
bool Lab_SetKeyboardOpen(LabTextService *svc, bool open) {
    /* 자체 상태를 먼저 갱신한다. compartment 쓰기가 실패해도 토글은 성립해야 한다 —
       PuTTY에서 "한/영이 한 번만 먹는" 증상이 여기서 나왔다. */
    svc->fallback_open = open;
    ITfCompartment *c = GetOpenCloseCompartment(svc);
    if (!c) return true;
    VARIANT v; VariantInit(&v);
    v.vt = VT_I4; v.lVal = open ? 1 : 0;
    HRESULT hr = ITfCompartment_SetValue(c, svc->client_id, &v);
    VariantClear(&v);
    ITfCompartment_Release(c);
    if (SUCCEEDED(hr)) svc->compartment_usable = true;
    return true;
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
static STDMETHODIMP KS_OnSetFocus(ITfKeyEventSink *me, BOOL granted) {
    (void)me;
    Lab_TraceEvent("key_sink.focus", NULL, S_OK, granted ? 1 : 0);
    return S_OK;
}

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
    if (a == LAB_KEY_FINALIZE_AND_PASS) {
        /* 여기서 확정하고 원래 키는 앱으로 (RFC-0009 §8.2).
           OnKeyDown이 또 불려도 composition이 이미 없으므로 중복 확정되지 않는다. */
        LabContextState *st = Lab_FindContext(svc, ctx);
        if (st) Lab_FinalizeComposition(svc, st, LAB_FINALIZE);
        *eaten = FALSE;
        return S_OK;
    }
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
    case LAB_KEY_TOGGLE: {
        LabContextState *st = Lab_FindContext(svc, ctx);
        if (st) Lab_FinalizeComposition(svc, st, LAB_FINALIZE);   /* 남은 조합을 흘리지 않는다 */
        bool was = Lab_IsKeyboardOpen(svc);
        Lab_SetKeyboardOpen(svc, !was);
        Lab_TraceEvent("keyboard.toggle", st, S_OK,
                       (was ? 1 : 0) | (svc->compartment_usable ? 2 : 0));
        *eaten = TRUE;
        return S_OK;
    }
    case LAB_KEY_HANDLE_HANGUL: {
        LabContextState *st = Lab_EnsureContext(svc, ctx);
        if (!st) return S_OK;
        LabKeySnapshot k = CaptureKey(wparam, lparam, st->generation);
        HangulStep step;
        if (!Hangul2_Step(&st->hangul, k.virtual_key, k.shift, &step)) return S_OK;
        /* 편집이 성공한 경우에만 FSM 상태가 갱신된다 (RFC-0009 §5.2) */
        LabSessionResult res;
        HRESULT hr = Lab_ApplyStep(svc, st, &step, &res);
        if (FAILED(hr)) {
            Lab_TraceEvent("hangul_step.failed", st, hr,
                           ((LONG)step.committed_len << 16) | step.preedit_len);
            /* 이 호스트에서는 표준 composition이 성립하지 않는다.
               조합 상태를 남겨두면 다음 키가 그 위에 쌓여 낱자가 흩어진다
               (AkelPad에서 "ㄱㅏㄴㅏ" 로 나오던 증상).
               조합을 접고 이 키는 앱에 넘긴다 — 실패를 감추지 않는다. */
            Lab_ForgetComposition(st);
            Hangul2_Reset(&st->hangul);
            *eaten = FALSE;
            return S_OK;
        }
        *eaten = TRUE;
        return S_OK;
    }
    case LAB_KEY_HANDLE_BACKSPACE: {
        LabContextState *st = Lab_FindContext(svc, ctx);
        if (!st) return S_OK;
        HangulStep step;
        if (!Hangul2_Backspace(&st->hangul, &step)) return S_OK;
        if (Hangul2_IsEmpty(&step.next)) {
            /* 마지막 한 단계를 지우면 조합 자체가 사라진다 → 취소로 끝낸다 */
            Lab_FinalizeComposition(svc, st, LAB_CANCEL);
        } else {
            Lab_ApplyStep(svc, st, &step, NULL);
        }
        *eaten = TRUE;
        return S_OK;
    }
    case LAB_KEY_FINALIZE_AND_PASS: {
        /* 조합을 확정하고 원래 키는 앱으로 넘긴다. replay하지 않는다 (RFC-0009 §8.2, D7).
           OnTestKeyDown에서 이미 확정됐을 수 있으므로 두 번 불려도 안전해야 한다 —
           composition이 없으면 Lab_FinalizeComposition이 바로 S_OK로 돌아온다. */
        LabContextState *st = Lab_FindContext(svc, ctx);
        if (st) Lab_FinalizeComposition(svc, st, LAB_FINALIZE);
        return S_OK;      /* eaten=FALSE 유지 */
    }
    default:
        return S_OK;   /* PASS: 앱이 원래 키를 받는다 */
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

/* ── 텍스트 편집 싱크: 호스트가 edit session 뒤에 무엇을 했는지 순서만 관측 ── */
static STDMETHODIMP TE_QI(ITfTextEditSink *me, REFIID riid, void **ppv) {
    return SVC_QI((ITfTextInputProcessor*)FROM_TEXT_EDIT(me), riid, ppv);
}
static STDMETHODIMP_(ULONG) TE_AddRef(ITfTextEditSink *me) {
    return SVC_AddRef((ITfTextInputProcessor*)FROM_TEXT_EDIT(me));
}
static STDMETHODIMP_(ULONG) TE_Release(ITfTextEditSink *me) {
    return SVC_Release((ITfTextInputProcessor*)FROM_TEXT_EDIT(me));
}

#ifdef LAB_ALWAYS_TRACE
/* Query only whether a changed range exists. Never inspect range text or property values. */
static HRESULT EditRecordHasUpdate(ITfEditRecord *record, DWORD flags,
                                   const GUID *property, BOOL *changed) {
    *changed = FALSE;
    if (!record) return E_POINTER;

    const GUID *properties[1];
    const GUID **property_array = NULL;
    ULONG property_count = 0;
    if (property) {
        properties[0] = property;
        property_array = properties;
        property_count = 1;
    }

    IEnumTfRanges *ranges = NULL;
    HRESULT hr = ITfEditRecord_GetTextAndPropertyUpdates(
            record, flags, property_array, property_count, &ranges);
    if (FAILED(hr)) return hr;

    ITfRange *range = NULL;
    ULONG fetched = 0;
    HRESULT next_hr = IEnumTfRanges_Next(ranges, 1, &range, &fetched);
    if (range) ITfRange_Release(range);
    IEnumTfRanges_Release(ranges);
    if (FAILED(next_hr)) return next_hr;
    *changed = fetched != 0;
    return S_OK;
}

static void TraceEditRecordUpdate(ITfEditRecord *record, LabContextState *st,
                                  const char *event, DWORD flags, const GUID *property) {
    BOOL changed = FALSE;
    HRESULT hr = EditRecordHasUpdate(record, flags, property, &changed);
    Lab_TraceEvent(event, st, hr, changed ? 1 : 0);
}
#endif

static STDMETHODIMP TE_OnEndEdit(ITfTextEditSink *me, ITfContext *ctx,
                                 TfEditCookie read_cookie, ITfEditRecord *record) {
    (void)read_cookie;
    LabTextService *svc = FROM_TEXT_EDIT(me);
    LabContextState *st = Lab_FindContext(svc, ctx);
    BOOL selection_changed = FALSE;
    HRESULT hr = record ? ITfEditRecord_GetSelectionStatus(record, &selection_changed) : E_POINTER;
    Lab_TraceEvent("text_edit.end", st, hr, selection_changed ? 1 : 0);
#ifdef LAB_ALWAYS_TRACE
    BOOL in_write_session = FALSE;
    HRESULT write_hr = ITfContext_InWriteSession(ctx, svc->client_id, &in_write_session);
    Lab_TraceEvent("text_edit.in_write_session", st, write_hr,
                   in_write_session ? 1 : 0);
    TraceEditRecordUpdate(record, st, "text_edit.changed_text",
                          TF_GTP_INCL_TEXT, NULL);
    TraceEditRecordUpdate(record, st, "text_edit.changed_composing",
                          0, &kPropComposing);
    TraceEditRecordUpdate(record, st, "text_edit.changed_attribute",
                          0, &GUID_PROP_ATTRIBUTE);
    TraceEditRecordUpdate(record, st, "text_edit.changed_langid",
                          0, &kPropLangId);
    TraceEditRecordUpdate(record, st, "text_edit.changed_reading",
                          0, &kPropReading);
#endif
    return S_OK;
}
static const ITfTextEditSinkVtbl g_text_edit_vtbl = {
    TE_QI, TE_AddRef, TE_Release, TE_OnEndEdit
};

void Lab_InitTextEditSink(LabTextService *svc) {
    svc->text_edit_sink.lpVtbl = (ITfTextEditSinkVtbl*)&g_text_edit_vtbl;
}

/* ── 스레드 매니저 싱크: 컨텍스트 수명 추적 (RFC-0009 §9.4) ── */
static STDMETHODIMP TS_QI(ITfThreadMgrEventSink *me, REFIID riid, void **ppv) {
    return SVC_QI((ITfTextInputProcessor*)FROM_THREAD(me), riid, ppv);
}
static STDMETHODIMP_(ULONG) TS_AddRef(ITfThreadMgrEventSink *me)  { return SVC_AddRef((ITfTextInputProcessor*)FROM_THREAD(me)); }
static STDMETHODIMP_(ULONG) TS_Release(ITfThreadMgrEventSink *me) { return SVC_Release((ITfTextInputProcessor*)FROM_THREAD(me)); }
static STDMETHODIMP TS_OnInitDocumentMgr(ITfThreadMgrEventSink *me, ITfDocumentMgr *d) { (void)me;(void)d; return S_OK; }
static STDMETHODIMP TS_OnUninitDocumentMgr(ITfThreadMgrEventSink *me, ITfDocumentMgr *d) { (void)me;(void)d; return S_OK; }
static STDMETHODIMP TS_OnSetFocus(ITfThreadMgrEventSink *me, ITfDocumentMgr *in, ITfDocumentMgr *out) {
    (void)me;(void)in;(void)out;
    Lab_TraceEvent("thread_manager.focus", NULL, S_OK, 0);
    return S_OK;
}
static STDMETHODIMP TS_OnPushContext(ITfThreadMgrEventSink *me, ITfContext *ctx) {
    LabContextState *st = Lab_EnsureContext(FROM_THREAD(me), ctx);
    Lab_TraceEvent("context.push", st, st ? S_OK : E_OUTOFMEMORY, 0);
    return S_OK;
}
static STDMETHODIMP TS_OnPopContext(ITfThreadMgrEventSink *me, ITfContext *ctx) {
    /* 컨텍스트가 사라지면 그 상태도 버린다. generation이 바뀌므로 stale callback은 무효가 된다. */
    LabTextService *svc = FROM_THREAD(me);
    LabContextState *st = Lab_FindContext(svc, ctx);
    Lab_TraceEvent("context.pop", st, S_OK, 0);
    Lab_RemoveContext(svc, ctx);
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
    Lab_TraceEvent("service.activate", NULL, S_OK, 0);
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
    for (LabContextState *p = s->contexts; p; p = p->next)
        if (p->composition) Lab_FinalizeComposition(s, p, LAB_FINALIZE);
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
    else if (IsEqualIID(riid, &IID_ITfCompositionSink))
        *ppv = &s->composition_sink;
    else if (IsEqualIID(riid, &IID_ITfTextEditSink))
        *ppv = &s->text_edit_sink;
    else if (IsEqualIID(riid, &IID_ITfDisplayAttributeProvider_Lab))
        *ppv = &s->attr_provider;
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
    Lab_InitCompositionSink(s);
    Lab_InitTextEditSink(s);
    Lab_InitDisplayAttribute(s);
    s->ref = 1;
    Lab_DllAddRef();
    return s;
}
