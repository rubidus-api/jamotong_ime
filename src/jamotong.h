#pragma once

#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <msctf.h>
#include <olectl.h>
#include "fsm.h"
#include "config.h"
#include "langbar.h"
#include "display_attr.h"
#include "chord.h"
#include "chord_layout.h"

// Jamotong IME CLSID: {C471BCF2-343F-4187-A103-24151C3E20B9}
DEFINE_GUID(CLSID_JamotongIME, 
0xc471bcf2, 0x343f, 0x4187, 0xa1, 0x03, 0x24, 0x15, 0x1c, 0x3e, 0x20, 0xb9);

// Jamotong IME Profile GUID: {8D786315-AC92-498C-8D4C-E8B0E1B008EE}
DEFINE_GUID(GUID_Profile_Jamotong, 
0x8d786315, 0xac92, 0x498c, 0x8d, 0x4c, 0xe8, 0xb0, 0xe1, 0xb0, 0x08, 0xee);

// ── ITfFnConfigure / ITfFunction (이 MinGW의 msctf.h엔 없어 최소 vtbl 직접 선언) ──
//   Windows 언어 설정의 IME "옵션" 버튼이 이 인터페이스의 Show()를 호출해 설정창을 연다.
//   ITfFnConfigure : ITfFunction : IUnknown (GetDisplayName은 ITfFunction, Show는 ITfFnConfigure).
typedef struct ITfFnConfigure ITfFnConfigure;
typedef struct ITfFnConfigureVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ITfFnConfigure*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(ITfFnConfigure*);
    ULONG   (STDMETHODCALLTYPE *Release)(ITfFnConfigure*);
    HRESULT (STDMETHODCALLTYPE *GetDisplayName)(ITfFnConfigure*, BSTR*);
    HRESULT (STDMETHODCALLTYPE *Show)(ITfFnConfigure*, HWND, LANGID, REFGUID);
} ITfFnConfigureVtbl;
struct ITfFnConfigure { const ITfFnConfigureVtbl *lpVtbl; };
extern const GUID IID_ITfFnConfigure_J;   // {88f567c6-1757-49f8-a1b2-89234c1eeff9}
extern const GUID IID_ITfFunction_J;      // {101d6610-0990-11d3-8df0-00105a2799b5}

// ITfTextInputProcessorEx vtbl — 부모(ITfTextInputProcessor) 5 + ActivateEx (★상속 순서 고정, T009/T010 선례).
// mingw msctf.h 에 타입이 없어 직접 선언. self 인자는 기존 캐스트((ITfTextInputProcessor*)obj)와의
// 호환을 위해 ITfTextInputProcessor* 로 둔다 (COM 이진 배치는 동일).
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ITfTextInputProcessor*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(ITfTextInputProcessor*);
    ULONG   (STDMETHODCALLTYPE *Release)(ITfTextInputProcessor*);
    HRESULT (STDMETHODCALLTYPE *Activate)(ITfTextInputProcessor*, ITfThreadMgr*, TfClientId);
    HRESULT (STDMETHODCALLTYPE *Deactivate)(ITfTextInputProcessor*);
    HRESULT (STDMETHODCALLTYPE *ActivateEx)(ITfTextInputProcessor*, ITfThreadMgr*, TfClientId, DWORD);
} JamoTIPExVtbl;

// preserved key 등록 항목 (RFC-0013 C — preserved.c)
#define JAMO_PRESERVED_MAX 32   // 기능 4종 × 단축키 최대 8
typedef struct { GUID guid; int fn; TF_PRESERVEDKEY key; } JamoPreservedEntry;

// Text Service Instance Struct
typedef struct JamotongTextService {
    JamoTIPExVtbl *lpVtblTIP;   // ITfTextInputProcessor(Ex) — 부모 5개가 앞이라 기존 캐스트 그대로 유효
    ITfKeyEventSinkVtbl *lpVtblKES;                     // 키 입력(ITfKeyEventSink)
    const ITfDisplayAttributeProviderVtbl *lpVtblDAP;   // 디스플레이 속성 공급자(RFC-0010 인라인 조합 밑줄)
    ITfFunctionProviderVtbl *lpVtblFuncProv;            // 함수 공급자(설정 옵션 노출)
    const ITfFnConfigureVtbl *lpVtblFnConfig;           // "옵션" 버튼 → 설정창
    ITfThreadMgrEventSinkVtbl *lpVtblTMES;              // 문서 포커스 추적(문서 '관여')
    ITfTextEditSinkVtbl *lpVtblTES;                     // 포커스 문서 텍스트편집 싱크
    LONG refCount;
    ITfThreadMgr *threadMgr;
    TfClientId clientId;
    DWORD activateFlags;   // ActivateEx 로 받은 플래그 (Activate 경유면 0). UI-less 판단은 Phase 3 에서.
    TfGuidAtom daAtom;   // registered atom for GUID_JamotongComposingDA
    DWORD tmesCookie;    // ThreadMgrEventSink advise 쿠키
    DWORD tesCookie;     // TextEditSink advise 쿠키
    ITfContext *pTESContext;   // TextEditSink이 붙은 현재 컨텍스트

    // 조합 미리보기 오버레이(RFC-0002)용: 마지막 편집 세션에서 얻은 캐럿 화면 rect.
    //   편집 세션(동기) 안에서 GetTextExt로 기록 → OutputResult가 세션 반환 직후 읽음(입력 스레드 전용).
    RECT lastCaretRect;
    BOOL lastCaretValid;
    // CUAS 낡은 좌표 보정용: 직전에 칩을 그린 '원시' rect. CUAS는 비동기 삽입 때문에
    // GetTextExt/캐럿이 한 키 늦게 전진한다 — 커밋이 있었는데 rect가 그대로면 낡은 것.
    RECT prevChipRect;
    BOOL prevChipValid;
    int  chipPendingAdv;   // 낡은 rect가 여러 키 지속(빠른 타이핑)될 때의 누적 보정 폭(px)

    // ── RFC-0010 문서 인라인 표준 composition (비단명 컨텍스트, comp_inline.c) ──
    const ITfCompositionSinkVtbl *lpVtblCompSink;   // 외부 종료 통지 sink
    ITfComposition *pComposition;   // 활성 문서 composition (입력 스레드 전용)
    ITfContext *pCompContext;       // composition 소유 컨텍스트 (AddRef 보유)
    ITfContext *pPathContext;       // 경로 판정 캐시 대상 (weak — 포인터 비교 전용)
    int  pathKind;                  // JamoPathKind (pPathContext에 대한 판정)
    int  pathDemerits;              // 갱신 생존 없는 연속 외부 종료 카운트 (강등용)
    BOOL compUpdatedOnce;           // 현 composition이 갱신에서 생존했는가 (강등 리셋 근거)

    // 무간섭(직접 입력) 모드 — 원격 데스크톱 등에서 해제 단축키 외 모든 키를 통과.
    // TIP 인스턴스는 프로세스별이므로 상태의 원본은 HKCU 레지스트리이고(프로세스 간 공유),
    // 이 필드는 캐시다(포커스 변경·토글 시 재읽기 — text_service.c).
    BOOL passthrough;

    // ── RFC-0012 Phase 1 compartment (compartment.c) — 한/영 상태의 표준 자리 ──
    ITfCompartmentEventSinkVtbl *lpVtblCES;   // OPENCLOSE 변경 통지 sink
    ITfCompartment *cpOpenClose;   // GUID_COMPARTMENT_KEYBOARD_OPENCLOSE (thread mgr 스코프)
    ITfCompartment *cpConvMode;    // GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION
    DWORD cpCookie;                // OPENCLOSE advise 쿠키
    DWORD cpCookieConv;            // INPUTMODE_CONVERSION advise 쿠키 (한/A 표시기는 이쪽을 바꿀 수 있다)
    wchar_t cpPendingCommit;       // 밖에서 온 전환 때 확정 못 한 음절 — 다음 키 이벤트(pic 있음)에서 먼저 확정
    long  cpLastOpen, cpLastConv;  // 마지막으로 발행/수용한 값 (-1 = 아직 없음). 같으면 안 쓴다.
    BOOL  cpSelfWrite;             // 우리가 쓰는 중 — OnChange 메아리 무시
    BOOL  ctxKeyboardDisabled;     // 포커스 문맥의 KEYBOARD_DISABLED (앱이 입력기를 껐다) 캐시

    // ── RFC-0013 C preserved key (preserved.c) ──
    JamoPreservedEntry preserved[JAMO_PRESERVED_MAX];
    int preservedCount;

    // Config & Engine State
    JamotongConfig config;
    FsmContext fsm;
    ChordContext chord;       // 모아치기(동시치기) 상태 (Moachigi=1 한글 자판)
    ChordKbContext chordKb;   // 일반 코드 자판(ARTSEY류) 상태

    // UI Elements
    JamotongLangBarItem *pLangBarItem;
} JamotongTextService;

#include <stddef.h>
#define IMPL_TO_OBJ(InterfaceName, pThis) \
    ((JamotongTextService*)((char*)(pThis) - offsetof(JamotongTextService, lpVtbl##InterfaceName)))

HRESULT JamotongTextService_Create(IUnknown *pUnkOuter, REFIID riid, void **ppvObject);


// 무간섭(직접 입력) 모드 — text_service.c. 상태 원본=HKCU\Software\Jamotong\Passthrough.
BOOL Jamotong_GetPassthroughReg(void);                             // 레지스트리 읽기
void Jamotong_SetPassthrough(JamotongTextService *obj, BOOL on);   // 조합 정리+레지스트리+발행

// 밖(compartment 통지 등)에서 자판이 바뀌었을 때의 공통 뒤처리: 조합 경계 정리 + 언어바. (text_service.c)
// 키 이벤트 밖에서 자판을 바꾸기 **전에** 조합 중 음절을 확정·정리한다 (언어바 버튼 클릭·compartment 통지).
// 실기 2026-08-23: 트레이의 한/A 칩 = 우리 언어바 버튼이고, 그 클릭은 0.16.2 부터 확정 없이 Rotate 만 했다.
void Jamotong_FlushForExternalSwitch(JamotongTextService *obj);
void Jamotong_OnLayoutSwitched(JamotongTextService *obj);

// 함수 공급자/설정(ITfFnConfigure) — func_configure.c
void    FuncConfig_Init(JamotongTextService *obj);       // vtbl 포인터 설정 (Create에서)
HRESULT FuncConfig_Advise(JamotongTextService *obj);     // Activate에서 (in-session 노출)
void    FuncConfig_Unadvise(JamotongTextService *obj);   // Deactivate에서

// Registration Functions
HRESULT RegisterProfiles(void);
HRESULT UnregisterProfiles(void);
HRESULT RegisterCategories(void);
HRESULT UnregisterCategories(void);

// Class Factory
typedef struct JamotongClassFactory {
    IClassFactoryVtbl *lpVtbl;
    LONG refCount;
} JamotongClassFactory;

// Global instance count to manage DLL unloading
extern LONG g_DllRefCount;

// g_configLock은 config.h에 선언 (config.c도 접근).
