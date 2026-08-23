#include "compartment.h"
#include "comp_state.h"
#include "langbar.h"

// mingw msctf.h/libuuid 에 없는 GUID. 값 출처: Microsoft Learn 'Predefined Compartments' +
// .NET WPF 공개 소스의 TSF interop 상수(ccf05dd8-4a87-11d7-a6e2-00065b84435c). 확인 2026-08-20.
// ★값이 틀리면 조용히 "없는 기능"처럼 동작한다 — 제품 register.c 의 _J 선례와 같은 규율로 출처를 적는다.
static const GUID GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION_J =
    { 0xccf05dd8, 0x4a87, 0x11d7, { 0xa6, 0xe2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c } };

// 앞선 선언(jamotong.h 의 kIID_* 패턴과 동일: mingw 가 선언만 하고 uuid 에 없는 것 대비)
static const IID kIID_ITfCompartmentEventSink_J =
    { 0x743abd5f, 0xf26d, 0x48df, { 0x8c, 0xc5, 0x23, 0x84, 0x92, 0x41, 0x9b, 0x64 } };

static HRESULT GetThreadCompartment(JamotongTextService *obj, const GUID *guid, ITfCompartment **out) {
    *out = NULL;
    if (!obj->threadMgr) return E_FAIL;
    ITfCompartmentMgr *cm = NULL;
    HRESULT hr = obj->threadMgr->lpVtbl->QueryInterface(obj->threadMgr, &IID_ITfCompartmentMgr, (void**)&cm);
    if (FAILED(hr) || !cm) return hr;
    hr = cm->lpVtbl->GetCompartment(cm, guid, out);
    cm->lpVtbl->Release(cm);
    return hr;
}

static long ReadI4(ITfCompartment *cp, long dflt) {
    VARIANT v; VariantInit(&v);
    long r = dflt;
    if (cp && SUCCEEDED(cp->lpVtbl->GetValue(cp, &v)) && v.vt == VT_I4) r = v.lVal;
    VariantClear(&v);
    return r;
}

static HRESULT WriteI4(JamotongTextService *obj, ITfCompartment *cp, long val) {
    if (!cp) return E_POINTER;
    VARIANT v; VariantInit(&v); v.vt = VT_I4; v.lVal = val;
    return cp->lpVtbl->SetValue(cp, obj->clientId, &v);
}

// ── 발행 ──────────────────────────────────────────────────────────────────────
void Compart_Publish(JamotongTextService *obj) {
    if (!obj->cpOpenClose && !obj->cpConvMode) return;   // Attach 안 됨/킬스위치

    long open = 0, conv = 0;
    EnterCriticalSection(&g_configLock);
    LayoutConfig *layout = Config_GetCurrentLayout(&obj->config);
    int type = layout ? (int)layout->type : 0;
    bool fullWidth = obj->config.options.fullWidth;
    LeaveCriticalSection(&g_configLock);
    CompState_Values(type, obj->passthrough ? true : false, fullWidth, &open, &conv);

    // 같은 값이면 쓰지 않는다(포커스마다 불려도 비용 0). 쓰는 동안 자기 통지는 무시.
    obj->cpSelfWrite = TRUE;
    if (obj->cpOpenClose && obj->cpLastOpen != open) {
        if (SUCCEEDED(WriteI4(obj, obj->cpOpenClose, open))) obj->cpLastOpen = open;
    }
    if (obj->cpConvMode && obj->cpLastConv != conv) {
        if (SUCCEEDED(WriteI4(obj, obj->cpConvMode, conv))) obj->cpLastConv = conv;
    }
    obj->cpSelfWrite = FALSE;
}

// ── 밖에서 바뀐 OPENCLOSE → 자판 전환 (ITfCompartmentEventSink::OnChange) ──────
static HRESULT STDMETHODCALLTYPE CES_QueryInterface(ITfCompartmentEventSink *pThis, REFIID riid, void **ppv) {
    JamotongTextService *obj = IMPL_TO_OBJ(CES, pThis);
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &kIID_ITfCompartmentEventSink_J)) {
        *ppv = &obj->lpVtblCES; obj->lpVtblTIP->AddRef((ITfTextInputProcessor*)obj); return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE CES_AddRef(ITfCompartmentEventSink *pThis) {
    JamotongTextService *obj = IMPL_TO_OBJ(CES, pThis);
    return obj->lpVtblTIP->AddRef((ITfTextInputProcessor*)obj);
}
static ULONG STDMETHODCALLTYPE CES_Release(ITfCompartmentEventSink *pThis) {
    JamotongTextService *obj = IMPL_TO_OBJ(CES, pThis);
    return obj->lpVtblTIP->Release((ITfTextInputProcessor*)obj);
}
static HRESULT STDMETHODCALLTYPE CES_OnChange(ITfCompartmentEventSink *pThis, REFGUID rguid) {
    JamotongTextService *obj = IMPL_TO_OBJ(CES, pThis);
    if (obj->cpSelfWrite) return S_OK;                       // 우리가 쓴 값의 메아리
    // 한/A 표시기·앱은 OPENCLOSE 를 바꿀 수도, (MS 한국어 IME 관례대로) CONVERSION 의 NATIVE 비트를
    // 바꿀 수도 있다 — 둘 다 "한글을 원하는가" 로 읽는다.
    bool wantHangul; long open = obj->cpLastOpen;
    if (IsEqualGUID(rguid, &GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
        open = ReadI4(obj->cpOpenClose, obj->cpLastOpen);
        if (open == obj->cpLastOpen) return S_OK;            // 실질 변화 없음
        wantHangul = open != 0;
    } else if (IsEqualGUID(rguid, &GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION_J)) {
        long conv = ReadI4(obj->cpConvMode, obj->cpLastConv);
        if (conv == obj->cpLastConv) return S_OK;
        wantHangul = (conv & 0x0001 /*NATIVE*/) != 0;
        obj->cpLastConv = conv;
    } else return S_OK;
    if (obj->passthrough) { Compart_Publish(obj); return S_OK; }   // 무간섭 중엔 밖의 요구를 되돌린다

    int target = -1;
    EnterCriticalSection(&g_configLock);
    {
        int   types[8]; bool enabled[8];
        int n = obj->config.layoutCount; if (n > 8) n = 8;
        for (int i = 0; i < n; i++) { types[i] = (int)obj->config.layouts[i].type; enabled[i] = obj->config.layouts[i].enabled; }
        target = CompState_PickIndex(types, enabled, n, obj->config.currentLayoutIndex, wantHangul);
        if (target >= 0 && target != obj->config.currentLayoutIndex) obj->config.currentLayoutIndex = target;
    }
    LeaveCriticalSection(&g_configLock);

    if (target >= 0) {
        Jamotong_OnLayoutSwitched(obj);      // 조합 확정+정리 + 언어바 (text_service.c)
        obj->cpLastOpen = open;              // 밖의 값을 받아들였다
        Compart_Publish(obj);                // conv 모드 등 나머지를 맞춘다
    } else {
        Compart_Publish(obj);                // 줄 자판이 없다 → 우리 값을 다시 발행해 어긋남 해소
    }
    return S_OK;
}
static ITfCompartmentEventSinkVtbl g_CESVtbl = { CES_QueryInterface, CES_AddRef, CES_Release, CES_OnChange };

// ── 부착/해제 ───────────────────────────────────────────────────────────────
void Compart_Attach(JamotongTextService *obj) {
    obj->lpVtblCES = &g_CESVtbl;
    obj->cpOpenClose = NULL; obj->cpConvMode = NULL; obj->cpCookie = 0; obj->cpCookieConv = 0;
    obj->cpLastOpen = -1; obj->cpLastConv = -1; obj->cpSelfWrite = FALSE;
    obj->ctxKeyboardDisabled = FALSE;

    bool on;
    EnterCriticalSection(&g_configLock);
    on = obj->config.options.useCompartments;
    LeaveCriticalSection(&g_configLock);
    if (!on) return;

    if (FAILED(GetThreadCompartment(obj, &GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &obj->cpOpenClose))) obj->cpOpenClose = NULL;
    if (FAILED(GetThreadCompartment(obj, &GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION_J, &obj->cpConvMode))) obj->cpConvMode = NULL;

    if (obj->cpOpenClose) {
        ITfSource *src = NULL;
        if (SUCCEEDED(obj->cpOpenClose->lpVtbl->QueryInterface(obj->cpOpenClose, &IID_ITfSource, (void**)&src)) && src) {
            if (FAILED(src->lpVtbl->AdviseSink(src, &kIID_ITfCompartmentEventSink_J, (IUnknown*)&obj->lpVtblCES, &obj->cpCookie)))
                obj->cpCookie = 0;
            src->lpVtbl->Release(src);
        }
    }
    if (obj->cpConvMode) {
        ITfSource *src = NULL;
        if (SUCCEEDED(obj->cpConvMode->lpVtbl->QueryInterface(obj->cpConvMode, &IID_ITfSource, (void**)&src)) && src) {
            if (FAILED(src->lpVtbl->AdviseSink(src, &kIID_ITfCompartmentEventSink_J, (IUnknown*)&obj->lpVtblCES, &obj->cpCookieConv)))
                obj->cpCookieConv = 0;
            src->lpVtbl->Release(src);
        }
    }
    obj->cpPendingCommit = 0;
    Compart_Publish(obj);   // 처음 한 번은 무조건 쓴다(cpLast* = -1)
}

void Compart_Detach(JamotongTextService *obj) {
    if (obj->cpOpenClose) {
        if (obj->cpCookie) {
            ITfSource *src = NULL;
            if (SUCCEEDED(obj->cpOpenClose->lpVtbl->QueryInterface(obj->cpOpenClose, &IID_ITfSource, (void**)&src)) && src) {
                src->lpVtbl->UnadviseSink(src, obj->cpCookie);
                src->lpVtbl->Release(src);
            }
            obj->cpCookie = 0;
        }
        obj->cpOpenClose->lpVtbl->Release(obj->cpOpenClose); obj->cpOpenClose = NULL;
    }
    if (obj->cpConvMode) {
        if (obj->cpCookieConv) {
            ITfSource *src = NULL;
            if (SUCCEEDED(obj->cpConvMode->lpVtbl->QueryInterface(obj->cpConvMode, &IID_ITfSource, (void**)&src)) && src) {
                src->lpVtbl->UnadviseSink(src, obj->cpCookieConv);
                src->lpVtbl->Release(src);
            }
            obj->cpCookieConv = 0;
        }
        obj->cpConvMode->lpVtbl->Release(obj->cpConvMode); obj->cpConvMode = NULL;
    }
}

// ── 앱이 끈 문맥 (context 스코프 KEYBOARD_DISABLED) ─────────────────────────
void Compart_ReadContextDisabled(JamotongTextService *obj, ITfContext *pic) {
    obj->ctxKeyboardDisabled = FALSE;
    if (!pic || (!obj->cpOpenClose && !obj->cpConvMode)) return;   // 킬스위치면 읽지 않는다
    ITfCompartmentMgr *cm = NULL;
    if (FAILED(pic->lpVtbl->QueryInterface(pic, &IID_ITfCompartmentMgr, (void**)&cm)) || !cm) return;
    ITfCompartment *cp = NULL;
    if (SUCCEEDED(cm->lpVtbl->GetCompartment(cm, &GUID_COMPARTMENT_KEYBOARD_DISABLED, &cp)) && cp) {
        obj->ctxKeyboardDisabled = ReadI4(cp, 0) != 0;
        cp->lpVtbl->Release(cp);
    }
    cm->lpVtbl->Release(cm);
}
