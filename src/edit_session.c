#include "edit_session.h"
#include <string.h>

typedef struct JamotongEditSession {
    ITfEditSessionVtbl *lpVtbl;
    LONG refCount;
    JamotongTextService *pService;
    ITfContext *pContext;
    EditSessionData data;
} JamotongEditSession;

static HRESULT STDMETHODCALLTYPE ES_QueryInterface(ITfEditSession *pThis, REFIID riid, void **ppvObject) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfEditSession)) {
        *ppvObject = pThis;
        pThis->lpVtbl->AddRef(pThis);
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ES_AddRef(ITfEditSession *pThis) {
    JamotongEditSession *es = (JamotongEditSession*)pThis;
    return InterlockedIncrement(&es->refCount);
}

static ULONG STDMETHODCALLTYPE ES_Release(ITfEditSession *pThis) {
    JamotongEditSession *es = (JamotongEditSession*)pThis;
    ULONG res = InterlockedDecrement(&es->refCount);
    if (res == 0) {
        es->pService->lpVtblTIP->Release((ITfTextInputProcessor*)es->pService);
        es->pContext->lpVtbl->Release(es->pContext);
        HeapFree(GetProcessHeap(), 0, es);
    }
    return res;
}

// 삽입 후 커서(선택)를 range 끝으로 이동. 이걸 안 하면 다음 삽입이 커서 위치(앞)에 계속 들어가
// 글자가 역순(abcd→dcba)으로 쌓인다. Clone으로 원본 range는 보존.
static void MoveCaretToEnd(ITfContext *ctx, TfEditCookie ec, ITfRange *range) {
    ITfRange *pEnd = NULL;
    if (SUCCEEDED(range->lpVtbl->Clone(range, &pEnd))) {
        pEnd->lpVtbl->Collapse(pEnd, ec, TF_ANCHOR_END);
        TF_SELECTION sel;
        sel.range = pEnd;
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        ctx->lpVtbl->SetSelection(ctx, ec, 1, &sel);
        pEnd->lpVtbl->Release(pEnd);
    }
}

// 현재 선택(캐럿)의 화면 rect를 svc->lastCaretRect에 기록 (조합 미리보기 오버레이용, RFC-0002).
//   TSF 정석: GetActiveView → GetTextExt(선택 range). TF_E_NOLAYOUT 등으로 실패하면
//   lastCaretValid=FALSE로 남고, 호출자(text_service)가 GetGUIThreadInfo로 폴백한다.
static void CaptureCaretRect(JamotongTextService *svc, ITfContext *ctx, TfEditCookie ec) {
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

// ── 커밋 전용 엔진 (감지 없음, TSF 조합 없음) ────────────────────────────────────────
//   확정된 음절(committed)만 문서에 삽입한다. 조합중(preedit)은 문서에 넣지 않고,
//   플로팅 오버레이(RFC-0002)가 캐럿 위치에 표시한다(캡처한 캐럿 rect 사용).
//   삽입(InsertTextAtSelection)은 네이티브·CUAS 모든 앱에서 동작하고, range 편집(제자리 교체)은
//   CUAS EDIT 컨트롤에서 안 되므로(누적 버그) 문서 인라인 미리보기는 하지 않는다.
static HRESULT STDMETHODCALLTYPE ES_DoEditSession(ITfEditSession *pThis, TfEditCookie ec) {
    JamotongEditSession *es = (JamotongEditSession*)pThis;
    ITfContext *ctx = es->pContext;
    int cLen = (int)wcslen(es->data.committed);

    if (cLen > 0) {
        ITfInsertAtSelection *pIns = NULL;
        if (SUCCEEDED(ctx->lpVtbl->QueryInterface(ctx, &IID_ITfInsertAtSelection, (void**)&pIns))) {
            ITfRange *r = NULL;
            if (SUCCEEDED(pIns->lpVtbl->InsertTextAtSelection(pIns, ec, 0, es->data.committed, cLen, &r)) && r) {
                MoveCaretToEnd(ctx, ec, r);   // 커서를 삽입 글자 뒤로 (역순 방지)
                r->lpVtbl->Release(r);
            }
            pIns->lpVtbl->Release(pIns);
        }
    }
    CaptureCaretRect(es->pService, ctx, ec);   // 삽입 '후' 캐럿 = 조합 미리보기가 뜰 자리
    return S_OK;
}

static ITfEditSessionVtbl EditSessionVtbl = {
    ES_QueryInterface,
    ES_AddRef,
    ES_Release,
    ES_DoEditSession
};

HRESULT RequestEditSessionData(JamotongTextService *pService, ITfContext *pContext, const EditSessionData *data) {
    JamotongEditSession *es = (JamotongEditSession*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(JamotongEditSession));
    if (!es) return E_OUTOFMEMORY;
    
    es->lpVtbl = &EditSessionVtbl;
    es->refCount = 1;
    es->pService = pService;
    es->pContext = pContext;
    es->data = *data;
    
    pService->lpVtblTIP->AddRef((ITfTextInputProcessor*)pService);
    pContext->lpVtbl->AddRef(pContext);

    pService->lastCaretValid = FALSE;   // 세션 실패/미도달 시 낡은 캐럿 rect 사용 방지
    // 동기(TF_ES_SYNC) — NavilIME도 동기를 쓰며 AkelPad에서 정상 동작한다. (async는 무효였음)
    HRESULT hrSession = S_OK;
    HRESULT hr = pContext->lpVtbl->RequestEditSession(pContext, pService->clientId, (ITfEditSession*)es, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);

    es->lpVtbl->Release((ITfEditSession*)es);
    return hr;
}

HRESULT RequestEditSession(JamotongTextService *pService, ITfContext *pContext, FsmResult fsmRes) {
    EditSessionData data = {0};
    if (fsmRes.commitChar) data.committed[0] = fsmRes.commitChar;
    if (fsmRes.preeditChar) data.composing[0] = fsmRes.preeditChar;
    return RequestEditSessionData(pService, pContext, &data);
}

// ----------------------------------------------------
// 읽기 세션 (단어 단위 한자 변환을 위해 커서 앞의 텍스트 읽기)
// ----------------------------------------------------
typedef struct {
    ITfEditSessionVtbl *lpVtbl;
    LONG refCount;
    JamotongTextService *pService;
    ITfContext *pContext;
    wchar_t *outBuf;
    int maxLen;
} ReadEditSession;

static HRESULT STDMETHODCALLTYPE RES_QueryInterface(ITfEditSession *pThis, REFIID riid, void **ppvObject) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfEditSession)) {
        *ppvObject = pThis;
        pThis->lpVtbl->AddRef(pThis);
        return S_OK;
    }
    *ppvObject = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE RES_AddRef(ITfEditSession *pThis) {
    return InterlockedIncrement(&((ReadEditSession*)pThis)->refCount);
}
static ULONG STDMETHODCALLTYPE RES_Release(ITfEditSession *pThis) {
    ReadEditSession *es = (ReadEditSession*)pThis;
    ULONG res = InterlockedDecrement(&es->refCount);
    if (res == 0) {
        es->pService->lpVtblTIP->Release((ITfTextInputProcessor*)es->pService);
        es->pContext->lpVtbl->Release(es->pContext);
        HeapFree(GetProcessHeap(), 0, es);
    }
    return res;
}
static HRESULT STDMETHODCALLTYPE RES_DoEditSession(ITfEditSession *pThis, TfEditCookie ec) {
    ReadEditSession *es = (ReadEditSession*)pThis;
    es->outBuf[0] = L'\0';
    
    ITfInsertAtSelection *pInsert = NULL;
    if (FAILED(es->pContext->lpVtbl->QueryInterface(es->pContext, &IID_ITfInsertAtSelection, (void**)&pInsert))) return E_FAIL;
    
    ITfRange *pRange = NULL;
    // READ 쿠키에선 QUERYONLY(삽입 없이 선택 range만)만 유효. NOQUERY(수정 삽입)는 실패했음.
    if (SUCCEEDED(pInsert->lpVtbl->InsertTextAtSelection(pInsert, ec, TF_IAS_QUERYONLY, NULL, 0, &pRange)) && pRange) {
        LONG cch = 0;
        pRange->lpVtbl->ShiftStart(pRange, ec, -es->maxLen, &cch, NULL);
        if (cch < 0) {
            ULONG copied = 0;
            pRange->lpVtbl->GetText(pRange, ec, 0, es->outBuf, es->maxLen, &copied);
            es->outBuf[copied] = L'\0';
        }
        pRange->lpVtbl->Release(pRange);
    }
    pInsert->lpVtbl->Release(pInsert);
    return S_OK;
}
static ITfEditSessionVtbl ReadSessionVtbl = { RES_QueryInterface, RES_AddRef, RES_Release, RES_DoEditSession };

HRESULT RequestReadSessionString(JamotongTextService *pService, ITfContext *pContext, wchar_t *outBuf, int maxLen) {
    ReadEditSession *es = (ReadEditSession*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ReadEditSession));
    if (!es) return E_OUTOFMEMORY;
    es->lpVtbl = &ReadSessionVtbl; es->refCount = 1; es->pService = pService; es->pContext = pContext;
    es->outBuf = outBuf; es->maxLen = maxLen;
    pService->lpVtblTIP->AddRef((ITfTextInputProcessor*)pService); pContext->lpVtbl->AddRef(pContext);
    HRESULT hrSession = S_OK;
    HRESULT hr = pContext->lpVtbl->RequestEditSession(pContext, pService->clientId, (ITfEditSession*)es, TF_ES_SYNC | TF_ES_READ, &hrSession);
    es->lpVtbl->Release((ITfEditSession*)es);
    return hr;
}

// ----------------------------------------------------
// 교체 세션 (한자 선택 시 기존 텍스트 교체)
// ----------------------------------------------------
typedef struct {
    ITfEditSessionVtbl *lpVtbl;
    LONG refCount;
    JamotongTextService *pService;
    ITfContext *pContext;
    int replaceLen;
    wchar_t replacement[128];
} ReplaceEditSession;

static HRESULT STDMETHODCALLTYPE Rep_QueryInterface(ITfEditSession *pThis, REFIID riid, void **ppvObject) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfEditSession)) {
        *ppvObject = pThis; pThis->lpVtbl->AddRef(pThis); return S_OK;
    }
    *ppvObject = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE Rep_AddRef(ITfEditSession *pThis) {
    return InterlockedIncrement(&((ReplaceEditSession*)pThis)->refCount);
}
static ULONG STDMETHODCALLTYPE Rep_Release(ITfEditSession *pThis) {
    ReplaceEditSession *es = (ReplaceEditSession*)pThis;
    ULONG res = InterlockedDecrement(&es->refCount);
    if (res == 0) {
        es->pService->lpVtblTIP->Release((ITfTextInputProcessor*)es->pService);
        es->pContext->lpVtbl->Release(es->pContext);
        HeapFree(GetProcessHeap(), 0, es);
    }
    return res;
}
static HRESULT STDMETHODCALLTYPE Rep_DoEditSession(ITfEditSession *pThis, TfEditCookie ec) {
    ReplaceEditSession *es = (ReplaceEditSession*)pThis;

    // 치환(단어단위 한자) 후 오토마타 초기화 — 안 하면 다음 키가 옛 음절을 이어가 중복/깨짐.
    Fsm_Init(&es->pService->fsm);

    ITfInsertAtSelection *pInsert = NULL;
    if (FAILED(es->pContext->lpVtbl->QueryInterface(es->pContext, &IID_ITfInsertAtSelection, (void**)&pInsert))) return E_FAIL;
    
    ITfRange *pRange = NULL;
    // TF_IAS_QUERYONLY: 삽입 없이 현재 선택 range만 얻는다(MSDN 규약 — NOQUERY는 ppRange를 채우지
    // 않을 수 있어 NULL 역참조 위험이 있었음). 얻은 range의 시작을 뒤로 밀어 교체 대상 확보 후 SetText.
    if (SUCCEEDED(pInsert->lpVtbl->InsertTextAtSelection(pInsert, ec, TF_IAS_QUERYONLY, NULL, 0, &pRange)) && pRange) {
        LONG cch = 0;
        pRange->lpVtbl->ShiftStart(pRange, ec, -es->replaceLen, &cch, NULL);
        int rlen = (int)wcslen(es->replacement);
        pRange->lpVtbl->SetText(pRange, ec, 0, es->replacement, rlen);
        MoveCaretToEnd(es->pContext, ec, pRange);   // 커서를 치환 글자 뒤로 (역순 방지)
        pRange->lpVtbl->Release(pRange);
    }
    pInsert->lpVtbl->Release(pInsert);
    return S_OK;
}
static ITfEditSessionVtbl RepSessionVtbl = { Rep_QueryInterface, Rep_AddRef, Rep_Release, Rep_DoEditSession };

HRESULT RequestReplaceSessionString(JamotongTextService *pService, ITfContext *pContext, int replaceLen, const wchar_t *replacement) {
    ReplaceEditSession *es = (ReplaceEditSession*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ReplaceEditSession));
    if (!es) return E_OUTOFMEMORY;
    es->lpVtbl = &RepSessionVtbl; es->refCount = 1; es->pService = pService; es->pContext = pContext;
    es->replaceLen = replaceLen;
    wcsncpy(es->replacement, replacement, 127); es->replacement[127] = L'\0';   // [128] 버퍼 오버플로 방지
    pService->lpVtblTIP->AddRef((ITfTextInputProcessor*)pService); pContext->lpVtbl->AddRef(pContext);
    HRESULT hrSession = S_OK;
    HRESULT hr = pContext->lpVtbl->RequestEditSession(pContext, pService->clientId, (ITfEditSession*)es, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    es->lpVtbl->Release((ITfEditSession*)es);
    return hr;
}
