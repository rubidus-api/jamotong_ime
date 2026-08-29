// tsf-conformance-lab / conf_tip.c
//
// RFC-0012 의 "작은 올바른 구현체". 표준 TSF 계약만 담은 얇은 TIP 이며, 조합 엔진이 없다.
// 목적은 글자를 넣는 것이 아니라 **계약이 서는지 보는 것**이다:
//
//   C1  ITfTextInputProcessorEx::ActivateEx 가 불리는가, 플래그는 무엇인가
//       (Ex 를 구현하면 Activate 는 안 불린다 — 초기화는 한 곳에서)
//   C2  compartment(KEYBOARD_OPENCLOSE / INPUTMODE_CONVERSION) 읽기·쓰기·변경 통지
//   C3  preserved key 등록과 OnPreservedKey 전달 (한/영·한자·명령키)
//   C4  포커스 문서마다 GetStatus 의 static flag (TS_SS_TRANSITORY 여부)
//   C5  단명 문서에서 TRANSITORYEXTENSION_PARENT 가 실제로 채워져 있는가 (가설 H-T1)
//
// 모든 관측은 %TEMP%\jamotong-conf-lab.log 에 한 줄씩 남는다. 제품과 CLSID·상태·소스를
// 공유하지 않는다(RFC-0009 규율). 여기서 증거가 나온 계약만 제품으로 옮긴다.

#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <msctf.h>
#include <olectl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include "tsf_missing_guids.h"

// lab 전용 CLSID/프로파일 — 제품과 절대 겹치지 않는다.
// {C0NF1AB0-...} 형태의 임의 값.
static const GUID CLSID_ConfLabTip =
    { 0xc0f1ab01, 0x7d33, 0x4a2e, { 0x9b, 0x21, 0x0e, 0x77, 0x35, 0x1c, 0x44, 0x10 } };
static const GUID GUID_ConfLabProfile =
    { 0xc0f1ab02, 0x7d33, 0x4a2e, { 0x9b, 0x21, 0x0e, 0x77, 0x35, 0x1c, 0x44, 0x10 } };
// preserved key 명령 GUID
static const GUID GUID_ConfLabCmdRotate =
    { 0xc0f1ab03, 0x7d33, 0x4a2e, { 0x9b, 0x21, 0x0e, 0x77, 0x35, 0x1c, 0x44, 0x10 } };
static const GUID GUID_ConfLabCmdHanja =
    { 0xc0f1ab04, 0x7d33, 0x4a2e, { 0x9b, 0x21, 0x0e, 0x77, 0x35, 0x1c, 0x44, 0x10 } };

static HINSTANCE g_hInst;
static LONG      g_dllRef;

// ---------------------------------------------------------------- 로그

static void logf_(const char *fmt, ...)
{
    char path[MAX_PATH], line[1024];
    va_list ap;
    DWORD n = GetTempPathA(MAX_PATH, path);
    if (!n || n >= MAX_PATH) return;
    strcat_s(path, sizeof path, "jamotong-conf-lab.log");

    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);

    if (IsDebuggerPresent()) { OutputDebugStringA(line); OutputDebugStringA("\n"); }

    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        SYSTEMTIME st; GetLocalTime(&st);
        char out[1200];
        int len = snprintf(out, sizeof out, "%02d:%02d:%02d.%03d [%lu] %s\r\n",
                           st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                           GetCurrentThreadId(), line);
        DWORD wrote = 0;
        if (len > 0) WriteFile(h, out, (DWORD)len, &wrote, NULL);
        CloseHandle(h);
    }
}

// ---------------------------------------------------------------- TIP 객체

typedef struct ConfTip ConfTip;

struct ConfTip {
    const ITfTextInputProcessorExVtbl *lpVtblTIPEx;
    ITfKeyEventSinkVtbl          *lpVtblKES;
    ITfThreadMgrEventSinkVtbl    *lpVtblTMES;
    ITfCompartmentEventSinkVtbl  *lpVtblCES;

    LONG            ref;
    ITfThreadMgr   *tm;
    TfClientId      cid;
    DWORD           activateFlags;   // C1
    BOOL            uiless;          // TF_TMAE_UIELEMENTENABLEDONLY 로 활성화된 스레드인가

    DWORD           cookieKeySink;
    DWORD           cookieThreadSink;
    DWORD           cookieOpenClose;
    ITfCompartment *cpOpenClose;
    BOOL            preservedOk[2];
};

#define TIP_FROM_EX(p)   ((ConfTip *)(p))
#define TIP_FROM_KES(p)  ((ConfTip *)((char *)(p) - offsetof(ConfTip, lpVtblKES)))
#define TIP_FROM_TMES(p) ((ConfTip *)((char *)(p) - offsetof(ConfTip, lpVtblTMES)))
#define TIP_FROM_CES(p)  ((ConfTip *)((char *)(p) - offsetof(ConfTip, lpVtblCES)))

static HRESULT tip_QI(ConfTip *me, REFIID riid, void **ppv)
{
    if (!ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_ITfTextInputProcessor) ||
        IsEqualIID(riid, &IID_ITfTextInputProcessorEx_L))
        *ppv = &me->lpVtblTIPEx;
    else if (IsEqualIID(riid, &IID_ITfKeyEventSink))
        *ppv = &me->lpVtblKES;
    else if (IsEqualIID(riid, &IID_ITfThreadMgrEventSink))
        *ppv = &me->lpVtblTMES;
    else if (IsEqualIID(riid, &IID_ITfCompartmentEventSink))
        *ppv = &me->lpVtblCES;
    else
        return E_NOINTERFACE;

    InterlockedIncrement(&me->ref);
    return S_OK;
}

static ULONG tip_AddRef(ConfTip *me) { return InterlockedIncrement(&me->ref); }

static ULONG tip_Release(ConfTip *me)
{
    LONG r = InterlockedDecrement(&me->ref);
    if (r == 0) {
        free(me);
        InterlockedDecrement(&g_dllRef);
    }
    return (ULONG)r;
}

// ---------------------------------------------------------------- C4/C5: 문서 관찰

// 이 문맥이 단명(TS_SS_TRANSITORY)인가를 GetStatus 로 본다. 앱 이름을 보지 않는다.
static void observe_context(ITfContext *ctx, const char *tag)
{
    TF_STATUS st = { 0 };
    HRESULT hr = ITfContext_GetStatus(ctx, &st);
    if (FAILED(hr)) {
        logf_("C4 %s GetStatus FAILED hr=0x%08lX", tag, (unsigned long)hr);
        return;
    }
    logf_("C4 %s static=0x%08lX dynamic=0x%08lX transitory=%s",
          tag, (unsigned long)st.dwStaticFlags, (unsigned long)st.dwDynamicFlags,
          (st.dwStaticFlags & TS_SS_TRANSITORY) ? "YES" : "no");
}

// H-T1: 단명 문서의 문서관리자 compartment 에 부모 ITfDocumentMgr 가 실제로 있는가.
// 있으면 RFC-0010 분기 위에 "부모 문맥 사용" 이라는 선택지가 생긴다. 없으면 현행이 최선임이 확정.
static void observe_transitory_parent(ITfDocumentMgr *dm)
{
    ITfCompartmentMgr *cm = NULL;
    HRESULT hr = ITfDocumentMgr_QueryInterface(dm, &IID_ITfCompartmentMgr, (void **)&cm);
    if (FAILED(hr) || !cm) {
        logf_("C5 documentmgr has no ITfCompartmentMgr hr=0x%08lX", (unsigned long)hr);
        return;
    }

    struct { const GUID *g; const char *label; } probe[] = {
        { &GUID_COMPARTMENT_TRANSITORYEXTENSION_L_UNVERIFIED,       "TRANSITORYEXTENSION?" },
        { &GUID_COMPARTMENT_TRANSITORYEXTENSION_PARENT_L,           "PARENT" },
        { &GUID_COMPARTMENT_TRANSITORYEXTENSION_DOCUMENTMANAGER_L,  "DOCMGR" },
    };

    for (size_t i = 0; i < sizeof probe / sizeof probe[0]; i++) {
        ITfCompartment *cp = NULL;
        hr = ITfCompartmentMgr_GetCompartment(cm, probe[i].g, &cp);
        if (FAILED(hr) || !cp) {
            logf_("C5 %s GetCompartment hr=0x%08lX", probe[i].label, (unsigned long)hr);
            continue;
        }
        VARIANT v; VariantInit(&v);
        HRESULT hv = ITfCompartment_GetValue(cp, &v);
        // hv == S_FALSE 면 값이 없다 = 아무도 안 채웠다.
        logf_("C5 %s value hr=0x%08lX vt=%d %s", probe[i].label, (unsigned long)hv, (int)v.vt,
              (hv == S_OK && (v.vt == VT_UNKNOWN || v.vt == VT_I4)) ? "PRESENT" : "empty");
        VariantClear(&v);
        ITfCompartment_Release(cp);
    }
    ITfCompartmentMgr_Release(cm);
}

// ---------------------------------------------------------------- C2: compartment

static HRESULT compartment_get(ITfThreadMgr *tm, const GUID *guid, ITfCompartment **out)
{
    ITfCompartmentMgr *cm = NULL;
    HRESULT hr = ITfThreadMgr_QueryInterface(tm, &IID_ITfCompartmentMgr, (void **)&cm);
    if (FAILED(hr)) return hr;
    hr = ITfCompartmentMgr_GetCompartment(cm, guid, out);
    ITfCompartmentMgr_Release(cm);
    return hr;
}

static void compartment_log(ITfCompartment *cp, const char *label)
{
    VARIANT v; VariantInit(&v);
    HRESULT hr = ITfCompartment_GetValue(cp, &v);
    logf_("C2 %s hr=0x%08lX vt=%d val=%ld", label, (unsigned long)hr, (int)v.vt, (long)v.lVal);
    VariantClear(&v);
}

static void setup_compartments(ConfTip *me)
{
    // 한/영 상태 = KEYBOARD_OPENCLOSE. 표준 자리. (제품의 HKCU 채널을 대체할 후보)
    HRESULT hr = compartment_get(me->tm, &GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &me->cpOpenClose);
    logf_("C2 get[KEYBOARD_OPENCLOSE] hr=0x%08lX", (unsigned long)hr);
    if (FAILED(hr) || !me->cpOpenClose) return;

    compartment_log(me->cpOpenClose, "openclose.initial");

    // 변경 통지 구독 — 시스템/앱이 상태를 바꾸면 우리가 안다(폴링 불필요).
    ITfSource *src = NULL;
    hr = ITfCompartment_QueryInterface(me->cpOpenClose, &IID_ITfSource, (void **)&src);
    if (SUCCEEDED(hr) && src) {
        hr = ITfSource_AdviseSink(src, &IID_ITfCompartmentEventSink,
                                  (IUnknown *)&me->lpVtblCES, &me->cookieOpenClose);
        logf_("C2 advise[KEYBOARD_OPENCLOSE] hr=0x%08lX", (unsigned long)hr);
        ITfSource_Release(src);
    }

    // 변환 모드(한글/영문·전각 등)를 IMM32 와 같은 뜻으로 노출한다.
    ITfCompartment *conv = NULL;
    hr = compartment_get(me->tm, &GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION_L, &conv);
    logf_("C2 get[INPUTMODE_CONVERSION] hr=0x%08lX", (unsigned long)hr);
    if (SUCCEEDED(hr) && conv) {
        VARIANT v; VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = TF_CONVERSIONMODE_NATIVE;      // 한글 모드
        hr = ITfCompartment_SetValue(conv, me->cid, &v);
        logf_("C2 set[INPUTMODE_CONVERSION=NATIVE] hr=0x%08lX", (unsigned long)hr);
        ITfCompartment_Release(conv);
    }
}

// ---------------------------------------------------------------- C3: preserved key

static void setup_preserved_keys(ConfTip *me)
{
    ITfKeystrokeMgr *km = NULL;
    HRESULT hr = ITfThreadMgr_QueryInterface(me->tm, &IID_ITfKeystrokeMgr, (void **)&km);
    if (FAILED(hr) || !km) {
        logf_("C3 QI[ITfKeystrokeMgr] hr=0x%08lX", (unsigned long)hr);
        return;
    }

    // 명령키만 preserved key 로 예약한다. 조합 상태에 따라 뜻이 달라지는 키
    // (Enter/Esc/BackSpace)는 여기 넣지 않는다 — 그건 key event sink 의 몫이다.
    static const WCHAR descRotate[] = L"conf-lab: toggle hangul/english";
    static const WCHAR descHanja[]  = L"conf-lab: hanja conversion";

    TF_PRESERVEDKEY pkRotate = { VK_SPACE, TF_MOD_SHIFT };
    hr = ITfKeystrokeMgr_PreserveKey(km, me->cid, &GUID_ConfLabCmdRotate, &pkRotate,
                                     descRotate, (ULONG)(ARRAYSIZE(descRotate) - 1));
    me->preservedOk[0] = SUCCEEDED(hr);
    logf_("C3 PreserveKey[Shift+Space] hr=0x%08lX", (unsigned long)hr);

    // 하드웨어 한자키. 드라이버/레이아웃 단계에서 이미 소비될 수 있다 — 그것이 이 시험의 요점.
    TF_PRESERVEDKEY pkHanja = { VK_HANJA, 0 };
    hr = ITfKeystrokeMgr_PreserveKey(km, me->cid, &GUID_ConfLabCmdHanja, &pkHanja,
                                     descHanja, (ULONG)(ARRAYSIZE(descHanja) - 1));
    me->preservedOk[1] = SUCCEEDED(hr);
    logf_("C3 PreserveKey[VK_HANJA] hr=0x%08lX", (unsigned long)hr);

    ITfKeystrokeMgr_Release(km);
}

static void teardown_preserved_keys(ConfTip *me)
{
    ITfKeystrokeMgr *km = NULL;
    if (FAILED(ITfThreadMgr_QueryInterface(me->tm, &IID_ITfKeystrokeMgr, (void **)&km)) || !km)
        return;
    if (me->preservedOk[0]) {
        TF_PRESERVEDKEY pk = { VK_SPACE, TF_MOD_SHIFT };
        ITfKeystrokeMgr_UnpreserveKey(km, &GUID_ConfLabCmdRotate, &pk);
    }
    if (me->preservedOk[1]) {
        TF_PRESERVEDKEY pk = { VK_HANJA, 0 };
        ITfKeystrokeMgr_UnpreserveKey(km, &GUID_ConfLabCmdHanja, &pk);
    }
    ITfKeystrokeMgr_Release(km);
}

// ---------------------------------------------------------------- ITfTextInputProcessorEx

static HRESULT STDMETHODCALLTYPE ex_QI(ITfTextInputProcessorEx *me, REFIID riid, void **ppv)
{ return tip_QI(TIP_FROM_EX(me), riid, ppv); }
static ULONG STDMETHODCALLTYPE ex_AddRef(ITfTextInputProcessorEx *me)
{ return tip_AddRef(TIP_FROM_EX(me)); }
static ULONG STDMETHODCALLTYPE ex_Release(ITfTextInputProcessorEx *me)
{ return tip_Release(TIP_FROM_EX(me)); }

static HRESULT tip_activate_common(ConfTip *me, ITfThreadMgr *tm, TfClientId cid, DWORD flags)
{
    me->tm = tm;
    ITfThreadMgr_AddRef(tm);
    me->cid = cid;
    me->activateFlags = flags;
    me->uiless = (flags & TF_TMAE_UIELEMENTENABLEDONLY) ? TRUE : FALSE;

    logf_("C1 activate cid=%u flags=0x%08lX uiless=%s",
          (unsigned)cid, (unsigned long)flags, me->uiless ? "YES" : "no");

    // 키 이벤트 sink
    ITfKeystrokeMgr *km = NULL;
    HRESULT hr = ITfThreadMgr_QueryInterface(tm, &IID_ITfKeystrokeMgr, (void **)&km);
    if (SUCCEEDED(hr) && km) {
        hr = ITfKeystrokeMgr_AdviseKeyEventSink(km, cid, (ITfKeyEventSink *)&me->lpVtblKES, TRUE);
        logf_("C3 AdviseKeyEventSink hr=0x%08lX", (unsigned long)hr);
        ITfKeystrokeMgr_Release(km);
    }

    // 스레드 매니저 사건 sink (포커스 문서 관찰 = C4/C5)
    ITfSource *src = NULL;
    hr = ITfThreadMgr_QueryInterface(tm, &IID_ITfSource, (void **)&src);
    if (SUCCEEDED(hr) && src) {
        hr = ITfSource_AdviseSink(src, &IID_ITfThreadMgrEventSink,
                                  (IUnknown *)&me->lpVtblTMES, &me->cookieThreadSink);
        logf_("C4 AdviseSink[ThreadMgrEventSink] hr=0x%08lX", (unsigned long)hr);
        ITfSource_Release(src);
    }

    setup_compartments(me);
    setup_preserved_keys(me);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ex_ActivateEx(ITfTextInputProcessorEx *me,
                                               ITfThreadMgr *tm, TfClientId cid, DWORD flags)
{ return tip_activate_common(TIP_FROM_EX(me), tm, cid, flags); }

// ITfTextInputProcessorEx 를 구현하면 이쪽은 안 불려야 한다. 불리면 그 사실 자체가 관측이다.
static HRESULT STDMETHODCALLTYPE ex_Activate(ITfTextInputProcessorEx *me,
                                             ITfThreadMgr *tm, TfClientId cid)
{
    logf_("C1 !! Activate (non-Ex) called — 이 호스트는 Ex 를 안 쓴다");
    return tip_activate_common(TIP_FROM_EX(me), tm, cid, 0);
}

static HRESULT STDMETHODCALLTYPE ex_Deactivate(ITfTextInputProcessorEx *pme)
{
    ConfTip *me = TIP_FROM_EX(pme);
    logf_("C1 deactivate");

    if (me->tm) {
        teardown_preserved_keys(me);

        ITfKeystrokeMgr *km = NULL;
        if (SUCCEEDED(ITfThreadMgr_QueryInterface(me->tm, &IID_ITfKeystrokeMgr, (void **)&km)) && km) {
            ITfKeystrokeMgr_UnadviseKeyEventSink(km, me->cid);
            ITfKeystrokeMgr_Release(km);
        }
        ITfSource *src = NULL;
        if (me->cookieThreadSink &&
            SUCCEEDED(ITfThreadMgr_QueryInterface(me->tm, &IID_ITfSource, (void **)&src)) && src) {
            ITfSource_UnadviseSink(src, me->cookieThreadSink);
            ITfSource_Release(src);
            me->cookieThreadSink = 0;
        }
        if (me->cpOpenClose) {
            if (me->cookieOpenClose) {
                ITfSource *cs = NULL;
                if (SUCCEEDED(ITfCompartment_QueryInterface(me->cpOpenClose, &IID_ITfSource,
                                                            (void **)&cs)) && cs) {
                    ITfSource_UnadviseSink(cs, me->cookieOpenClose);
                    ITfSource_Release(cs);
                }
                me->cookieOpenClose = 0;
            }
            ITfCompartment_Release(me->cpOpenClose);
            me->cpOpenClose = NULL;
        }
        ITfThreadMgr_Release(me->tm);
        me->tm = NULL;
    }
    return S_OK;
}

static const ITfTextInputProcessorExVtbl g_exVtbl = {
    ex_QI, ex_AddRef, ex_Release, ex_Activate, ex_Deactivate, ex_ActivateEx
};

// ---------------------------------------------------------------- ITfKeyEventSink

static HRESULT STDMETHODCALLTYPE kes_QI(ITfKeyEventSink *me, REFIID riid, void **ppv)
{ return tip_QI(TIP_FROM_KES(me), riid, ppv); }
static ULONG STDMETHODCALLTYPE kes_AddRef(ITfKeyEventSink *me)
{ return tip_AddRef(TIP_FROM_KES(me)); }
static ULONG STDMETHODCALLTYPE kes_Release(ITfKeyEventSink *me)
{ return tip_Release(TIP_FROM_KES(me)); }

static HRESULT STDMETHODCALLTYPE kes_OnSetFocus(ITfKeyEventSink *me, BOOL fForeground)
{ (void)me; logf_("C3 OnSetFocus fg=%d", (int)fForeground); return S_OK; }

// 이 lab 은 글자를 넣지 않는다 — 무엇이 오는지만 본다. 그래서 아무 키도 먹지 않는다.
static HRESULT STDMETHODCALLTYPE kes_OnTestKeyDown(ITfKeyEventSink *me, ITfContext *ctx,
                                                   WPARAM w, LPARAM l, BOOL *pfEaten)
{ (void)me; (void)ctx; (void)l; (void)w; if (pfEaten) *pfEaten = FALSE;
  // 키마다 로그하지 않는다 — 호스트 UI 스레드에서 매 키 파일 열기는 비용이고, 가상키 기록은
  // 비밀번호 입력까지 남긴다. 싱크 생존은 첫 호출 1회로 증명.
  static LONG s_once; if (InterlockedCompareExchange(&s_once, 1, 0) == 0) logf_("C3 OnTestKeyDown sink alive");
  return S_OK; }

static HRESULT STDMETHODCALLTYPE kes_OnKeyDown(ITfKeyEventSink *me, ITfContext *ctx,
                                               WPARAM w, LPARAM l, BOOL *pfEaten)
{ (void)me; (void)ctx; (void)l; (void)w; if (pfEaten) *pfEaten = FALSE; return S_OK; }

static HRESULT STDMETHODCALLTYPE kes_OnTestKeyUp(ITfKeyEventSink *me, ITfContext *ctx,
                                                 WPARAM w, LPARAM l, BOOL *pfEaten)
{ (void)me; (void)ctx; (void)w; (void)l; if (pfEaten) *pfEaten = FALSE; return S_OK; }

static HRESULT STDMETHODCALLTYPE kes_OnKeyUp(ITfKeyEventSink *me, ITfContext *ctx,
                                             WPARAM w, LPARAM l, BOOL *pfEaten)
{ (void)me; (void)ctx; (void)w; (void)l; if (pfEaten) *pfEaten = FALSE; return S_OK; }

// C3 의 핵심 관측: 예약한 명령키가 여기로 오는가 (= SendInput 재전송 우회로를 지울 수 있는가)
static HRESULT STDMETHODCALLTYPE kes_OnPreservedKey(ITfKeyEventSink *me, ITfContext *ctx,
                                                    REFGUID rguid, BOOL *pfEaten)
{
    (void)ctx;
    ConfTip *tip = TIP_FROM_KES(me);
    const char *which = IsEqualGUID(rguid, &GUID_ConfLabCmdRotate) ? "rotate"
                      : IsEqualGUID(rguid, &GUID_ConfLabCmdHanja)  ? "hanja"
                      : "unknown";
    logf_("C3 OnPreservedKey [%s]", which);

    // 한/영 토글을 compartment 로 표현해 본다 — 상태의 표준 자리(§RFC-0012 Phase 1).
    if (tip->cpOpenClose && IsEqualGUID(rguid, &GUID_ConfLabCmdRotate)) {
        VARIANT v; VariantInit(&v);
        if (SUCCEEDED(ITfCompartment_GetValue(tip->cpOpenClose, &v))) {
            LONG cur = (v.vt == VT_I4) ? v.lVal : 0;
            VariantClear(&v);
            VARIANT nv; VariantInit(&nv); nv.vt = VT_I4; nv.lVal = cur ? 0 : 1;
            HRESULT hr = ITfCompartment_SetValue(tip->cpOpenClose, tip->cid, &nv);
            logf_("C2 openclose %ld -> %ld hr=0x%08lX", (long)cur, (long)nv.lVal, (unsigned long)hr);
        }
    }
    if (pfEaten) *pfEaten = TRUE;
    return S_OK;
}

static ITfKeyEventSinkVtbl g_kesVtbl = {
    kes_QI, kes_AddRef, kes_Release, kes_OnSetFocus,
    kes_OnTestKeyDown, kes_OnKeyDown, kes_OnTestKeyUp, kes_OnKeyUp, kes_OnPreservedKey
};

// ---------------------------------------------------------------- ITfThreadMgrEventSink

static HRESULT STDMETHODCALLTYPE tmes_QI(ITfThreadMgrEventSink *me, REFIID riid, void **ppv)
{ return tip_QI(TIP_FROM_TMES(me), riid, ppv); }
static ULONG STDMETHODCALLTYPE tmes_AddRef(ITfThreadMgrEventSink *me)
{ return tip_AddRef(TIP_FROM_TMES(me)); }
static ULONG STDMETHODCALLTYPE tmes_Release(ITfThreadMgrEventSink *me)
{ return tip_Release(TIP_FROM_TMES(me)); }

static HRESULT STDMETHODCALLTYPE tmes_OnInitDocumentMgr(ITfThreadMgrEventSink *me, ITfDocumentMgr *dm)
{ (void)me; (void)dm; return S_OK; }
static HRESULT STDMETHODCALLTYPE tmes_OnUninitDocumentMgr(ITfThreadMgrEventSink *me, ITfDocumentMgr *dm)
{ (void)me; (void)dm; return S_OK; }

static HRESULT STDMETHODCALLTYPE tmes_OnSetFocus(ITfThreadMgrEventSink *me,
                                                 ITfDocumentMgr *dmFocus, ITfDocumentMgr *dmPrev)
{
    (void)me; (void)dmPrev;
    logf_("C4 OnSetFocus dm=%p", (void *)dmFocus);
    if (!dmFocus) return S_OK;

    ITfContext *ctx = NULL;
    if (SUCCEEDED(ITfDocumentMgr_GetTop(dmFocus, &ctx)) && ctx) {
        observe_context(ctx, "focus");
        ITfContext_Release(ctx);
    }
    observe_transitory_parent(dmFocus);   // H-T1
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tmes_OnPushContext(ITfThreadMgrEventSink *me, ITfContext *ctx)
{ (void)me; observe_context(ctx, "push"); return S_OK; }
static HRESULT STDMETHODCALLTYPE tmes_OnPopContext(ITfThreadMgrEventSink *me, ITfContext *ctx)
{ (void)me; (void)ctx; return S_OK; }

static ITfThreadMgrEventSinkVtbl g_tmesVtbl = {
    tmes_QI, tmes_AddRef, tmes_Release,
    tmes_OnInitDocumentMgr, tmes_OnUninitDocumentMgr,
    tmes_OnSetFocus, tmes_OnPushContext, tmes_OnPopContext
};

// ---------------------------------------------------------------- ITfCompartmentEventSink

static HRESULT STDMETHODCALLTYPE ces_QI(ITfCompartmentEventSink *me, REFIID riid, void **ppv)
{ return tip_QI(TIP_FROM_CES(me), riid, ppv); }
static ULONG STDMETHODCALLTYPE ces_AddRef(ITfCompartmentEventSink *me)
{ return tip_AddRef(TIP_FROM_CES(me)); }
static ULONG STDMETHODCALLTYPE ces_Release(ITfCompartmentEventSink *me)
{ return tip_Release(TIP_FROM_CES(me)); }

static HRESULT STDMETHODCALLTYPE ces_OnChange(ITfCompartmentEventSink *me, REFGUID rguid)
{
    ConfTip *tip = TIP_FROM_CES(me);
    (void)rguid;
    if (tip->cpOpenClose) compartment_log(tip->cpOpenClose, "openclose.onchange");
    else logf_("C2 OnChange (no compartment)");
    return S_OK;
}

static ITfCompartmentEventSinkVtbl g_cesVtbl = { ces_QI, ces_AddRef, ces_Release, ces_OnChange };

// ---------------------------------------------------------------- ClassFactory / DLL

static ConfTip *tip_create(void)
{
    ConfTip *t = (ConfTip *)calloc(1, sizeof *t);
    if (!t) return NULL;
    t->lpVtblTIPEx = &g_exVtbl;
    t->lpVtblKES   = &g_kesVtbl;
    t->lpVtblTMES  = &g_tmesVtbl;
    t->lpVtblCES   = &g_cesVtbl;
    t->ref = 1;
    InterlockedIncrement(&g_dllRef);
    return t;
}

typedef struct { IClassFactoryVtbl *lpVtbl; } ConfFactory;

static HRESULT STDMETHODCALLTYPE cf_QI(IClassFactory *me, REFIID riid, void **ppv)
{
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) {
        *ppv = me; return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE cf_AddRef(IClassFactory *me) { (void)me; return 2; }
static ULONG STDMETHODCALLTYPE cf_Release(IClassFactory *me) { (void)me; return 1; }
static HRESULT STDMETHODCALLTYPE cf_CreateInstance(IClassFactory *me, IUnknown *outer,
                                                   REFIID riid, void **ppv)
{
    (void)me;
    if (outer) return CLASS_E_NOAGGREGATION;
    ConfTip *t = tip_create();
    if (!t) return E_OUTOFMEMORY;
    HRESULT hr = tip_QI(t, riid, ppv);
    tip_Release(t);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cf_LockServer(IClassFactory *me, BOOL fLock)
{ (void)me; if (fLock) InterlockedIncrement(&g_dllRef); else InterlockedDecrement(&g_dllRef); return S_OK; }

static IClassFactoryVtbl g_cfVtbl = {
    cf_QI, cf_AddRef, cf_Release, cf_CreateInstance, cf_LockServer
};
static ConfFactory g_factory = { &g_cfVtbl };

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hInst;
        DisableThreadLibraryCalls(hInst);
    }
    return TRUE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    if (!IsEqualCLSID(rclsid, &CLSID_ConfLabTip)) return CLASS_E_CLASSNOTAVAILABLE;
    return cf_QI((IClassFactory *)&g_factory, riid, ppv);
}

HRESULT WINAPI DllCanUnloadNow(void)
{ return g_dllRef > 0 ? S_FALSE : S_OK; }

// --- 등록 ---------------------------------------------------------------------------------
// 카테고리 등록은 **구현한 것만** 한다(RFC-0012 §3 머리말). 이 lab 은 UI 를 안 그리므로
// UIELEMENTENABLED 를 등록하지 않는다 — 능력 선언은 약속이고, 못 지키면 더 크게 깨진다.

static void guid_to_str(const GUID *g, WCHAR *out, size_t cch)
{
    StringFromGUID2(g, out, (int)cch);
}

// ── RFC-0014 E1: 사용자별(HKCU 전용) 등록 실험 ─────────────────────────────────────
// regsvr32 /n /i:user conf_tip.dll  → DllInstall(TRUE, L"user")  (비승격으로 실행하는 것이 실험의 요점)
// COM 을 HKCU\Software\Classes 에, CTF\TIP 트리를 HKCU 에 그대로 복제한다. HKLM 은 일절 건드리지
// 않는다 — 그래야 "입력 전환기가 HKCU 등록을 읽는가"가 순수하게 측정된다.
static LSTATUS RegSetStrW(HKEY root, const WCHAR *sub, const WCHAR *name, const WCHAR *val) {
    HKEY hk; LSTATUS r = RegCreateKeyExW(root, sub, 0, NULL, 0, KEY_WRITE, NULL, &hk, NULL);
    if (r) return r;
    r = RegSetValueExW(hk, name, 0, REG_SZ, (const BYTE*)val, (DWORD)((wcslen(val)+1)*sizeof(WCHAR)));
    RegCloseKey(hk);
    return r;
}
static LSTATUS RegSetDwW(HKEY root, const WCHAR *sub, const WCHAR *name, DWORD val) {
    HKEY hk; LSTATUS r = RegCreateKeyExW(root, sub, 0, NULL, 0, KEY_WRITE, NULL, &hk, NULL);
    if (r) return r;
    r = RegSetValueExW(hk, name, 0, REG_DWORD, (const BYTE*)&val, sizeof val);
    RegCloseKey(hk);
    return r;
}

HRESULT WINAPI DllInstall(BOOL bInstall, LPCWSTR pszCmdLine)
{
    if (!pszCmdLine || _wcsicmp(pszCmdLine, L"user") != 0)
        return E_INVALIDARG;   // 이 DLL 의 DllInstall 은 사용자별 실험 전용

    WCHAR cls[64], prof[64], path[MAX_PATH], sub[512];
    StringFromGUID2(&CLSID_ConfLabTip, cls, 64);
    StringFromGUID2(&GUID_ConfLabProfile, prof, 64);
    if (!GetModuleFileNameW(g_hInst, path, MAX_PATH)) return E_FAIL;
    // GUID_TFCAT_TIP_KEYBOARD 문자열 (msctf 공개 값)
    static const WCHAR kCatKeyboard[] = L"{34745C63-B2F0-4784-8B67-5E12C8701A31}";

    logf_("E1 DllInstall install=%d (per-user)", (int)bInstall);

    if (bInstall) {
        // COM (HKCU Classes — COM 이 HKLM 위에 병합)
        swprintf(sub, 512, L"Software\\Classes\\CLSID\\%s", cls);
        RegSetStrW(HKEY_CURRENT_USER, sub, NULL, L"jamotong conf-lab TIP (per-user)");
        swprintf(sub, 512, L"Software\\Classes\\CLSID\\%s\\InProcServer32", cls);
        RegSetStrW(HKEY_CURRENT_USER, sub, NULL, path);
        RegSetStrW(HKEY_CURRENT_USER, sub, L"ThreadingModel", L"Apartment");
        // CTF\TIP 트리 (HKLM 구조를 HKCU 에 복제 — 전환기가 읽는지가 실험)
        swprintf(sub, 512, L"Software\\Microsoft\\CTF\\TIP\\%s\\LanguageProfile\\0x00000412\\%s", cls, prof);
        RegSetStrW(HKEY_CURRENT_USER, sub, L"Description", L"jamotong conf-lab (per-user)");
        RegSetDwW (HKEY_CURRENT_USER, sub, L"Enable", 1);
        swprintf(sub, 512, L"Software\\Microsoft\\CTF\\TIP\\%s\\Category\\Category\\%s\\%s", cls, kCatKeyboard, cls);
        RegSetStrW(HKEY_CURRENT_USER, sub, NULL, L"");
        swprintf(sub, 512, L"Software\\Microsoft\\CTF\\TIP\\%s\\Category\\Item\\%s\\%s", cls, cls, kCatKeyboard);
        RegSetStrW(HKEY_CURRENT_USER, sub, NULL, L"");
        logf_("E1 per-user registration written (HKCU only)");
        return S_OK;
    } else {
        swprintf(sub, 512, L"Software\\Microsoft\\CTF\\TIP\\%s", cls);
        RegDeleteTreeW(HKEY_CURRENT_USER, sub);
        swprintf(sub, 512, L"Software\\Classes\\CLSID\\%s", cls);
        RegDeleteTreeW(HKEY_CURRENT_USER, sub);
        logf_("E1 per-user registration removed");
        return S_OK;
    }
}

HRESULT WINAPI DllRegisterServer(void)
{
    WCHAR clsidStr[64], key[256], path[MAX_PATH];
    HKEY hk = NULL, hk2 = NULL;

    if (!GetModuleFileNameW(g_hInst, path, MAX_PATH)) return E_FAIL;
    guid_to_str(&CLSID_ConfLabTip, clsidStr, ARRAYSIZE(clsidStr));
    swprintf(key, ARRAYSIZE(key), L"CLSID\\%s", clsidStr);

    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, key, 0, NULL, 0, KEY_WRITE, NULL, &hk, NULL))
        return SELFREG_E_CLASS;
    RegSetValueExW(hk, NULL, 0, REG_SZ, (const BYTE *)L"jamotong conf-lab TIP",
                   (DWORD)((wcslen(L"jamotong conf-lab TIP") + 1) * sizeof(WCHAR)));
    if (!RegCreateKeyExW(hk, L"InProcServer32", 0, NULL, 0, KEY_WRITE, NULL, &hk2, NULL)) {
        RegSetValueExW(hk2, NULL, 0, REG_SZ, (const BYTE *)path,
                       (DWORD)((wcslen(path) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hk2, L"ThreadingModel", 0, REG_SZ, (const BYTE *)L"Apartment",
                       (DWORD)(sizeof(L"Apartment")));
        RegCloseKey(hk2);
    }
    RegCloseKey(hk);

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    BOOL didInit = SUCCEEDED(hr);

    ITfInputProcessorProfiles *pp = NULL;
    hr = CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITfInputProcessorProfiles, (void **)&pp);
    if (SUCCEEDED(hr) && pp) {
        static const WCHAR desc[] = L"jamotong conf-lab";
        hr = ITfInputProcessorProfiles_Register(pp, &CLSID_ConfLabTip);
        if (SUCCEEDED(hr))
            hr = ITfInputProcessorProfiles_AddLanguageProfile(
                     pp, &CLSID_ConfLabTip, MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN),
                     &GUID_ConfLabProfile, desc, (ULONG)(ARRAYSIZE(desc) - 1), path, 0, 0);
        ITfInputProcessorProfiles_Release(pp);
    }

    ITfCategoryMgr *cat = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_ITfCategoryMgr, (void **)&cat)) && cat) {
        ITfCategoryMgr_RegisterCategory(cat, &CLSID_ConfLabTip, &GUID_TFCAT_TIP_KEYBOARD,
                                        &CLSID_ConfLabTip);
        ITfCategoryMgr_Release(cat);
    }

    if (didInit) CoUninitialize();
    return hr;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    WCHAR clsidStr[64], key[256];
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    BOOL didInit = SUCCEEDED(hr);

    ITfCategoryMgr *cat = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_ITfCategoryMgr, (void **)&cat)) && cat) {
        ITfCategoryMgr_UnregisterCategory(cat, &CLSID_ConfLabTip, &GUID_TFCAT_TIP_KEYBOARD,
                                          &CLSID_ConfLabTip);
        ITfCategoryMgr_Release(cat);
    }
    ITfInputProcessorProfiles *pp = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_ITfInputProcessorProfiles, (void **)&pp)) && pp) {
        ITfInputProcessorProfiles_Unregister(pp, &CLSID_ConfLabTip);
        ITfInputProcessorProfiles_Release(pp);
    }
    if (didInit) CoUninitialize();

    guid_to_str(&CLSID_ConfLabTip, clsidStr, ARRAYSIZE(clsidStr));
    swprintf(key, ARRAYSIZE(key), L"CLSID\\%s\\InProcServer32", clsidStr);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
    swprintf(key, ARRAYSIZE(key), L"CLSID\\%s", clsidStr);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
    return S_OK;
}
