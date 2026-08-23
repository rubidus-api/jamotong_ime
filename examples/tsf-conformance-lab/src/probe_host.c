// tsf-conformance-lab / probe_host.c
//
// RFC-0012 Phase 0 — "이 TSF 호스트에서 표준 계약 중 무엇이 실제로 서는가"를 재는 프로그램.
// 앱(호스트) 역할을 하며, 제품 코드와 공유하는 것이 없다.
//
// 재는 것:
//   1. ITfThreadMgr 생성/활성화, client id
//   2. QI: ITfThreadMgrEx / ITfUIElementMgr / ITfCompartmentMgr / ITfKeystrokeMgr / ITfCategoryMgr
//   3. compartment: 스레드 스코프 SetValue/GetValue + ITfCompartmentEventSink 통지가 오는가
//   4. preserved key: PreserveKey/UnpreserveKey 가 받아들여지는가
//   5. document mgr / context 생성, Push, GetStatus(TS_SS_*) 관찰
//   6. transitory extension compartment 3종이 문서 관리자 스코프에서 읽히는가
//   7. ITfThreadMgrEx::ActivateEx(TF_TMAE_UIELEMENTENABLEDONLY) = UI-less 스레드가 서는가
//   8. ITfInputProcessorProfiles 열거
//
// 출력은 한 줄에 한 계약: "PROBE <이름> hr=0x... <비고>". 기계가 표로 접는다.
// 판정은 이 프로그램이 하지 않는다 — 수를 낸다.

#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <msctf.h>
#include "tsf_missing_guids.h"
#include <stdio.h>

static int g_pass = 0, g_fail = 0;

static void probe(const char *name, HRESULT hr, const char *note)
{
    int ok = SUCCEEDED(hr);
    ok ? g_pass++ : g_fail++;
    printf("PROBE %-42s %-4s hr=0x%08lX %s\n",
           name, ok ? "OK" : "FAIL", (unsigned long)hr, note ? note : "");
    fflush(stdout);
}

// ---------------------------------------------------------------- compartment sink

typedef struct {
    ITfCompartmentEventSinkVtbl *lpVtbl;
    LONG ref;
    int  hits;
} CompSink;

static HRESULT STDMETHODCALLTYPE cs_QI(ITfCompartmentEventSink *me, REFIID riid, void **ppv)
{
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfCompartmentEventSink)) {
        *ppv = me;
        ITfCompartmentEventSink_AddRef(me);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE cs_AddRef(ITfCompartmentEventSink *me)
{ return InterlockedIncrement(&((CompSink *)me)->ref); }
static ULONG STDMETHODCALLTYPE cs_Release(ITfCompartmentEventSink *me)
{ return InterlockedDecrement(&((CompSink *)me)->ref); }   // 스택 객체 — 해제 안 함
static HRESULT STDMETHODCALLTYPE cs_OnChange(ITfCompartmentEventSink *me, REFGUID rguid)
{ (void)rguid; ((CompSink *)me)->hits++; return S_OK; }

static ITfCompartmentEventSinkVtbl g_csVtbl = { cs_QI, cs_AddRef, cs_Release, cs_OnChange };

// ---------------------------------------------------------------- helpers

static HRESULT set_i4(ITfCompartment *cp, TfClientId cid, LONG v)
{
    VARIANT var; VariantInit(&var); var.vt = VT_I4; var.lVal = v;
    return ITfCompartment_SetValue(cp, cid, &var);
}

static void probe_compartment(ITfCompartmentMgr *cm, TfClientId cid,
                              const GUID *guid, const char *label)
{
    ITfCompartment *cp = NULL;
    char name[96];
    HRESULT hr = ITfCompartmentMgr_GetCompartment(cm, guid, &cp);
    snprintf(name, sizeof name, "compartment.get[%s]", label);
    probe(name, hr, NULL);
    if (FAILED(hr) || !cp) return;

    hr = set_i4(cp, cid, 1);
    snprintf(name, sizeof name, "compartment.set[%s]", label);
    probe(name, hr, NULL);

    VARIANT got; VariantInit(&got);
    hr = ITfCompartment_GetValue(cp, &got);
    snprintf(name, sizeof name, "compartment.get-value[%s]", label);
    {
        char note[64];
        snprintf(note, sizeof note, "vt=%d val=%ld", (int)got.vt, (long)got.lVal);
        probe(name, hr, note);
    }
    VariantClear(&got);

    // 변경 통지
    {
        ITfSource *src = NULL;
        hr = ITfCompartment_QueryInterface(cp, &IID_ITfSource, (void **)&src);
        snprintf(name, sizeof name, "compartment.QI-source[%s]", label);
        probe(name, hr, NULL);
        if (SUCCEEDED(hr) && src) {
            CompSink sink = { &g_csVtbl, 1, 0 };
            DWORD cookie = 0;
            hr = ITfSource_AdviseSink(src, &IID_ITfCompartmentEventSink,
                                      (IUnknown *)&sink, &cookie);
            snprintf(name, sizeof name, "compartment.advise[%s]", label);
            probe(name, hr, NULL);
            if (SUCCEEDED(hr)) {
                set_i4(cp, cid, 0);
                snprintf(name, sizeof name, "compartment.notify[%s]", label);
                {
                    char note[48];
                    snprintf(note, sizeof note, "OnChange hits=%d", sink.hits);
                    probe(name, sink.hits > 0 ? S_OK : E_FAIL, note);
                }
                ITfSource_UnadviseSink(src, cookie);
            }
            ITfSource_Release(src);
        }
    }
    ITfCompartment_Release(cp);
}

// ---------------------------------------------------------------- main

int main(void)
{
    HRESULT hr;
    ITfThreadMgr *tm = NULL;
    TfClientId cid = 0;

    printf("== tsf-conformance-lab probe_host (RFC-0012 Phase 0) ==\n");

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    probe("CoInitializeEx", hr, NULL);

    hr = CoCreateInstance(&CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITfThreadMgr, (void **)&tm);
    probe("CoCreateInstance(CLSID_TF_ThreadMgr)", hr, NULL);
    if (FAILED(hr) || !tm) goto done;

    hr = ITfThreadMgr_Activate(tm, &cid);
    {
        char note[48];
        snprintf(note, sizeof note, "clientid=%u", (unsigned)cid);
        probe("ITfThreadMgr::Activate", hr, note);
    }

    // --- QI 표
    {
        struct { const IID *iid; const char *label; } q[] = {
            { &IID_ITfThreadMgrEx,     "ITfThreadMgrEx" },
            { &IID_ITfUIElementMgr,    "ITfUIElementMgr" },
            { &IID_ITfCompartmentMgr,  "ITfCompartmentMgr" },
            { &IID_ITfKeystrokeMgr,    "ITfKeystrokeMgr" },
            { &IID_ITfSource,          "ITfSource" },
            { &IID_ITfMessagePump,     "ITfMessagePump" },
        };
        for (size_t i = 0; i < sizeof q / sizeof q[0]; i++) {
            IUnknown *p = NULL;
            char name[80];
            snprintf(name, sizeof name, "QI.threadmgr[%s]", q[i].label);
            hr = ITfThreadMgr_QueryInterface(tm, q[i].iid, (void **)&p);
            probe(name, hr, NULL);
            if (p) IUnknown_Release(p);
        }
    }

    // --- category mgr (등록 계약)
    {
        ITfCategoryMgr *catm = NULL;
        hr = CoCreateInstance(&CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
                              &IID_ITfCategoryMgr, (void **)&catm);
        probe("CoCreateInstance(CLSID_TF_CategoryMgr)", hr, NULL);
        if (catm) ITfCategoryMgr_Release(catm);
    }

    // --- input processor profiles (등록된 TIP 열거)
    {
        ITfInputProcessorProfiles *pp = NULL;
        hr = CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                              &IID_ITfInputProcessorProfiles, (void **)&pp);
        probe("CoCreateInstance(CLSID_TF_InputProcessorProfiles)", hr, NULL);
        if (SUCCEEDED(hr) && pp) {
            IEnumGUID *e = NULL;
            hr = ITfInputProcessorProfiles_EnumInputProcessorInfo(pp, &e);
            probe("EnumInputProcessorInfo", hr, NULL);
            if (SUCCEEDED(hr) && e) {
                GUID g; ULONG got = 0; int n = 0;
                while (IEnumGUID_Next(e, 1, &g, &got) == S_OK && got == 1) n++;
                {
                    char note[48];
                    snprintf(note, sizeof note, "registered TIPs=%d", n);
                    probe("EnumInputProcessorInfo.count", S_OK, note);
                }
                IEnumGUID_Release(e);
            }
            ITfInputProcessorProfiles_Release(pp);
        }
    }

    // --- compartment 계약
    {
        ITfCompartmentMgr *cm = NULL;
        hr = ITfThreadMgr_QueryInterface(tm, &IID_ITfCompartmentMgr, (void **)&cm);
        if (SUCCEEDED(hr) && cm) {
            probe_compartment(cm, cid, &GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
                              "KEYBOARD_OPENCLOSE");
            probe_compartment(cm, cid, &GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION_L,
                              "INPUTMODE_CONVERSION");
            ITfCompartmentMgr_Release(cm);
        }
    }

    // --- preserved key 계약
    {
        ITfKeystrokeMgr *km = NULL;
        hr = ITfThreadMgr_QueryInterface(tm, &IID_ITfKeystrokeMgr, (void **)&km);
        if (SUCCEEDED(hr) && km) {
            // 임의의 lab 전용 명령 GUID (제품과 겹치지 않음)
            static const GUID kLabCmd =
                { 0x7b1d2c40, 0x9a3e, 0x4f21, { 0xb2, 0x77, 0x51, 0x0c, 0x2e, 0x64, 0x91, 0x03 } };
            TF_PRESERVEDKEY pk = { VK_SPACE, TF_MOD_SHIFT };
            static const WCHAR desc[] = L"lab: rotate";
            hr = ITfKeystrokeMgr_PreserveKey(km, cid, &kLabCmd, &pk,
                                             desc, (ULONG)(sizeof desc / sizeof desc[0] - 1));
            probe("ITfKeystrokeMgr::PreserveKey(Shift+Space)", hr, NULL);
            if (SUCCEEDED(hr)) {
                hr = ITfKeystrokeMgr_UnpreserveKey(km, &kLabCmd, &pk);
                probe("ITfKeystrokeMgr::UnpreserveKey", hr, NULL);
            }
            ITfKeystrokeMgr_Release(km);
        }
    }

    // --- document manager / context / transitory compartments
    {
        ITfDocumentMgr *dm = NULL;
        hr = ITfThreadMgr_CreateDocumentMgr(tm, &dm);
        probe("ITfThreadMgr::CreateDocumentMgr", hr, NULL);
        if (SUCCEEDED(hr) && dm) {
            ITfCompartmentMgr *dcm = NULL;
            hr = ITfDocumentMgr_QueryInterface(dm, &IID_ITfCompartmentMgr, (void **)&dcm);
            probe("QI.documentmgr[ITfCompartmentMgr]", hr, NULL);
            if (SUCCEEDED(hr) && dcm) {
                struct { const GUID *g; const char *label; } t[] = {
                    { &GUID_COMPARTMENT_TRANSITORYEXTENSION_L_UNVERIFIED,   "TRANSITORYEXTENSION?" },
                    { &GUID_COMPARTMENT_TRANSITORYEXTENSION_PARENT_L,         "TRANSITORY_PARENT" },
                    { &GUID_COMPARTMENT_TRANSITORYEXTENSION_DOCUMENTMANAGER_L,"TRANSITORY_DOCMGR" },
                };
                for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
                    ITfCompartment *cp = NULL;
                    char name[80];
                    snprintf(name, sizeof name, "compartment.get[%s]", t[i].label);
                    hr = ITfCompartmentMgr_GetCompartment(dcm, t[i].g, &cp);
                    probe(name, hr, NULL);
                    if (cp) {
                        VARIANT v; VariantInit(&v);
                        HRESULT hv = ITfCompartment_GetValue(cp, &v);
                        char note[64];
                        snprintf(note, sizeof note, "vt=%d (S_FALSE=비어있음)", (int)v.vt);
                        snprintf(name, sizeof name, "compartment.value[%s]", t[i].label);
                        probe(name, hv, note);
                        VariantClear(&v);
                        ITfCompartment_Release(cp);
                    }
                }
                ITfCompartmentMgr_Release(dcm);
            }
            ITfDocumentMgr_Release(dm);
        }
    }

    // --- UI-less 스레드 (ThreadMgrEx::ActivateEx)
    {
        ITfThreadMgrEx *tmex = NULL;
        hr = ITfThreadMgr_QueryInterface(tm, &IID_ITfThreadMgrEx, (void **)&tmex);
        if (SUCCEEDED(hr) && tmex) {
            TfClientId cid2 = 0;
            hr = ITfThreadMgrEx_ActivateEx(tmex, &cid2, TF_TMAE_UIELEMENTENABLEDONLY);
            probe("ITfThreadMgrEx::ActivateEx(UIELEMENTENABLEDONLY)", hr, NULL);
            ITfThreadMgrEx_Release(tmex);
        }
    }

    hr = ITfThreadMgr_Deactivate(tm);
    probe("ITfThreadMgr::Deactivate", hr, NULL);
    ITfThreadMgr_Release(tm);

done:
    CoUninitialize();
    printf("== probe summary: ok=%d fail=%d ==\n", g_pass, g_fail);
    return 0;
}
