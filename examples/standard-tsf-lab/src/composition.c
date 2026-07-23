/* composition.c — 실제 ITfComposition 트랜잭션 (RFC-0009 §7)
 *
 * 핵심 규약:
 *  - 한 키 = 한 edit session. 그 안에서 composition 생성·갱신·prefix 확정을 끝낸다.
 *  - 어느 단계든 실패하면 다음 FSM 상태를 publish하지 않는다(§7.2).
 *  - 요청 성공(request_hr)과 세션 성공(session_hr)을 분리한다(§5.3).
 *  - 확정과 취소를 boolean 하나로 합치지 않는다(§7.4).
 */
#include "lab_tip.h"
#include <stdlib.h>
#include <stddef.h>

/* MinGW-w64는 GUID_PROP_READING 심볼을 libuuid에 넣어주지 않는다.
   값은 Microsoft msctf.h와 같다. */
static const GUID kPropReading =
    { 0x5463f7c0, 0x8e31, 0x11d3, { 0xa9, 0xf3, 0x00, 0x80, 0x5f, 0x8e, 0xff, 0xf8 } };

/* StartComposition이 S_OK인데 결과가 NULL인 경우를 실패로 다룬다(§7.1). */
#define LAB_E_COMPOSITION_REJECTED MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x201)

typedef struct LabEditSession {
    ITfEditSession base;
    LONG ref;
    LabTextService *service;
    LabContextState *state;
    const HangulStep *step;          /* LAB_UPDATE/COMMIT_PREFIX일 때 */
    LabCompositionCommand command;
    bool callback_ran;
    HRESULT inner_hr;
} LabEditSession;

static void SetTracePhase(LabContextState *st, LabTracePhase phase) {
    st->trace_phase = phase;
}

/* ── caret을 range 끝에 명시적으로 둔다 (§7.2) ── */
static HRESULT SetSelectionAtEnd(LabEditSession *es, TfEditCookie ec, ITfRange *range) {
    LabContextState *st = es->state;
    SetTracePhase(st, LAB_TRACE_SET_SELECTION);
#ifdef LAB_AKEL_NO_SELECTION_BUILD
    (void)ec;
    (void)range;
    Lab_TraceEvent("selection.skipped", st, S_OK, 0);
    return S_OK;
#else
    ITfRange *clone = NULL;
    HRESULT hr = ITfRange_Clone(range, &clone);
    if (SUCCEEDED(hr)) {
        hr = ITfRange_Collapse(clone, ec, TF_ANCHOR_END);
    }
    if (SUCCEEDED(hr) && clone) {
        TF_SELECTION sel;
        sel.range = clone;
#ifdef LAB_AKEL_AE_NONE_BUILD
        sel.style.ase = TF_AE_NONE;
#else
        sel.style.ase = TF_AE_END;
#endif
        sel.style.fInterimChar = FALSE;
        hr = ITfContext_SetSelection(st->context, ec, 1, &sel);
    }
    if (clone) ITfRange_Release(clone);
    Lab_TraceEvent("selection.set_end", st, hr, 0);
    return hr;
#endif
}

/* ── composition 확보 (§7.1) ── */
static HRESULT EnsureComposition(LabEditSession *es, TfEditCookie ec,
                                 const WCHAR *initial_text, LONG initial_len,
                                 bool *inserted_text, ITfRange **range_out) {
    LabContextState *st = es->state;
    *inserted_text = false;
    *range_out = NULL;
#ifndef LAB_AKEL_INSERT_FIRST_BUILD
    (void)initial_text;
    (void)initial_len;
#endif
    if (st->composition) {
        SetTracePhase(st, LAB_TRACE_GET_RANGE);
        HRESULT existing_hr = ITfComposition_GetRange(st->composition, range_out);
        Lab_TraceEvent("composition.get_range.existing", st, existing_hr, 0);
        return existing_hr;
    }

    ITfInsertAtSelection *insert = NULL;
    ITfRange *insert_range = NULL;
    ITfContextComposition *cc = NULL;
    ITfComposition *created = NULL;          /* 전부 성공한 뒤에만 publish */
    HRESULT hr;

    SetTracePhase(st, LAB_TRACE_INSERT_QUERY);
    hr = ITfContext_QueryInterface(st->context, &IID_ITfInsertAtSelection, (void**)&insert);
    Lab_TraceEvent("insert_at_selection.query_interface", st, hr, 0);
    if (FAILED(hr)) goto done;
#ifdef LAB_AKEL_INSERT_FIRST_BUILD
    if (!initial_text || initial_len <= 0) {
        hr = E_INVALIDARG;
        Lab_TraceEvent("insert_at_selection.insert_first.invalid", st, hr, initial_len);
        goto done;
    }
    SetTracePhase(st, LAB_TRACE_INSERT_TEXT);
    Lab_TraceEvent("insert_at_selection.insert_first.before", st, S_OK, initial_len);
    hr = ITfInsertAtSelection_InsertTextAtSelection(
            insert, ec, TF_IAS_NO_DEFAULT_COMPOSITION,
            initial_text, initial_len, &insert_range);
    Lab_TraceEvent("insert_at_selection.insert_first.after", st, hr, initial_len);
#else
    /* TF_IAS_QUERYONLY: 텍스트를 넣지 않고 삽입 지점 range만 얻는다 */
    hr = ITfInsertAtSelection_InsertTextAtSelection(insert, ec, TF_IAS_QUERYONLY,
                                                    NULL, 0, &insert_range);
    Lab_TraceEvent("insert_at_selection.query_range", st, hr, 0);
#endif
    if (FAILED(hr)) goto done;
    hr = ITfContext_QueryInterface(st->context, &IID_ITfContextComposition, (void**)&cc);
    Lab_TraceEvent("context_composition.query_interface", st, hr, 0);
    if (FAILED(hr)) goto done;
    SetTracePhase(st, LAB_TRACE_START_COMPOSITION);
    Lab_TraceEvent("composition.start.before", st, S_OK, 0);
    hr = ITfContextComposition_StartComposition(cc, ec, insert_range,
                                                &es->service->composition_sink, &created);
    Lab_TraceEvent("composition.start.after", st, hr, created ? 1 : 0);
    if (SUCCEEDED(hr) && created == NULL) {
        Lab_TraceEvent("composition.start.null", st, LAB_E_COMPOSITION_REJECTED, 0);
        hr = LAB_E_COMPOSITION_REJECTED; goto done;
    }
    if (FAILED(hr)) goto done;

    SetTracePhase(st, LAB_TRACE_GET_RANGE);
    hr = ITfComposition_GetRange(created, range_out);
    Lab_TraceEvent("composition.get_range.new", st, hr, 0);
    if (FAILED(hr)) goto done;

    st->composition = created;               /* publish only after the range is usable */
    created = NULL;
    Lab_TraceEvent("composition.publish", st, S_OK, 0);
#ifdef LAB_AKEL_INSERT_FIRST_BUILD
    *inserted_text = true;
#endif

done:
    if (FAILED(hr) && created) {
        SetTracePhase(st, LAB_TRACE_END_COMPOSITION);
        HRESULT end_hr = ITfComposition_EndComposition(created, ec);
        Lab_TraceEvent("composition.start.rollback_end", st, end_hr, 0);
    }
#ifdef LAB_AKEL_INSERT_FIRST_BUILD
    if (FAILED(hr) && insert_range) {
        HRESULT rollback_hr = ITfRange_SetText(insert_range, ec, 0, L"", 0);
        Lab_TraceEvent("insert_at_selection.insert_first.rollback", st, rollback_hr, 0);
    }
#endif
    if (created) ITfComposition_Release(created);
    if (cc) ITfContextComposition_Release(cc);
    if (insert_range) ITfRange_Release(insert_range);
    if (insert) ITfInsertAtSelection_Release(insert);
    return hr;
}

/* ── preedit 갱신 (§7.2) ── */
static HRESULT ReplacePreedit(LabEditSession *es, TfEditCookie ec, ITfRange *range,
                              const WCHAR *text, LONG len, bool already_inserted) {
    HRESULT hr = S_OK;
    if (already_inserted) {
        Lab_TraceEvent("range.set_text.skipped_insert_first", es->state, S_OK, len);
    } else {
        SetTracePhase(es->state, LAB_TRACE_SET_TEXT);
        Lab_TraceEvent("range.set_text.before", es->state, S_OK, len);
        hr = ITfRange_SetText(range, ec, 0, text, len);   /* flag 0 = normal update */
        Lab_TraceEvent("range.set_text.after", es->state, hr, len);
        if (FAILED(hr)) return hr;
    }
    /* 표시 속성 실패는 조합을 무효로 만들지 않는다 — 밑줄이 없을 뿐이다 */
    SetTracePhase(es->state, LAB_TRACE_DISPLAY_ATTRIBUTE);
    HRESULT attr_hr = Lab_ApplyInputAttribute(es->service, ec, es->state->context, range);
    Lab_TraceEvent("display_attribute.apply", es->state, attr_hr, 0);
    return SetSelectionAtEnd(es, ec, range);
}

/* ── 확정 prefix를 composition 밖으로 밀어낸다 (§7.3) ── */
static HRESULT CommitPrefix(LabEditSession *es, TfEditCookie ec, LONG committed_len) {
    LabContextState *st = es->state;
    ITfRange *whole = NULL, *new_start = NULL;
    LONG moved = 0;
    SetTracePhase(st, LAB_TRACE_COMMIT_PREFIX);
    HRESULT hr = ITfComposition_GetRange(st->composition, &whole);
    Lab_TraceEvent("commit_prefix.get_range", st, hr, committed_len);
    if (FAILED(hr)) goto done;

    /* GUID_PROP_READING: 확정 prefix 구간에 reading segment를 명시한다.
       CUAS가 이 정보를 GCS_RESULTCLAUSE로 옮기는지가 Phase 3의 관측 대상이다(§7.3).
       두벌식 실험체에서는 reading과 결과 문자열이 같지만, 목적은 경계를 명시하는 것이다.
       실패해도 확정 자체는 성립하므로 트랜잭션을 되돌리지 않는다. */
    {
        ITfProperty *reading = NULL;
        if (SUCCEEDED(ITfContext_GetProperty(st->context, &kPropReading, &reading))) {
            ITfRange *prefix = NULL;
            if (SUCCEEDED(ITfRange_Clone(whole, &prefix))) {
                LONG shifted = 0;
                /* prefix = [시작, 시작+committed_len) */
                if (SUCCEEDED(ITfRange_Collapse(prefix, ec, TF_ANCHOR_START)) &&
                    SUCCEEDED(ITfRange_ShiftEnd(prefix, ec, committed_len, &shifted, NULL)) &&
                    shifted == committed_len) {
                    WCHAR text[8];
                    ULONG got = 0;
                    if (SUCCEEDED(ITfRange_GetText(prefix, ec, 0, text,
                                                   (ULONG)(sizeof text / sizeof text[0]), &got))) {
                        BSTR bs = SysAllocStringLen(text, got);
                        if (bs) {
                            VARIANT v; VariantInit(&v);
                            v.vt = VT_BSTR; v.bstrVal = bs;
                            ITfProperty_SetValue(reading, ec, prefix, &v);
                            VariantClear(&v);
                        }
                    }
                }
                ITfRange_Release(prefix);
            }
            ITfProperty_Release(reading);
        }
    }

    hr = ITfRange_Clone(whole, &new_start);
    if (FAILED(hr)) goto done;
    hr = ITfRange_ShiftStart(new_start, ec, committed_len, &moved, NULL);
    if (FAILED(hr) || moved != committed_len) {
        if (SUCCEEDED(hr)) hr = E_FAIL;      /* 부분 이동은 실패로 다룬다 */
        goto done;
    }
    hr = ITfRange_Collapse(new_start, ec, TF_ANCHOR_START);
    if (FAILED(hr)) goto done;
    hr = ITfComposition_ShiftStart(st->composition, ec, new_start);
    Lab_TraceEvent("commit_prefix.shift_start", st, hr, moved);

done:
    if (new_start) ITfRange_Release(new_start);
    if (whole) ITfRange_Release(whole);
    return hr;
}

/* ── 실제 편집 (한 키 = 한 트랜잭션) ── */
static HRESULT DoWork(LabEditSession *es, TfEditCookie ec) {
    LabContextState *st = es->state;

    if (es->command == LAB_FINALIZE || es->command == LAB_CANCEL) {
        if (!st->composition) return S_OK;
        ITfRange *range = NULL;
        SetTracePhase(st, LAB_TRACE_GET_RANGE);
        HRESULT hr = ITfComposition_GetRange(st->composition, &range);
        Lab_TraceEvent("finalize.get_range", st, hr, es->command);
        if (SUCCEEDED(hr)) {
            if (es->command == LAB_CANCEL) {
                /* 취소: 조합 텍스트를 지우고 끝낸다 */
                SetTracePhase(st, LAB_TRACE_SET_TEXT);
                HRESULT set_hr = ITfRange_SetText(range, ec, 0, L"", 0);
                Lab_TraceEvent("cancel.set_text", st, set_hr, 0);
            } else {
                /* 확정: 텍스트는 그대로 두고 composition만 걷어낸다.
                   ShiftStart로 전체를 밖으로 밀어 빈 composition을 만든다(§7.4). */
                LONG moved = 0, len = 0;
                WCHAR buf[8];
                ULONG got = 0;
                ITfRange *clone = NULL;
                if (SUCCEEDED(ITfRange_Clone(range, &clone))) {
                    if (SUCCEEDED(ITfRange_GetText(clone, ec, 0, buf,
                                                   (ULONG)(sizeof buf/sizeof buf[0]), &got)))
                        len = (LONG)got;
                    ITfRange_Release(clone);
                }
                if (len > 0) {
                    HRESULT shift_hr = ITfRange_ShiftStart(range, ec, len, &moved, NULL);
                    Lab_TraceEvent("finalize.range_shift", st, shift_hr, moved);
                }
                SetSelectionAtEnd(es, ec, range);
            }
            ITfRange_Release(range);
        }
        SetTracePhase(st, LAB_TRACE_END_COMPOSITION);
        HRESULT end_hr = ITfComposition_EndComposition(st->composition, ec);
        Lab_TraceEvent("composition.end", st, end_hr, es->command);
        Lab_ForgetComposition(st);
        return hr;
    }

    /* UPDATE / COMMIT_PREFIX: build committed+preedit before selecting the startup protocol. */
    const HangulStep *step = es->step;
    WCHAR whole[8];
    LONG n = 0;
    for (uint8_t i = 0; i < step->committed_len; i++) whole[n++] = step->committed[i];
    for (uint8_t i = 0; i < step->preedit_len;   i++) whole[n++] = step->preedit[i];

    ITfRange *range = NULL;
    bool inserted_text = false;
    HRESULT hr = EnsureComposition(es, ec, whole, n, &inserted_text, &range);
    if (FAILED(hr)) return hr;

    hr = ReplacePreedit(es, ec, range, whole, n, inserted_text);
    if (SUCCEEDED(hr) && step->committed_len > 0) {
        hr = CommitPrefix(es, ec, (LONG)step->committed_len);
        /* 확정 후 preedit가 비었으면 composition을 끝낸다 */
        if (SUCCEEDED(hr) && step->preedit_len == 0) {
            SetTracePhase(st, LAB_TRACE_END_COMPOSITION);
            HRESULT end_hr = ITfComposition_EndComposition(es->state->composition, ec);
            Lab_TraceEvent("composition.end_after_prefix", st, end_hr, 0);
            Lab_ForgetComposition(es->state);
        }
    }
    ITfRange_Release(range);
    return hr;
}

/* ── ITfEditSession ── */
static STDMETHODIMP ES_QI(ITfEditSession *me, REFIID riid, void **ppv) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfEditSession)) {
        *ppv = me; InterlockedIncrement(&((LabEditSession*)me)->ref); return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static STDMETHODIMP_(ULONG) ES_AddRef(ITfEditSession *me) {
    return (ULONG)InterlockedIncrement(&((LabEditSession*)me)->ref);
}
static STDMETHODIMP_(ULONG) ES_Release(ITfEditSession *me) {
    LabEditSession *es = (LabEditSession*)me;
    LONG n = InterlockedDecrement(&es->ref);
    if (n == 0) free(es);
    return (ULONG)(n < 0 ? 0 : n);
}
static STDMETHODIMP ES_DoEditSession(ITfEditSession *me, TfEditCookie ec) {
    LabEditSession *es = (LabEditSession*)me;
    es->callback_ran = true;
    SetTracePhase(es->state, LAB_TRACE_EDIT_SESSION);
    Lab_TraceEvent("edit_session.callback.begin", es->state, S_OK, es->command);
    es->inner_hr = DoWork(es, ec);
    Lab_TraceEvent("edit_session.callback.end", es->state, es->inner_hr, es->command);
    return es->inner_hr;
}
static const ITfEditSessionVtbl g_es_vtbl = { ES_QI, ES_AddRef, ES_Release, ES_DoEditSession };

/* 요청 성공과 세션 성공을 분리해 돌려준다 (RFC-0009 §5.3) */
static HRESULT RequestSyncWrite(LabTextService *svc, LabContextState *st,
                                const HangulStep *step, LabCompositionCommand cmd,
                                LabSessionResult *out) {
    LabSessionResult local;
    if (!out) out = &local;
    out->request_hr = E_UNEXPECTED;
    out->session_hr = E_UNEXPECTED;
    out->callback_ran = false;

    LabEditSession *es = (LabEditSession*)calloc(1, sizeof(*es));
    if (!es) return E_OUTOFMEMORY;
    es->base.lpVtbl = (ITfEditSessionVtbl*)&g_es_vtbl;
    es->ref = 1;
    es->service = svc; es->state = st; es->step = step; es->command = cmd;
    es->inner_hr = E_UNEXPECTED;

    uint32_t transaction = ++svc->next_transaction;
    if (transaction == 0) transaction = ++svc->next_transaction;
    st->active_transaction = transaction;
    SetTracePhase(st, LAB_TRACE_REQUEST);
    uint32_t termination_before = st->termination_epoch;
    Lab_TraceEvent("edit_session.request", st, S_OK, cmd);
    Lab_TraceContextCaps(st);

    out->request_hr = ITfContext_RequestEditSession(st->context, svc->client_id,
                                                    &es->base, TF_ES_SYNC | TF_ES_READWRITE,
                                                    &out->session_hr);
    out->callback_ran = es->callback_ran;
    Lab_TraceSession(cmd == LAB_UPDATE ? "update" :
                     cmd == LAB_COMMIT_PREFIX ? "commit_prefix" :
                     cmd == LAB_FINALIZE ? "finalize" : "cancel",
                     st, out, es->inner_hr);
    Lab_TraceEvent("edit_session.post_state", st,
                   FAILED(out->request_hr) ? out->request_hr : out->session_hr,
                   (LONG)(st->termination_epoch - termination_before));
    if (FAILED(out->request_hr) || FAILED(out->session_hr)) {
        Lab_TraceContextCaps(st);
    }
    ITfEditSession_Release(&es->base);

    if (FAILED(out->request_hr)) return out->request_hr;
    return out->session_hr;
}

void Lab_ForgetComposition(LabContextState *st) {
    if (st->composition) {
        Lab_TraceEvent("composition.release_local", st, S_OK, 0);
        ITfComposition_Release(st->composition);
        st->composition = NULL;
    }
}

static void CloseTraceTransaction(LabContextState *st) {
    Lab_TraceEvent("edit_transaction.close", st, S_OK, 0);
    st->trace_phase = LAB_TRACE_NONE;
    st->active_transaction = 0;
}

/* 편집이 성공한 경우에만 FSM 상태를 publish한다 (RFC-0009 §5.2) */
HRESULT Lab_ApplyStep(LabTextService *svc, LabContextState *st,
                      const HangulStep *step, LabSessionResult *result) {
    LabCompositionCommand cmd = (step->committed_len > 0) ? LAB_COMMIT_PREFIX : LAB_UPDATE;
    HRESULT hr = RequestSyncWrite(svc, st, step, cmd, result);
    if (SUCCEEDED(hr)) {
        st->hangul = step->next;
        Lab_TraceEvent("fsm.publish", st, hr,
                       ((LONG)step->committed_len << 16) | step->preedit_len);
    }
    CloseTraceTransaction(st);
    return hr;
}

HRESULT Lab_FinalizeComposition(LabTextService *svc, LabContextState *st,
                                LabCompositionCommand cmd) {
    if (!st->composition && Hangul2_IsEmpty(&st->hangul)) return S_OK;
    HRESULT hr = RequestSyncWrite(svc, st, NULL, cmd, NULL);
    Hangul2_Reset(&st->hangul);
    Lab_TraceEvent("fsm.reset_after_finalize", st, hr, cmd);
    CloseTraceTransaction(st);
    return hr;
}

/* ── ITfCompositionSink: 앱이 조합을 끝냈을 때 (RFC-0009 §9.3) ── */
#define FROM_COMP_SINK(p) ((LabTextService*)((char*)(p) - offsetof(LabTextService, composition_sink)))

static STDMETHODIMP CS_QI(ITfCompositionSink *me, REFIID riid, void **ppv) {
    LabTextService *svc = FROM_COMP_SINK(me);
    return svc->tip.lpVtbl->QueryInterface(&svc->tip, riid, ppv);
}
static STDMETHODIMP_(ULONG) CS_AddRef(ITfCompositionSink *me) {
    LabTextService *svc = FROM_COMP_SINK(me);
    return svc->tip.lpVtbl->AddRef(&svc->tip);
}
static STDMETHODIMP_(ULONG) CS_Release(ITfCompositionSink *me) {
    LabTextService *svc = FROM_COMP_SINK(me);
    return svc->tip.lpVtbl->Release(&svc->tip);
}
static STDMETHODIMP CS_OnCompositionTerminated(ITfCompositionSink *me, TfEditCookie ec,
                                               ITfComposition *composition) {
    (void)ec;
    LabTextService *svc = FROM_COMP_SINK(me);
    /* 우리가 끝낸 것과 앱이 끝낸 것이 겹쳐도 안전해야 한다 — identity로 찾는다(§7.4) */
    for (LabContextState *p = svc->contexts; p; p = p->next) {
        if (p->composition == composition) {
            p->termination_epoch++;
            Lab_TraceEvent("composition_terminated", p, S_OK,
                           p->active_transaction ? 1 : 0);
            Lab_ForgetComposition(p);
            Hangul2_Reset(&p->hangul);
            return S_OK;
        }
    }
    Lab_TraceEvent("composition_terminated.unmatched", NULL, S_OK, 0);
    return S_OK;
}
static const ITfCompositionSinkVtbl g_comp_sink_vtbl = {
    CS_QI, CS_AddRef, CS_Release, CS_OnCompositionTerminated
};

void Lab_InitCompositionSink(LabTextService *svc) {
    svc->composition_sink.lpVtbl = (ITfCompositionSinkVtbl*)&g_comp_sink_vtbl;
}
