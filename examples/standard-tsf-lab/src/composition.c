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

/* ── caret을 range 끝에 명시적으로 둔다 (§7.2) ── */
static HRESULT SetSelectionAtEnd(TfEditCookie ec, ITfContext *ctx, ITfRange *range) {
    ITfRange *clone = NULL;
    HRESULT hr = ITfRange_Clone(range, &clone);
    if (FAILED(hr)) return hr;
    hr = ITfRange_Collapse(clone, ec, TF_ANCHOR_END);
    if (SUCCEEDED(hr)) {
        TF_SELECTION sel;
        sel.range = clone;
        sel.style.ase = TF_AE_END;
        sel.style.fInterimChar = FALSE;
        hr = ITfContext_SetSelection(ctx, ec, 1, &sel);
    }
    ITfRange_Release(clone);
    return hr;
}

/* ── composition 확보 (§7.1) ── */
static HRESULT EnsureComposition(LabEditSession *es, TfEditCookie ec, ITfRange **range_out) {
    LabContextState *st = es->state;
    *range_out = NULL;
    if (st->composition)
        return ITfComposition_GetRange(st->composition, range_out);

    ITfInsertAtSelection *insert = NULL;
    ITfRange *insert_range = NULL;
    ITfContextComposition *cc = NULL;
    ITfComposition *created = NULL;          /* 전부 성공한 뒤에만 publish */
    HRESULT hr;

    hr = ITfContext_QueryInterface(st->context, &IID_ITfInsertAtSelection, (void**)&insert);
    if (FAILED(hr)) goto done;
    /* TF_IAS_QUERYONLY: 텍스트를 넣지 않고 삽입 지점 range만 얻는다 */
    hr = ITfInsertAtSelection_InsertTextAtSelection(insert, ec, TF_IAS_QUERYONLY,
                                                    NULL, 0, &insert_range);
    if (FAILED(hr)) goto done;
    hr = ITfContext_QueryInterface(st->context, &IID_ITfContextComposition, (void**)&cc);
    if (FAILED(hr)) goto done;
    hr = ITfContextComposition_StartComposition(cc, ec, insert_range,
                                                &es->service->composition_sink, &created);
    if (SUCCEEDED(hr) && created == NULL) { hr = LAB_E_COMPOSITION_REJECTED; goto done; }
    if (FAILED(hr)) goto done;

    st->composition = created;               /* publish */
    created = NULL;
    hr = ITfComposition_GetRange(st->composition, range_out);

done:
    if (created) ITfComposition_Release(created);
    if (cc) ITfContextComposition_Release(cc);
    if (insert_range) ITfRange_Release(insert_range);
    if (insert) ITfInsertAtSelection_Release(insert);
    return hr;
}

/* ── preedit 갱신 (§7.2) ── */
static HRESULT ReplacePreedit(LabEditSession *es, TfEditCookie ec, ITfRange *range,
                              const WCHAR *text, LONG len) {
    HRESULT hr = ITfRange_SetText(range, ec, 0, text, len);   /* flag 0 = 일반 갱신 */
    if (FAILED(hr)) return hr;
    /* 표시 속성 실패는 조합을 무효로 만들지 않는다 — 밑줄이 없을 뿐이다 */
    Lab_ApplyInputAttribute(es->service, ec, es->state->context, range);
    return SetSelectionAtEnd(ec, es->state->context, range);
}

/* ── 확정 prefix를 composition 밖으로 밀어낸다 (§7.3) ── */
static HRESULT CommitPrefix(LabEditSession *es, TfEditCookie ec, LONG committed_len) {
    LabContextState *st = es->state;
    ITfRange *whole = NULL, *new_start = NULL;
    LONG moved = 0;
    HRESULT hr = ITfComposition_GetRange(st->composition, &whole);
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
        HRESULT hr = ITfComposition_GetRange(st->composition, &range);
        if (SUCCEEDED(hr)) {
            if (es->command == LAB_CANCEL) {
                /* 취소: 조합 텍스트를 지우고 끝낸다 */
                ITfRange_SetText(range, ec, 0, L"", 0);
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
                if (len > 0) ITfRange_ShiftStart(range, ec, len, &moved, NULL);
                SetSelectionAtEnd(ec, st->context, range);
            }
            ITfRange_Release(range);
        }
        ITfComposition_EndComposition(st->composition, ec);
        Lab_ForgetComposition(st);
        return hr;
    }

    /* UPDATE / COMMIT_PREFIX: composition을 확보하고 committed+preedit를 한 번에 쓴다 */
    ITfRange *range = NULL;
    HRESULT hr = EnsureComposition(es, ec, &range);
    if (FAILED(hr)) return hr;

    const HangulStep *step = es->step;
    WCHAR whole[8];
    LONG n = 0;
    for (uint8_t i = 0; i < step->committed_len; i++) whole[n++] = step->committed[i];
    for (uint8_t i = 0; i < step->preedit_len;   i++) whole[n++] = step->preedit[i];

    hr = ReplacePreedit(es, ec, range, whole, n);
    if (SUCCEEDED(hr) && step->committed_len > 0) {
        hr = CommitPrefix(es, ec, (LONG)step->committed_len);
        /* 확정 후 preedit가 비었으면 composition을 끝낸다 */
        if (SUCCEEDED(hr) && step->preedit_len == 0) {
            ITfComposition_EndComposition(es->state->composition, ec);
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
    es->inner_hr = DoWork(es, ec);
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

    out->request_hr = ITfContext_RequestEditSession(st->context, svc->client_id,
                                                    &es->base, TF_ES_SYNC | TF_ES_READWRITE,
                                                    &out->session_hr);
    out->callback_ran = es->callback_ran;
    ITfEditSession_Release(&es->base);

    if (FAILED(out->request_hr)) return out->request_hr;
    return out->session_hr;
}

void Lab_ForgetComposition(LabContextState *st) {
    if (st->composition) { ITfComposition_Release(st->composition); st->composition = NULL; }
}

/* 편집이 성공한 경우에만 FSM 상태를 publish한다 (RFC-0009 §5.2) */
HRESULT Lab_ApplyStep(LabTextService *svc, LabContextState *st,
                      const HangulStep *step, LabSessionResult *result) {
    LabCompositionCommand cmd = (step->committed_len > 0) ? LAB_COMMIT_PREFIX : LAB_UPDATE;
    HRESULT hr = RequestSyncWrite(svc, st, step, cmd, result);
    if (SUCCEEDED(hr)) st->hangul = step->next;
    return hr;
}

HRESULT Lab_FinalizeComposition(LabTextService *svc, LabContextState *st,
                                LabCompositionCommand cmd) {
    if (!st->composition && Hangul2_IsEmpty(&st->hangul)) return S_OK;
    HRESULT hr = RequestSyncWrite(svc, st, NULL, cmd, NULL);
    Hangul2_Reset(&st->hangul);
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
            Lab_ForgetComposition(p);
            Hangul2_Reset(&p->hangul);
            break;
        }
    }
    return S_OK;
}
static const ITfCompositionSinkVtbl g_comp_sink_vtbl = {
    CS_QI, CS_AddRef, CS_Release, CS_OnCompositionTerminated
};

void Lab_InitCompositionSink(LabTextService *svc) {
    svc->composition_sink.lpVtbl = (ITfCompositionSinkVtbl*)&g_comp_sink_vtbl;
}
