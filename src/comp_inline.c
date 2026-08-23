// comp_inline.c — RFC-0010 문서 인라인 표준 composition (비단명 컨텍스트 전용)
//   근거: AkelPad R1~R4 + R2 재실행(단명=키별 종료가 정상), lab Phase 2(메모장 유지 PASS).
//   외부 종료(sink)·세션 실패는 감추지 않는다 — forget + FSM 리셋 + 강등 카운트.
#include "comp_inline.h"
#include "edit_session.h"   // JamoDiag

// StartComposition이 S_OK인데 NULL을 준 경우를 실패로 다룬다 (lab §7.1과 동일).
#define JAMO_E_COMPOSITION_REJECTED MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x201)

typedef enum { COMP_OP_UPDATE = 0, COMP_OP_FINALIZE = 1, COMP_OP_CANCEL = 2 } CompOp;

typedef struct InlineEditSession {
    ITfEditSessionVtbl *lpVtbl;
    LONG refCount;
    JamotongTextService *svc;
    ITfContext *ctx;
    CompOp op;
    wchar_t commit;    // COMP_OP_UPDATE: 확정 음절 (0=없음)
    wchar_t preedit;   // COMP_OP_UPDATE: 조합 중 음절 (0=없음)
} InlineEditSession;

// ── 내부 헬퍼 ────────────────────────────────────────────────────────────────────

// 로컬 composition 참조 정리 (문서 상태는 건드리지 않음).
static void ForgetComposition(JamotongTextService *svc) {
    if (svc->pComposition) {
        svc->pComposition->lpVtbl->Release(svc->pComposition);
        svc->pComposition = NULL;
    }
    if (svc->pCompContext) {
        svc->pCompContext->lpVtbl->Release(svc->pCompContext);
        svc->pCompContext = NULL;
    }
    svc->compUpdatedOnce = FALSE;
}

// 캐럿(선택)을 range 끝으로 — 생략하면 모든 호스트가 조합을 즉시 종료한다(실기 2026-07-05).
static HRESULT SelectRangeEnd(ITfContext *ctx, TfEditCookie ec, ITfRange *range) {
    ITfRange *pEnd = NULL;
    HRESULT hr = range->lpVtbl->Clone(range, &pEnd);
    if (SUCCEEDED(hr) && pEnd) {
        hr = pEnd->lpVtbl->Collapse(pEnd, ec, TF_ANCHOR_END);
        if (SUCCEEDED(hr)) {
            TF_SELECTION sel;
            sel.range = pEnd;
            sel.style.ase = TF_AE_END;
            sel.style.fInterimChar = FALSE;
            hr = ctx->lpVtbl->SetSelection(ctx, ec, 1, &sel);
        }
        pEnd->lpVtbl->Release(pEnd);
    }
    return hr;
}

// 조합 캐럿 화면 rect 캡처(팝업 위치용) — edit_session.c CaptureCaretRect와 동일한 폴백 계약.
static void CaptureCaret(JamotongTextService *svc, ITfContext *ctx, TfEditCookie ec) {
    svc->lastCaretValid = FALSE;
    TF_SELECTION sel; ULONG fetched = 0;
    if (FAILED(ctx->lpVtbl->GetSelection(ctx, ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) || fetched == 0)
        return;
    ITfContextView *pView = NULL;
    if (SUCCEEDED(ctx->lpVtbl->GetActiveView(ctx, &pView)) && pView) {
        RECT rc; BOOL clipped = FALSE;
        if (SUCCEEDED(pView->lpVtbl->GetTextExt(pView, ec, sel.range, &rc, &clipped))
            && (rc.right - rc.left >= 0) && (rc.bottom - rc.top > 0)) {
            svc->lastCaretRect = rc;
            svc->lastCaretValid = TRUE;
        }
        pView->lpVtbl->Release(pView);
    }
    sel.range->lpVtbl->Release(sel.range);
}

// composition 확보: 있으면 range 재획득(재사용 = 생존 → 강등 카운트 리셋), 없으면 생성.
// 전부 성공한 뒤에만 svc->pComposition에 publish. 실패 시 만든 것은 rollback(End)한다.
static HRESULT EnsureComposition(InlineEditSession *es, TfEditCookie ec, ITfRange **rangeOut) {
    JamotongTextService *svc = es->svc;
    *rangeOut = NULL;

    if (svc->pComposition) {
        HRESULT hr = svc->pComposition->lpVtbl->GetRange(svc->pComposition, rangeOut);
        if (SUCCEEDED(hr)) {
            svc->compUpdatedOnce = TRUE;   // 갱신에서 생존한 조합 — 이 컨텍스트는 건강
            svc->pathDemerits = 0;
        }
        return hr;
    }

    ITfInsertAtSelection *pIns = NULL;
    ITfRange *insRange = NULL;
    ITfContextComposition *pCC = NULL;
    ITfComposition *created = NULL;
    HRESULT hr = es->ctx->lpVtbl->QueryInterface(es->ctx, &IID_ITfInsertAtSelection, (void**)&pIns);
    if (FAILED(hr)) goto done;
    // TF_IAS_QUERYONLY: 텍스트 없이 삽입 지점 range만 얻는다.
    hr = pIns->lpVtbl->InsertTextAtSelection(pIns, ec, TF_IAS_QUERYONLY, NULL, 0, &insRange);
    if (FAILED(hr) || !insRange) { if (SUCCEEDED(hr)) hr = E_UNEXPECTED; goto done; }
    hr = es->ctx->lpVtbl->QueryInterface(es->ctx, &IID_ITfContextComposition, (void**)&pCC);
    if (FAILED(hr)) goto done;
    // 실제 sink 필수 — NULL sink는 일부 경로에서 E_INVALIDARG로 실패한다(실기 교훈).
    hr = pCC->lpVtbl->StartComposition(pCC, ec, insRange,
                                       (ITfCompositionSink*)&svc->lpVtblCompSink, &created);
    JamoDiag("COMP start hr=0x%08lX p=%p", (unsigned long)hr, (void*)created);
    if (SUCCEEDED(hr) && created == NULL) { hr = JAMO_E_COMPOSITION_REJECTED; goto done; }
    if (FAILED(hr)) goto done;
    hr = created->lpVtbl->GetRange(created, rangeOut);
    if (FAILED(hr)) goto done;

    svc->pComposition = created;   // publish (참조 이관)
    created = NULL;
    svc->pCompContext = es->ctx;
    es->ctx->lpVtbl->AddRef(es->ctx);
    svc->compUpdatedOnce = FALSE;

done:
    if (FAILED(hr) && created) created->lpVtbl->EndComposition(created, ec);   // rollback
    if (created) created->lpVtbl->Release(created);
    if (pCC) pCC->lpVtbl->Release(pCC);
    if (insRange) insRange->lpVtbl->Release(insRange);
    if (pIns) pIns->lpVtbl->Release(pIns);
    return hr;
}

// 확정 prefix(commitLen자)를 composition 밖으로 민다 (lab §7.3: ShiftStart, 부분 이동=실패).
static HRESULT CommitPrefix(InlineEditSession *es, TfEditCookie ec, LONG commitLen) {
    JamotongTextService *svc = es->svc;
    ITfRange *whole = NULL, *newStart = NULL;
    LONG moved = 0;
    HRESULT hr = svc->pComposition->lpVtbl->GetRange(svc->pComposition, &whole);
    if (FAILED(hr)) goto done;
    hr = whole->lpVtbl->Clone(whole, &newStart);
    if (FAILED(hr)) goto done;
    hr = newStart->lpVtbl->ShiftStart(newStart, ec, commitLen, &moved, NULL);
    if (FAILED(hr) || moved != commitLen) { if (SUCCEEDED(hr)) hr = E_FAIL; goto done; }
    hr = newStart->lpVtbl->Collapse(newStart, ec, TF_ANCHOR_START);
    if (FAILED(hr)) goto done;
    hr = svc->pComposition->lpVtbl->ShiftStart(svc->pComposition, ec, newStart);
done:
    if (newStart) newStart->lpVtbl->Release(newStart);
    if (whole) whole->lpVtbl->Release(whole);
    return hr;
}

// ── 편집 세션 본체 (한 키 = 한 동기 트랜잭션) ───────────────────────────────────────
static HRESULT DoInlineWork(InlineEditSession *es, TfEditCookie ec) {
    JamotongTextService *svc = es->svc;

    if (es->op == COMP_OP_FINALIZE || es->op == COMP_OP_CANCEL) {
        if (!svc->pComposition) return S_OK;
        ITfRange *range = NULL;
        HRESULT hr = svc->pComposition->lpVtbl->GetRange(svc->pComposition, &range);
        if (SUCCEEDED(hr) && range) {
            if (es->op == COMP_OP_CANCEL) {
                range->lpVtbl->SetText(range, ec, 0, L"", 0);   // 조합 텍스트 제거
            } else {
                SelectRangeEnd(es->ctx, ec, range);             // 텍스트 유지, 캐럿은 뒤로
            }
            range->lpVtbl->Release(range);
        }
        HRESULT endHr = svc->pComposition->lpVtbl->EndComposition(svc->pComposition, ec);
        JamoDiag("COMP %s end hr=0x%08lX", es->op == COMP_OP_CANCEL ? "cancel" : "finalize",
                 (unsigned long)endHr);
        ForgetComposition(svc);
        CaptureCaret(svc, es->ctx, ec);
        return FAILED(hr) ? hr : endHr;
    }

    // COMP_OP_UPDATE: whole = [commit][preedit] (각 0 또는 1자 — FsmResult 계약)
    wchar_t whole[3];
    LONG n = 0;
    if (es->commit) whole[n++] = es->commit;
    if (es->preedit) whole[n++] = es->preedit;
    whole[n] = L'\0';

    if (n == 0) {
        // 백스페이스로 조합이 비었다 — 조합 텍스트를 지우고 끝낸다 (취소와 동일 동작).
        if (!svc->pComposition) return S_OK;
        es->op = COMP_OP_CANCEL;
        return DoInlineWork(es, ec);
    }

    ITfRange *range = NULL;
    BOOL createdNow = (svc->pComposition == NULL);
    HRESULT hr = EnsureComposition(es, ec, &range);
    if (FAILED(hr)) return hr;

    hr = range->lpVtbl->SetText(range, ec, 0, whole, n);   // flag 0 = 일반 갱신 (조합 밑줄은 표시 속성 몫)
    if (FAILED(hr)) {
        // 방금 만든 조합이면 rollback — 실패를 감추면 증상이 엉뚱한 곳에서 나타난다.
        if (createdNow && svc->pComposition) {
            svc->pComposition->lpVtbl->EndComposition(svc->pComposition, ec);
            ForgetComposition(svc);
        }
        range->lpVtbl->Release(range);
        return hr;
    }
    // 표시 속성(밑줄) 실패는 조합을 무효로 만들지 않는다 — 밑줄이 없을 뿐이다.
    DA_ApplyToRange(es->ctx, ec, range, svc->daAtom);
    hr = SelectRangeEnd(es->ctx, ec, range);
    range->lpVtbl->Release(range);
    if (FAILED(hr)) return hr;

    if (es->commit) {
        hr = CommitPrefix(es, ec, 1);
        if (SUCCEEDED(hr) && !es->preedit) {
            HRESULT endHr = svc->pComposition->lpVtbl->EndComposition(svc->pComposition, ec);
            (void)endHr;   // 확정은 이미 성립 — End 실패로 되돌리지 않는다
            ForgetComposition(svc);
        }
    }
    CaptureCaret(svc, es->ctx, ec);
    return hr;
}

// ── ITfEditSession ──────────────────────────────────────────────────────────────
static HRESULT STDMETHODCALLTYPE IES_QueryInterface(ITfEditSession *pThis, REFIID riid, void **ppv) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfEditSession)) {
        *ppv = pThis;
        pThis->lpVtbl->AddRef(pThis);
        return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE IES_AddRef(ITfEditSession *pThis) {
    return InterlockedIncrement(&((InlineEditSession*)pThis)->refCount);
}
static ULONG STDMETHODCALLTYPE IES_Release(ITfEditSession *pThis) {
    InlineEditSession *es = (InlineEditSession*)pThis;
    ULONG res = InterlockedDecrement(&es->refCount);
    if (res == 0) {
        es->svc->lpVtblTIP->Release((ITfTextInputProcessor*)es->svc);
        es->ctx->lpVtbl->Release(es->ctx);
        HeapFree(GetProcessHeap(), 0, es);
    }
    return res;
}
static HRESULT STDMETHODCALLTYPE IES_DoEditSession(ITfEditSession *pThis, TfEditCookie ec) {
    return DoInlineWork((InlineEditSession*)pThis, ec);
}
static ITfEditSessionVtbl g_InlineSessionVtbl = {
    IES_QueryInterface, IES_AddRef, IES_Release, IES_DoEditSession
};

// 동기 요청 + 세션 내부 hr 전파 (RFC-0004 P2-2 — 요청 hr만 반환하면 내부 실패가 숨는다).
static HRESULT RequestInline(JamotongTextService *svc, ITfContext *ctx, CompOp op,
                             wchar_t commit, wchar_t preedit) {
    InlineEditSession *es = (InlineEditSession*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*es));
    if (!es) return E_OUTOFMEMORY;
    es->lpVtbl = &g_InlineSessionVtbl;
    es->refCount = 1;
    es->svc = svc; es->ctx = ctx;
    es->op = op; es->commit = commit; es->preedit = preedit;
    svc->lpVtblTIP->AddRef((ITfTextInputProcessor*)svc);
    ctx->lpVtbl->AddRef(ctx);

    HRESULT hrSession = S_OK;
    HRESULT hr = ctx->lpVtbl->RequestEditSession(ctx, svc->clientId, (ITfEditSession*)es,
                                                 TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    // 키 이벤트 밖(compartment 통지로 온 자판 전환 등)에서는 동기 세션이 TF_E_SYNCHRONOUS 로 거부된다.
    // MS SampleIME 의 _TerminateComposition 처럼 비동기(ASYNCDONTCARE)로 다시 건다 — 세션 객체는 힙+참조계수라 안전.
    if (hr == TF_E_SYNCHRONOUS) {
        hrSession = S_OK;
        hr = ctx->lpVtbl->RequestEditSession(ctx, svc->clientId, (ITfEditSession*)es,
                                             TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hrSession);
    }
    es->lpVtbl->Release((ITfEditSession*)es);
    return FAILED(hr) ? hr : hrSession;
}

// ── ITfCompositionSink: 호스트/CUAS가 조합을 끝냈을 때 ─────────────────────────────
static HRESULT STDMETHODCALLTYPE CS_QueryInterface(ITfCompositionSink *pThis, REFIID riid, void **ppv) {
    JamotongTextService *obj = IMPL_TO_OBJ(CompSink, pThis);
    return obj->lpVtblTIP->QueryInterface((ITfTextInputProcessor*)obj, riid, ppv);
}
static ULONG STDMETHODCALLTYPE CS_AddRef(ITfCompositionSink *pThis) {
    JamotongTextService *obj = IMPL_TO_OBJ(CompSink, pThis);
    return obj->lpVtblTIP->AddRef((ITfTextInputProcessor*)obj);
}
static ULONG STDMETHODCALLTYPE CS_Release(ITfCompositionSink *pThis) {
    JamotongTextService *obj = IMPL_TO_OBJ(CompSink, pThis);
    return obj->lpVtblTIP->Release((ITfTextInputProcessor*)obj);
}
static HRESULT STDMETHODCALLTYPE CS_OnCompositionTerminated(ITfCompositionSink *pThis,
                                                            TfEditCookie ec,
                                                            ITfComposition *pComposition) {
    (void)ec;
    JamotongTextService *obj = IMPL_TO_OBJ(CompSink, pThis);
    // 우리 종료와 겹쳐도 pointer identity로 안전하게 판별한다 (lab §7.4).
    if (pComposition && pComposition == obj->pComposition) {
        // 외부 종료: 조합 텍스트는 호스트가 확정한 그대로 문서에 남는다.
        // 갱신에서 한 번도 생존하지 못한 조합의 키별 종료가 반복되면 이 컨텍스트를 강등한다
        // (비단명 플래그가 보증이 아닌 호스트 대비 — 매뉴얼 §12.7.8).
        BOOL survived = obj->compUpdatedOnce;
        JamoDiag("COMP terminated externally (survived=%d demerits=%d)",
                 (int)survived, obj->pathDemerits);
        ForgetComposition(obj);
        Fsm_Init(&obj->fsm);   // 다음 키는 새 조합으로 시작 (이어 붙이면 자모가 겹친다)
        if (survived) {
            obj->pathDemerits = 0;
        } else if (obj->pathDemerits < JAMO_PATH_DEMOTE_LIMIT) {
            obj->pathDemerits++;
            if (obj->pathDemerits >= JAMO_PATH_DEMOTE_LIMIT) obj->pathKind = JAMO_PATH_COMMIT;
        }
    }
    return S_OK;
}
static const ITfCompositionSinkVtbl g_CompSinkVtbl = {
    CS_QueryInterface, CS_AddRef, CS_Release, CS_OnCompositionTerminated
};

// ── 공개 API ────────────────────────────────────────────────────────────────────
void JamoComp_Init(JamotongTextService *svc) {
    svc->lpVtblCompSink = &g_CompSinkVtbl;
    svc->pComposition = NULL;
    svc->pCompContext = NULL;
    svc->pPathContext = NULL;
    svc->pathKind = JAMO_PATH_COMMIT;
    svc->pathDemerits = 0;
    svc->compUpdatedOnce = FALSE;
}

JamoPathKind JamoComp_PathForContext(JamotongTextService *svc, ITfContext *pic) {
    if (!pic) return JAMO_PATH_COMMIT;
    if (!svc->config.options.inlineComposition) return JAMO_PATH_COMMIT;   // 킬스위치
    if (pic == svc->pPathContext) return (JamoPathKind)svc->pathKind;

    // 새 컨텍스트: 판정 1회 + 캐시. GetStatus는 edit cookie가 필요 없는 동기 호출이다.
    TF_STATUS status;
    ZeroMemory(&status, sizeof status);
    HRESULT statusHr = pic->lpVtbl->GetStatus(pic, &status);

    void *probe = NULL;
    int hasInsert = SUCCEEDED(pic->lpVtbl->QueryInterface(pic, &IID_ITfInsertAtSelection, &probe));
    if (probe) { ((IUnknown*)probe)->lpVtbl->Release((IUnknown*)probe); probe = NULL; }
    int hasCtxComp = SUCCEEDED(pic->lpVtbl->QueryInterface(pic, &IID_ITfContextComposition, &probe));
    if (probe) { ((IUnknown*)probe)->lpVtbl->Release((IUnknown*)probe); probe = NULL; }

    svc->pathDemerits = 0;   // 컨텍스트가 바뀌면 강등 이력도 새로 센다
    JamoPathKind kind = JamoPath_Decide((long)statusHr, (unsigned long)status.dwStaticFlags,
                                        hasInsert, hasCtxComp, svc->pathDemerits);
    JamoDiag("COMP path=%s statusHr=0x%08lX static=0x%lX ins=%d cc=%d",
             kind == JAMO_PATH_STANDARD ? "STANDARD" : "COMMIT",
             (unsigned long)statusHr, (unsigned long)status.dwStaticFlags, hasInsert, hasCtxComp);
    svc->pPathContext = pic;   // weak — 포인터 비교 전용, 포커스 이동 시 무효화
    svc->pathKind = (int)kind;
    return kind;
}

HRESULT JamoComp_Apply(JamotongTextService *svc, ITfContext *pic, FsmResult res) {
    HRESULT hr = RequestInline(svc, pic, COMP_OP_UPDATE, res.commitChar, res.preeditChar);
    if (FAILED(hr)) {
        JamoDiag("COMP apply failed hr=0x%08lX -> cancel+demote", (unsigned long)hr);
        JamoComp_Cancel(svc);   // 문서에 남았을 수 있는 조합 텍스트 제거 시도 (없으면 no-op)
        if (svc->pathDemerits < JAMO_PATH_DEMOTE_LIMIT) svc->pathDemerits = JAMO_PATH_DEMOTE_LIMIT;
        svc->pathKind = JAMO_PATH_COMMIT;   // 이 컨텍스트는 즉시 강등 — 같은 실패를 반복하지 않는다
    }
    return hr;
}

BOOL JamoComp_IsActive(const JamotongTextService *svc) {
    return svc->pComposition != NULL;
}

void JamoComp_Finalize(JamotongTextService *svc) {
    if (!svc->pComposition || !svc->pCompContext) return;
    ITfContext *ctx = svc->pCompContext;
    ctx->lpVtbl->AddRef(ctx);   // 세션 도중 ForgetComposition이 pCompContext를 놓아도 안전
    HRESULT hr = RequestInline(svc, ctx, COMP_OP_FINALIZE, 0, 0);
    if (FAILED(hr)) ForgetComposition(svc);   // 세션 불가(컨텍스트 소멸 등) → 로컬 참조만 정리
    ctx->lpVtbl->Release(ctx);
}

void JamoComp_Cancel(JamotongTextService *svc) {
    if (!svc->pComposition || !svc->pCompContext) return;
    ITfContext *ctx = svc->pCompContext;
    ctx->lpVtbl->AddRef(ctx);
    HRESULT hr = RequestInline(svc, ctx, COMP_OP_CANCEL, 0, 0);
    if (FAILED(hr)) ForgetComposition(svc);
    ctx->lpVtbl->Release(ctx);
}

void JamoComp_ResetPathCache(JamotongTextService *svc) {
    svc->pPathContext = NULL;
    svc->pathKind = JAMO_PATH_COMMIT;
    svc->pathDemerits = 0;
}

void JamoComp_Release(JamotongTextService *svc) {
    JamoComp_Finalize(svc);   // 남은 조합은 텍스트를 보존한 채 확정
    ForgetComposition(svc);
    JamoComp_ResetPathCache(svc);
}
