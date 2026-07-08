#include "edit_session.h"
#include <richedit.h>   // EM_EXGETSEL/EM_GETSELTEXT/CHARRANGE (선택 읽기 RichEdit 폴백)
#include <string.h>

#ifdef JAMO_DIAG   // 임시 진단 로그 (-DJAMO_DIAG 빌드에서만): %TEMP%\jamotong-diag.log
#include <stdio.h>
#include <stdarg.h>
void JamoDiag(const char *fmt, ...) {
    wchar_t path[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, path);
    if (!n || n > MAX_PATH - 24) return;
    wcscat(path, L"jamotong-diag.log");
    FILE *fp = _wfopen(path, L"a");
    if (!fp) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fclose(fp);
}
#else
void JamoDiag(const char *fmt, ...) { (void)fmt; }
#endif

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
    HRESULT hrOut = S_OK;   // 삽입 실패를 hrSession으로 전파 (RFC-0004 P2-2: S_OK로 삼키지 않음)

    if (cLen > 0) {
        ITfInsertAtSelection *pIns = NULL;
        hrOut = ctx->lpVtbl->QueryInterface(ctx, &IID_ITfInsertAtSelection, (void**)&pIns);
        if (SUCCEEDED(hrOut)) {
            ITfRange *r = NULL;
            HRESULT hrIns = pIns->lpVtbl->InsertTextAtSelection(pIns, ec, 0, es->data.committed, cLen, &r);
            JamoDiag("INSERT U+%04X len=%d hr=0x%08lX r=%p", (unsigned)es->data.committed[0], cLen, (unsigned long)hrIns, (void*)r);
            if (SUCCEEDED(hrIns) && r) {
                MoveCaretToEnd(ctx, ec, r);   // 커서를 삽입 글자 뒤로 (역순 방지)
                r->lpVtbl->Release(r);
            }
            if (FAILED(hrIns)) hrOut = hrIns;
            pIns->lpVtbl->Release(pIns);
        }
    }
    CaptureCaretRect(es->pService, ctx, ec);   // 삽입 '후' 캐럿 = 조합 미리보기가 뜰 자리
    return hrOut;
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
    // 바깥 hr(요청 접수)만 반환하면 DoEditSession 내부 실패가 숨는다 → 세션 hr까지 전파 (RFC-0004 P2-2)
    return FAILED(hr) ? hr : hrSession;
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
    return FAILED(hr) ? hr : hrSession;   // 세션 내부 실패까지 전파 (RFC-0004 P2-2)
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
    return FAILED(hr) ? hr : hrSession;   // 세션 내부 실패까지 전파 (RFC-0004 P2-2)
}

// ----------------------------------------------------
// [RFC-0003] 선택(블록) 텍스트 읽기 세션 — 선택 후 한자키 변환용.
//   선택 교체는 이후 InsertTextAtSelection(삽입)이 자동 수행하므로(타이핑 덮어쓰기와 동일 경로)
//   커밋 전용 원칙과 호환. 여기서는 읽기 + 후보창 위치용 rect 캡처만 한다.
// ----------------------------------------------------
typedef struct {
    ITfEditSessionVtbl *lpVtbl;
    LONG refCount;
    JamotongTextService *pService;
    ITfContext *pContext;
    wchar_t *outBuf;
    int maxLen;
} ReadSelSession;

static HRESULT STDMETHODCALLTYPE RSel_QueryInterface(ITfEditSession *pThis, REFIID riid, void **ppvObject) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfEditSession)) {
        *ppvObject = pThis; pThis->lpVtbl->AddRef(pThis); return S_OK;
    }
    *ppvObject = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE RSel_AddRef(ITfEditSession *pThis) {
    return InterlockedIncrement(&((ReadSelSession*)pThis)->refCount);
}
static ULONG STDMETHODCALLTYPE RSel_Release(ITfEditSession *pThis) {
    ReadSelSession *es = (ReadSelSession*)pThis;
    ULONG res = InterlockedDecrement(&es->refCount);
    if (res == 0) {
        es->pService->lpVtblTIP->Release((ITfTextInputProcessor*)es->pService);
        es->pContext->lpVtbl->Release(es->pContext);
        HeapFree(GetProcessHeap(), 0, es);
    }
    return res;
}
static HRESULT STDMETHODCALLTYPE RSel_DoEditSession(ITfEditSession *pThis, TfEditCookie ec) {
    ReadSelSession *es = (ReadSelSession*)pThis;
    JamotongTextService *svc = es->pService;
    ITfContext *ctx = es->pContext;
    es->outBuf[0] = L'\0';
    svc->lastCaretValid = FALSE;

    TF_SELECTION sel; ULONG fetched = 0;
    if (FAILED(ctx->lpVtbl->GetSelection(ctx, ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) || fetched == 0)
        return S_OK;

    ULONG copied = 0;
    sel.range->lpVtbl->GetText(sel.range, ec, 0, es->outBuf, (ULONG)es->maxLen, &copied);
    es->outBuf[copied] = L'\0';   // 빈 선택(접힌 캐럿)이면 copied=0

    if (copied > 0) {   // 후보창 위치 = 선택 range의 화면 rect (실패 시 호출자가 GUIThreadInfo 폴백)
        ITfContextView *pView = NULL;
        if (SUCCEEDED(ctx->lpVtbl->GetActiveView(ctx, &pView)) && pView) {
            RECT rc; BOOL clipped = FALSE;
            if (SUCCEEDED(pView->lpVtbl->GetTextExt(pView, ec, sel.range, &rc, &clipped))
                && (rc.bottom - rc.top > 0)) {
                svc->lastCaretRect = rc;
                svc->lastCaretValid = TRUE;
            }
            pView->lpVtbl->Release(pView);
        }
    }
    sel.range->lpVtbl->Release(sel.range);
    return S_OK;
}
static ITfEditSessionVtbl ReadSelVtbl = { RSel_QueryInterface, RSel_AddRef, RSel_Release, RSel_DoEditSession };

// CUAS 폴백 (실기 2026-07-08: 레거시 EDIT 계열은 선택을 TSF GetSelection으로 노출하지 않아
// 블록 한자 변환이 무동작이었다). 포커스 컨트롤에서 EM_GETSEL + WM_GETTEXT로 직접 읽는다 —
// EDIT/RichEdit/AkelEdit 등 EDIT 호환 컨트롤에서 동작하고, 그 외 컨트롤은 검증(s<e·범위)에
// 걸려 무해하게 빈손 반환. 같은 스레드의 포커스 창이므로 SendMessage 계열은 안전.
// ①차 폴백: RichEdit 계열(AkelEdit 포함) — EM_EXGETSEL(CHARRANGE)+EM_GETSELTEXT.
//   컨트롤이 선택 텍스트를 '직접' 복사해 주므로 오프셋 해석(문자/바이트·개행 축약) 차이에서
//   자유롭다. (실기 2026-07-08: EM_GETSEL+WM_GETTEXT 조합은 레거시 앱에서 "대한민국" 선택이
//   "대한"으로 잘리는 오프셋 불일치를 보임.) 모르는 컨트롤은 CHARRANGE를 안 건드려 걸러진다.
static bool ReadSelViaRichEdit(HWND h, wchar_t *outBuf, int maxLen) {
    CHARRANGE cr; cr.cpMin = -2; cr.cpMax = -2;   // 미응답 감지용 초기값
    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);
    if (cr.cpMin < 0 || cr.cpMax <= cr.cpMin) return false;
    if (cr.cpMax - cr.cpMin > maxLen) return false;   // 사전 조회 상한 초과
    // EM_GETSELTEXT는 버퍼 크기 인자가 없는 고전 API — 선택 길이를 위에서 상한(≤maxLen≤16)
    // 검증했고, CRLF 확장 등 여유를 위해 넉넉한 지역 버퍼에 받은 뒤 길이를 재검증한다.
    wchar_t buf[256]; buf[0] = L'\0';
    LRESULT n = SendMessageW(h, EM_GETSELTEXT, 0, (LPARAM)buf);
    if (n <= 0 || n > maxLen) return false;
    buf[n] = L'\0';
    wmemcpy(outBuf, buf, (size_t)n + 1);
    return true;
}

// 컨트롤 h의 현재 선택 텍스트 읽기: ① RichEdit 정확 경로 ② 플레인 EDIT(EM_GETSEL=문자 단위)
static bool ReadSelFromCtl(HWND h, wchar_t *outBuf, int maxLen) {
    outBuf[0] = L'\0';
    if (ReadSelViaRichEdit(h, outBuf, maxLen)) return true;
    DWORD s = 0, e = 0;
    SendMessageW(h, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (e <= s || (int)(e - s) > maxLen) return false;   // 선택 없음(비-EDIT 포함)/사전 상한 초과
    if (e > 262144) return false;                        // 과대 문서 보호 (전체 텍스트 복사 상한 512KB)
    int total = GetWindowTextLengthW(h);
    if (total <= 0 || (DWORD)total < e) return false;    // EM_GETSEL 응답이 텍스트와 불일치(비-EDIT 방어)
    wchar_t *buf = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, ((size_t)e + 1) * sizeof(wchar_t));
    if (!buf) return false;
    int got = GetWindowTextW(h, buf, (int)e + 1);        // 선택 끝까지만 복사
    if ((DWORD)got >= e) {
        DWORD n = e - s;
        wmemcpy(outBuf, buf + s, n);
        outBuf[n] = L'\0';
    }
    HeapFree(GetProcessHeap(), 0, buf);
    return outBuf[0] != L'\0';
}

static void ReadSelectionFromFocusCtl(wchar_t *outBuf, int maxLen) {
    GUITHREADINFO gti; memset(&gti, 0, sizeof(gti)); gti.cbSize = sizeof(gti);
    if (!GetGUIThreadInfo(0, &gti) || !gti.hwndFocus) return;
    ReadSelFromCtl(gti.hwndFocus, outBuf, maxLen);
}

// 캐럿 앞의 단어(word)를 EDIT 메시지로 '프로그램적으로 선택'하고 읽어서 검증한다.
// 성공하면 선택이 잡힌 채 반환 → 이어지는 InsertTextAtSelection 삽입이 선택을 교체한다
// (= 검증된 CUAS 호환 경로). TSF range 교체(ShiftStart+SetText)는 CUAS에서 부분 적용되어
// "대한민국→大韓민국"처럼 앞 글자만 바뀌는 오동작을 보였다(실기 2026-07-08).
// 오프셋 단위(문자/바이트)를 신뢰하지 않는다: 선택 후 실제 텍스트를 기대 단어와 대조하고,
// 불일치하면 문자당 2단위(바이트 해석)도 시도, 그래도 아니면 캐럿을 복원하고 실패한다.
// 컨트롤 h에서 [from,to)를 선택. RichEdit(EM_EXSETSEL)와 플레인 EDIT(EM_SETSEL) 모두 커버.
static void CtlSetSel(HWND h, LONG from, LONG to, bool rich) {
    if (rich) { CHARRANGE cr; cr.cpMin = from; cr.cpMax = to; SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&cr); }
    else SendMessageW(h, EM_SETSEL, (WPARAM)from, (LPARAM)to);
}

bool EditCtl_SelectWordBeforeCaret(const wchar_t *word) {
    int len = (int)wcslen(word);
    if (len <= 0 || len > 16) return false;
    GUITHREADINFO gti; memset(&gti, 0, sizeof(gti)); gti.cbSize = sizeof(gti);
    if (!GetGUIThreadInfo(0, &gti) || !gti.hwndFocus) return false;
    HWND h = gti.hwndFocus;
    // 캐럿 위치: EM_EXGETSEL(RichEdit/AkelEdit) 우선 — AkelEdit는 EM_GETSEL 미응답이라
    // EM_GETSEL만 쓰면 단어 선택이 통째로 실패했다(실기 2026-07-08).
    LONG caret = -1; bool rich = false;
    CHARRANGE cr; cr.cpMin = -2; cr.cpMax = -2;
    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);
    if (cr.cpMin >= 0 && cr.cpMin == cr.cpMax) { caret = cr.cpMin; rich = true; }
    else {
        DWORD s = 0xFFFFFFFF, e = 0xFFFFFFFF;
        SendMessageW(h, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
        if (s == 0xFFFFFFFF || s != e) return false;   // 비-EDIT/캐럿이 접혀있지 않음
        caret = (LONG)e;
    }
    if (caret <= 0) return false;
    for (int mult = 1; mult <= 2; mult++) {   // 1=문자 오프셋, 2=UTF-16 코드유닛(2단위) 해석
        LONG span = (LONG)len * mult;
        if (caret < span) continue;
        CtlSetSel(h, caret - span, caret, rich);
        wchar_t got[24] = {0};
        if (ReadSelFromCtl(h, got, 16) && wcscmp(got, word) == 0)
            return true;   // 검증 성공 — 선택 유지한 채 반환(호출자가 EM_REPLACESEL 교체)
    }
    CtlSetSel(h, caret, caret, rich);   // 검증 실패 → 캐럿 복원
    return false;
}

// 포커스 EDIT 계열 컨트롤의 현재 선택을 str로 교체(EM_REPLACESEL, undo 가능).
// EDIT의 표준 동작이라 선택 전체가 정확히 교체된다 — TSF InsertTextAtSelection이 CUAS에서
// 선택을 앞 글자만 부분 교체하던 문제("대한민국"→"大韓민국")의 정공 우회. 비-EDIT면 false.
bool EditCtl_ReplaceSelection(const wchar_t *str) {
    GUITHREADINFO gti; memset(&gti, 0, sizeof(gti)); gti.cbSize = sizeof(gti);
    if (!GetGUIThreadInfo(0, &gti) || !gti.hwndFocus) return false;
    HWND h = gti.hwndFocus;
    // EDIT 판정은 EM_EXGETSEL(RichEdit/AkelEdit) 우선 — AkelEdit는 EM_GETSEL에 제대로 응답하지
    // 않아, EM_GETSEL만 쓰면 판정 실패 → TSF 삽입 폴백 → CUAS 부분교체(4글자→2글자)로 이어졌다
    // (실기 2026-07-08). 선택 읽기는 이미 EM_EXGETSEL로 성공하던 컨트롤이다.
    CHARRANGE cr; cr.cpMin = -2; cr.cpMax = -2;
    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);
    if (cr.cpMin >= 0 && cr.cpMax >= cr.cpMin) {
        SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)str);
        JamoDiag("REPLACESEL exsel cp=%ld..%ld len=%d", cr.cpMin, cr.cpMax, (int)wcslen(str));
        return true;
    }
    DWORD s = 0xFFFFFFFF, e = 0xFFFFFFFF;
    SendMessageW(h, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (s == 0xFFFFFFFF || e == 0xFFFFFFFF) return false;   // 둘 다 미응답 = 비-EDIT
    SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)str);
    JamoDiag("REPLACESEL getsel %lu..%lu len=%d", (unsigned long)s, (unsigned long)e, (int)wcslen(str));
    return true;
}

HRESULT RequestReadSelectionString(JamotongTextService *pService, ITfContext *pContext, wchar_t *outBuf, int maxLen) {
    ReadSelSession *es = (ReadSelSession*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ReadSelSession));
    if (!es) return E_OUTOFMEMORY;
    es->lpVtbl = &ReadSelVtbl; es->refCount = 1; es->pService = pService; es->pContext = pContext;
    es->outBuf = outBuf; es->maxLen = maxLen;
    outBuf[0] = L'\0';
    pService->lpVtblTIP->AddRef((ITfTextInputProcessor*)pService); pContext->lpVtbl->AddRef(pContext);
    HRESULT hrSession = S_OK;
    HRESULT hr = pContext->lpVtbl->RequestEditSession(pContext, pService->clientId, (ITfEditSession*)es, TF_ES_SYNC | TF_ES_READ, &hrSession);
    es->lpVtbl->Release((ITfEditSession*)es);
    // TSF가 빈손이면(레거시 앱) 포커스 EDIT 컨트롤에서 직접 읽기 — 후보창 위치는 호출자의
    // GUIThreadInfo 캐럿 폴백이 잡는다. 삽입=선택 교체는 EDIT의 표준 동작이라 그대로 성립.
    if (outBuf[0] == L'\0') ReadSelectionFromFocusCtl(outBuf, maxLen);
    return FAILED(hr) ? hr : hrSession;   // 세션 내부 실패까지 전파 (RFC-0004 P2-2)
}
