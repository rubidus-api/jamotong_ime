/* minimal.c — "뜨고, 키 하나를 먹는" 최소 TSF IME
 *
 * 이 파일 하나 + minimal.def 만으로 Windows 언어 목록에 뜨는 IME가 된다.
 * 하는 일: 한/영 상태 없이, `a` 키를 먹어서 문서에 'ㄱ'을 넣는다. 그게 전부다.
 * 목적은 "COM → TIP → 등록 → 키 → 삽입"의 최소 경로를 눈으로 확인하는 것.
 *
 * 빌드 (MinGW-w64):
 *   x86_64-w64-mingw32-gcc -shared -o minimal.dll minimal.c minimal.def \
 *       -static -static-libgcc -D_UNICODE -DUNICODE \
 *       -lole32 -loleaut32 -luuid
 *
 * 설치 (관리자 권한 명령 프롬프트):
 *   regsvr32 minimal.dll
 * 제거:
 *   regsvr32 /u minimal.dll
 *
 * 주의: TIP DLL은 호스트 프로세스와 비트수가 같아야 한다. 32비트 앱에서 쓰려면
 * i686-w64-mingw32-gcc 로도 빌드해 따로 등록해야 한다(§0.5 참고).
 */
#define COBJMACROS          /* C에서 인터페이스를 ITf..._Method(p, ...) 로 호출 */
#define INITGUID            /* GUID 실체를 이 번역 단위에 정의 */
#include <windows.h>
#include <initguid.h>
#include <msctf.h>
#include <olectl.h>
#include <stdio.h>
#include <stdlib.h>   /* calloc/free */
#include <stddef.h>   /* offsetof — 32비트에서 빠뜨리면 컴파일 실패 */

/* ── 이 IME를 식별하는 GUID 3개. 반드시 새로 만들어 쓸 것 (PowerShell: [guid]::NewGuid()) ── */
DEFINE_GUID(CLSID_Minimal,   0x7b1f4c20,0x9a3e,0x4f61,0xa1,0x22,0x00,0x11,0x22,0x33,0x44,0x01);
DEFINE_GUID(GUID_MinProfile, 0x7b1f4c20,0x9a3e,0x4f61,0xa1,0x22,0x00,0x11,0x22,0x33,0x44,0x02);

#define LANGID_KO MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)

static HINSTANCE g_hInst;
static LONG g_refDll = 0;

/* ══════════════════════════════════════════════════════════════════
 * 1. 편집 세션 — 문서에 글자를 넣는 유일한 통로
 *    TSF는 아무 때나 문서를 못 고친다. "편집 세션"을 요청해 그 안에서만 고친다.
 * ══════════════════════════════════════════════════════════════════ */
typedef struct {
    ITfEditSession base;      /* ★ vtbl 포인터가 첫 멤버여야 COM 캐스팅이 성립한다 */
    LONG ref;
    ITfContext *ctx;
    TfClientId  cid;
    WCHAR       ch;
} EditSes;

static STDMETHODIMP ES_QI(ITfEditSession *me, REFIID riid, void **ppv) {
    EditSes *s = (EditSes*)me;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfEditSession)) {
        *ppv = me; InterlockedIncrement(&s->ref); return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static STDMETHODIMP_(ULONG) ES_AddRef(ITfEditSession *me) {
    return InterlockedIncrement(&((EditSes*)me)->ref);
}
static STDMETHODIMP_(ULONG) ES_Release(ITfEditSession *me) {
    EditSes *s = (EditSes*)me;
    LONG n = InterlockedDecrement(&s->ref);
    if (n == 0) { if (s->ctx) ITfContext_Release(s->ctx); free(s); }
    return n;
}
/* 실제 편집이 일어나는 곳. ec = 편집 쿠키(이 세션에서만 유효한 권한 표) */
static STDMETHODIMP ES_DoEditSession(ITfEditSession *me, TfEditCookie ec) {
    EditSes *s = (EditSes*)me;
    ITfInsertAtSelection *ins = NULL;
    ITfRange *rng = NULL;
    HRESULT hr = ITfContext_QueryInterface(s->ctx, &IID_ITfInsertAtSelection, (void**)&ins);
    if (FAILED(hr)) return hr;
    /* 현재 커서(선택) 자리에 문자열을 '삽입'한다. 조합(composition) 없이 바로 확정. */
    hr = ITfInsertAtSelection_InsertTextAtSelection(ins, ec, 0, &s->ch, 1, &rng);
    if (rng) ITfRange_Release(rng);
    ITfInsertAtSelection_Release(ins);
    return hr;
}
static const ITfEditSessionVtbl g_esVtbl = { ES_QI, ES_AddRef, ES_Release, ES_DoEditSession };

static HRESULT InsertChar(ITfContext *ctx, TfClientId cid, WCHAR ch) {
    EditSes *s = (EditSes*)calloc(1, sizeof(EditSes));
    if (!s) return E_OUTOFMEMORY;
    s->base.lpVtbl = (ITfEditSessionVtbl*)&g_esVtbl;
    s->ref = 1; s->ctx = ctx; s->cid = cid; s->ch = ch;
    ITfContext_AddRef(ctx);
    HRESULT hrSession = S_OK;
    /* TF_ES_SYNC: 지금 당장. 키 처리 중에는 동기 요청이 자연스럽다.
       TF_ES_READWRITE: 읽기+쓰기 권한을 달라. */
    HRESULT hr = ITfContext_RequestEditSession(ctx, cid, (ITfEditSession*)s,
                                               TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    ITfEditSession_Release((ITfEditSession*)s);
    return SUCCEEDED(hr) ? hrSession : hr;
}

/* ══════════════════════════════════════════════════════════════════
 * 2. TIP 본체 — ITfTextInputProcessor + ITfKeyEventSink 를 한 객체가 구현
 *    C에는 다중 상속이 없으므로 vtbl 포인터를 멤버로 여러 개 두고,
 *    각 멤버의 주소로부터 바깥 구조체 주소를 역산한다(IMPL_TO_OBJ).
 * ══════════════════════════════════════════════════════════════════ */
typedef struct {
    ITfTextInputProcessor tip;   /* 첫 멤버 */
    ITfKeyEventSink       key;   /* 두 번째 vtbl */
    LONG ref;
    ITfThreadMgr *tm;
    TfClientId    cid;
} Svc;
#define FROM_KEY(p) ((Svc*)((char*)(p) - offsetof(Svc, key)))

static STDMETHODIMP  SVC_QI(ITfTextInputProcessor*, REFIID, void**);
static STDMETHODIMP_(ULONG) SVC_AddRef(ITfTextInputProcessor *me) {
    return InterlockedIncrement(&((Svc*)me)->ref);
}
static STDMETHODIMP_(ULONG) SVC_Release(ITfTextInputProcessor *me) {
    Svc *s = (Svc*)me;
    LONG n = InterlockedDecrement(&s->ref);
    if (n == 0) { free(s); InterlockedDecrement(&g_refDll); }
    return n;
}
/* 키 싱크 쪽 IUnknown 3종은 본체로 위임한다 */
static STDMETHODIMP KS_QI(ITfKeyEventSink *me, REFIID riid, void **ppv) {
    return SVC_QI((ITfTextInputProcessor*)FROM_KEY(me), riid, ppv);
}
static STDMETHODIMP_(ULONG) KS_AddRef(ITfKeyEventSink *me)  { return SVC_AddRef((ITfTextInputProcessor*)FROM_KEY(me)); }
static STDMETHODIMP_(ULONG) KS_Release(ITfKeyEventSink *me) { return SVC_Release((ITfTextInputProcessor*)FROM_KEY(me)); }

static STDMETHODIMP KS_OnSetFocus(ITfKeyEventSink *me, BOOL f) { (void)me;(void)f; return S_OK; }
/* ★ OnTestKeyDown: "이 키 먹을 거니?"를 부작용 없이 답한다.
   여기서 TRUE로 답한 키만 OnKeyDown이 불린다. 이 이중 구조가 TSF의 핵심 관문이다. */
static STDMETHODIMP KS_OnTestKeyDown(ITfKeyEventSink *me, ITfContext *ctx,
                                     WPARAM w, LPARAM l, BOOL *eaten) {
    (void)me;(void)ctx;(void)l;
    *eaten = (w == 'A');           /* 'a'/'A' 키만 먹는다 */
    return S_OK;
}
/* 실제 처리. 여기서만 부작용(문서 편집)을 낸다. */
static STDMETHODIMP KS_OnKeyDown(ITfKeyEventSink *me, ITfContext *ctx,
                                 WPARAM w, LPARAM l, BOOL *eaten) {
    (void)l;
    Svc *s = FROM_KEY(me);
    *eaten = FALSE;
    if (w == 'A' && ctx) {
        HRESULT hr = InsertChar(ctx, s->cid, L'ㄱ');   /* U+3131 = 'ㄱ' */
        *eaten = SUCCEEDED(hr);
    }
    return S_OK;
}
static STDMETHODIMP KS_OnTestKeyUp(ITfKeyEventSink *me, ITfContext *c, WPARAM w, LPARAM l, BOOL *e) {
    (void)me;(void)c;(void)w;(void)l; *e = FALSE; return S_OK;
}
static STDMETHODIMP KS_OnKeyUp(ITfKeyEventSink *me, ITfContext *c, WPARAM w, LPARAM l, BOOL *e) {
    (void)me;(void)c;(void)w;(void)l; *e = FALSE; return S_OK;
}
static STDMETHODIMP KS_OnPreservedKey(ITfKeyEventSink *me, ITfContext *c, REFGUID g, BOOL *e) {
    (void)me;(void)c;(void)g; *e = FALSE; return S_OK;
}
/* ★ vtbl 순서는 헤더 선언 순서와 정확히 같아야 한다.
   msctf.h의 ITfKeyEventSink는 OnSetFocus, OnTestKeyDown, OnTestKeyUp, OnKeyDown, OnKeyUp,
   OnPreservedKey 순이다 — OnTestKeyUp이 OnKeyDown보다 앞이다(흔한 실수). */
static const ITfKeyEventSinkVtbl g_keyVtbl = {
    KS_QI, KS_AddRef, KS_Release,
    KS_OnSetFocus, KS_OnTestKeyDown, KS_OnTestKeyUp, KS_OnKeyDown, KS_OnKeyUp, KS_OnPreservedKey
};

/* Activate: 이 스레드에서 IME가 켜졌다. 키 싱크를 붙인다. */
static STDMETHODIMP SVC_Activate(ITfTextInputProcessor *me, ITfThreadMgr *tm, TfClientId cid) {
    Svc *s = (Svc*)me;
    ITfKeystrokeMgr *km = NULL;
    s->tm = tm; s->cid = cid;
    ITfThreadMgr_AddRef(tm);
    if (SUCCEEDED(ITfThreadMgr_QueryInterface(tm, &IID_ITfKeystrokeMgr, (void**)&km))) {
        ITfKeystrokeMgr_AdviseKeyEventSink(km, cid, &s->key, TRUE);
        ITfKeystrokeMgr_Release(km);
    }
    return S_OK;
}
/* Deactivate: 붙인 것을 반드시 떼고 참조를 놓는다. 안 그러면 앱이 안 죽는다. */
static STDMETHODIMP SVC_Deactivate(ITfTextInputProcessor *me) {
    Svc *s = (Svc*)me;
    ITfKeystrokeMgr *km = NULL;
    if (s->tm) {
        if (SUCCEEDED(ITfThreadMgr_QueryInterface(s->tm, &IID_ITfKeystrokeMgr, (void**)&km))) {
            ITfKeystrokeMgr_UnadviseKeyEventSink(km, s->cid);
            ITfKeystrokeMgr_Release(km);
        }
        ITfThreadMgr_Release(s->tm); s->tm = NULL;
    }
    return S_OK;
}
static const ITfTextInputProcessorVtbl g_tipVtbl = {
    SVC_QI, SVC_AddRef, SVC_Release, SVC_Activate, SVC_Deactivate
};
static STDMETHODIMP SVC_QI(ITfTextInputProcessor *me, REFIID riid, void **ppv) {
    Svc *s = (Svc*)me;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfTextInputProcessor)) *ppv = &s->tip;
    else if (IsEqualIID(riid, &IID_ITfKeyEventSink)) *ppv = &s->key;
    else { *ppv = NULL; return E_NOINTERFACE; }
    SVC_AddRef(me);
    return S_OK;
}

/* ══════════════════════════════════════════════════════════════════
 * 3. 클래스 팩토리 + DLL 진입점 4종
 * ══════════════════════════════════════════════════════════════════ */
static STDMETHODIMP CF_QI(IClassFactory *me, REFIID riid, void **ppv) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) { *ppv = me; return S_OK; }
    *ppv = NULL; return E_NOINTERFACE;
}
static STDMETHODIMP_(ULONG) CF_AddRef(IClassFactory *me)  { (void)me; return 2; }
static STDMETHODIMP_(ULONG) CF_Release(IClassFactory *me) { (void)me; return 1; }
static STDMETHODIMP CF_CreateInstance(IClassFactory *me, IUnknown *outer, REFIID riid, void **ppv) {
    (void)me;
    if (outer) return CLASS_E_NOAGGREGATION;
    Svc *s = (Svc*)calloc(1, sizeof(Svc));
    if (!s) return E_OUTOFMEMORY;
    s->tip.lpVtbl = (ITfTextInputProcessorVtbl*)&g_tipVtbl;
    s->key.lpVtbl = (ITfKeyEventSinkVtbl*)&g_keyVtbl;
    s->ref = 1;
    InterlockedIncrement(&g_refDll);
    HRESULT hr = SVC_QI(&s->tip, riid, ppv);
    SVC_Release(&s->tip);
    return hr;
}
static STDMETHODIMP CF_LockServer(IClassFactory *me, BOOL lock) {
    (void)me;
    if (lock) InterlockedIncrement(&g_refDll); else InterlockedDecrement(&g_refDll);
    return S_OK;
}
static const IClassFactoryVtbl g_cfVtbl = { CF_QI, CF_AddRef, CF_Release, CF_CreateInstance, CF_LockServer };
static IClassFactory g_cf = { (IClassFactoryVtbl*)&g_cfVtbl };

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) { g_hInst = h; DisableThreadLibraryCalls(h); }
    return TRUE;
}
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv) {
    if (!IsEqualCLSID(rclsid, &CLSID_Minimal)) return CLASS_E_CLASSNOTAVAILABLE;
    return CF_QI(&g_cf, riid, ppv);
}
STDAPI DllCanUnloadNow(void) { return g_refDll == 0 ? S_OK : S_FALSE; }

/* 등록: (1) COM 서버 레지스트리 (2) TSF 프로파일 (3) 카테고리 */
STDAPI DllRegisterServer(void) {
    WCHAR path[MAX_PATH], key[128];
    GUID clsid = CLSID_Minimal;
    WCHAR gs[64];
    if (!GetModuleFileNameW(g_hInst, path, MAX_PATH)) return E_FAIL;
    StringFromGUID2(&clsid, gs, 64);

    /* (1) HKCR\CLSID\{...}\InprocServer32 = DLL 경로, ThreadingModel = Apartment */
    HKEY hk = NULL, hs = NULL;
    swprintf(key, 128, L"CLSID\\%s", gs);
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, key, 0, NULL, 0, KEY_WRITE, NULL, &hk, NULL)) return E_FAIL;
    RegSetValueExW(hk, NULL, 0, REG_SZ, (const BYTE*)L"Minimal TIP", sizeof(L"Minimal TIP"));
    if (!RegCreateKeyExW(hk, L"InprocServer32", 0, NULL, 0, KEY_WRITE, NULL, &hs, NULL)) {
        RegSetValueExW(hs, NULL, 0, REG_SZ, (const BYTE*)path, (DWORD)((wcslen(path)+1)*sizeof(WCHAR)));
        RegSetValueExW(hs, L"ThreadingModel", 0, REG_SZ, (const BYTE*)L"Apartment", sizeof(L"Apartment"));
        RegCloseKey(hs);
    }
    RegCloseKey(hk);

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) return E_FAIL;
    HRESULT hr = E_FAIL;
    ITfInputProcessorProfiles *pp = NULL;
    ITfCategoryMgr *cm = NULL;
    /* (2) 프로파일: "한국어에 이 IME를 붙인다" */
    if (SUCCEEDED(CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_ITfInputProcessorProfiles, (void**)&pp))) {
        ITfInputProcessorProfiles_Register(pp, &CLSID_Minimal);
        ITfInputProcessorProfiles_AddLanguageProfile(pp, &CLSID_Minimal, LANGID_KO, &GUID_MinProfile,
            L"Minimal TIP", (ULONG)wcslen(L"Minimal TIP"), path, (ULONG)wcslen(path), 0);
        ITfInputProcessorProfiles_Release(pp);
        hr = S_OK;
    }
    /* (3) 카테고리: "나는 키보드형 TIP이다" — 이걸 빠뜨리면 목록에 떠도 활성화가 안 된다 */
    if (SUCCEEDED(CoCreateInstance(&CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_ITfCategoryMgr, (void**)&cm))) {
        ITfCategoryMgr_RegisterCategory(cm, &CLSID_Minimal, &GUID_TFCAT_TIP_KEYBOARD, &CLSID_Minimal);
        ITfCategoryMgr_Release(cm);
    }
    CoUninitialize();
    return hr;
}
STDAPI DllUnregisterServer(void) {
    WCHAR key[128], gs[64];
    GUID clsid = CLSID_Minimal;
    StringFromGUID2(&clsid, gs, 64);
    if (SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) {
        ITfInputProcessorProfiles *pp = NULL;
        ITfCategoryMgr *cm = NULL;
        if (SUCCEEDED(CoCreateInstance(&CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
                                       &IID_ITfCategoryMgr, (void**)&cm))) {
            ITfCategoryMgr_UnregisterCategory(cm, &CLSID_Minimal, &GUID_TFCAT_TIP_KEYBOARD, &CLSID_Minimal);
            ITfCategoryMgr_Release(cm);
        }
        if (SUCCEEDED(CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                                       &IID_ITfInputProcessorProfiles, (void**)&pp))) {
            ITfInputProcessorProfiles_RemoveLanguageProfile(pp, &CLSID_Minimal, LANGID_KO, &GUID_MinProfile);
            ITfInputProcessorProfiles_Unregister(pp, &CLSID_Minimal);
            ITfInputProcessorProfiles_Release(pp);
        }
        CoUninitialize();
    }
    swprintf(key, 128, L"CLSID\\%s\\InprocServer32", gs);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
    swprintf(key, 128, L"CLSID\\%s", gs);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
    return S_OK;
}
