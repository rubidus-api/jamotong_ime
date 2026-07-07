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

// Text Service Instance Struct
typedef struct JamotongTextService {
    ITfTextInputProcessorVtbl *lpVtblTIP;
    ITfKeyEventSinkVtbl *lpVtblKES;                     // 키 입력(ITfKeyEventSink)
    const ITfDisplayAttributeProviderVtbl *lpVtblDAP;   // 디스플레이 속성 공급자(등록만; 커밋전용이라 미사용)
    ITfFunctionProviderVtbl *lpVtblFuncProv;            // 함수 공급자(설정 옵션 노출)
    const ITfFnConfigureVtbl *lpVtblFnConfig;           // "옵션" 버튼 → 설정창
    ITfThreadMgrEventSinkVtbl *lpVtblTMES;              // 문서 포커스 추적(문서 '관여')
    ITfTextEditSinkVtbl *lpVtblTES;                     // 포커스 문서 텍스트편집 싱크
    LONG refCount;
    ITfThreadMgr *threadMgr;
    TfClientId clientId;
    TfGuidAtom daAtom;   // registered atom for GUID_JamotongComposingDA
    DWORD tmesCookie;    // ThreadMgrEventSink advise 쿠키
    DWORD tesCookie;     // TextEditSink advise 쿠키
    ITfContext *pTESContext;   // TextEditSink이 붙은 현재 컨텍스트

    // 조합 미리보기 오버레이(RFC-0002)용: 마지막 편집 세션에서 얻은 캐럿 화면 rect.
    //   편집 세션(동기) 안에서 GetTextExt로 기록 → OutputResult가 세션 반환 직후 읽음(입력 스레드 전용).
    RECT lastCaretRect;
    BOOL lastCaretValid;

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

// 현재 자판 상태를 HKCU\Software\Jamotong 에 발행(CurrentAbbrev/CurrentName) — 트레이 모니터링
// 툴(jamotong.exe)이 읽는다. Activate·자판 전환 시 호출. (text_service.c)
void Jamotong_PublishStatus(JamotongConfig *config);

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
