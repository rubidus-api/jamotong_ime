/* lab_tip.h — 실험체 TIP 공통 선언 (RFC-0009 §3, §5.1)
 *
 * 제품(src/)과 어떤 헤더도 공유하지 않는다. 같은 helper를 쓰면 같은 ABI/수명/HRESULT
 * 오류가 양쪽에 생겨 대조군의 가치가 사라진다(RFC-0009 §3).
 */
#ifndef LAB_TIP_H
#define LAB_TIP_H

#define COBJMACROS
#include <windows.h>
#include <msctf.h>
#include <stdbool.h>
#include <stdint.h>

#include "hangul2.h"
#include "key_policy.h"

/* ── MinGW-w64의 msctf.h에 없는 것들을 직접 선언한다 ──
   ITfDisplayAttributeProvider는 헤더에 아예 없고, TF_INVALID_GUIDATOM도 빠져 있다.
   값·시그니처는 Microsoft 공개 문서(msctf.h)와 같다. */
#ifndef TF_INVALID_GUIDATOM
#  define TF_INVALID_GUIDATOM ((TfGuidAtom)0)
#endif

#ifndef __ITfDisplayAttributeProvider_INTERFACE_DEFINED__
#define __ITfDisplayAttributeProvider_INTERFACE_DEFINED__
typedef struct ITfDisplayAttributeProvider ITfDisplayAttributeProvider;
typedef struct ITfDisplayAttributeProviderVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ITfDisplayAttributeProvider*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(ITfDisplayAttributeProvider*);
    ULONG   (STDMETHODCALLTYPE *Release)(ITfDisplayAttributeProvider*);
    HRESULT (STDMETHODCALLTYPE *EnumDisplayAttributeInfo)(ITfDisplayAttributeProvider*,
                                                          IEnumTfDisplayAttributeInfo**);
    HRESULT (STDMETHODCALLTYPE *GetDisplayAttributeInfo)(ITfDisplayAttributeProvider*,
                                                         REFGUID, ITfDisplayAttributeInfo**, BSTR*);
} ITfDisplayAttributeProviderVtbl;
struct ITfDisplayAttributeProvider { const ITfDisplayAttributeProviderVtbl *lpVtbl; };
/* {fee47777-163c-4769-996a-6e9c50ad8f54} */
EXTERN_C const GUID IID_ITfDisplayAttributeProvider_Lab;
#endif

/* 실험체 전용 GUID — 제품과 절대 겹치지 않는다 */
extern const CLSID CLSID_LabService;
extern const GUID  GUID_LabProfile;

#define LAB_DISPLAY_NAME  L"Jamotong Standard TSF Lab"
#define LAB_LANGID        MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)

/* 컨텍스트별 상태 (RFC-0009 §5.1) — 탭/창/문서를 옮겨도 섞이지 않게 */
typedef struct LabContextState {
    struct LabContextState *next;
    ITfContext      *context;
    ITfComposition  *composition;
    HangulState      hangul;
    uint32_t         generation;
} LabContextState;

typedef struct LabTextService {
    ITfTextInputProcessor tip;      /* 첫 멤버 */
    ITfKeyEventSink       key_sink;
    ITfThreadMgrEventSink thread_sink;
    ITfCompositionSink    composition_sink;
    ITfDisplayAttributeProvider attr_provider;
    LONG            ref;
    ITfThreadMgr   *thread_mgr;
    TfClientId      client_id;
    DWORD           thread_sink_cookie;
    LabContextState *contexts;
    uint32_t        next_generation;
    /* compartment를 못 읽거나 못 쓰는 호스트(PuTTY 등)를 위한 자체 상태.
       compartment가 정상인 호스트에서는 그쪽을 우선한다. */
    bool            fallback_open;
    bool            compartment_usable;
} LabTextService;

/* composition 명령. 확정과 취소를 boolean 하나로 합치지 않는다 —
   로그와 테스트에서 두 의미를 분리해야 하기 때문이다 (RFC-0009 §7.4). */
typedef enum LabCompositionCommand {
    LAB_UPDATE = 0,
    LAB_COMMIT_PREFIX,
    LAB_FINALIZE,
    LAB_CANCEL
} LabCompositionCommand;

/* 편집 세션 결과 — 요청 성공과 세션 성공을 분리한다 (RFC-0009 §5.3) */
typedef struct LabSessionResult {
    HRESULT request_hr;
    HRESULT session_hr;
    bool    callback_ran;
} LabSessionResult;

/* context_state.c */
LabContextState *Lab_FindContext(LabTextService *svc, ITfContext *ctx);
LabContextState *Lab_EnsureContext(LabTextService *svc, ITfContext *ctx);
void             Lab_ReleaseContexts(LabTextService *svc);

/* text_service.c */
LabTextService *Lab_CreateService(void);
bool Lab_IsKeyboardOpen(LabTextService *svc);
bool Lab_IsKeyboardDisabled(LabTextService *svc, ITfContext *ctx);
bool Lab_SetKeyboardOpen(LabTextService *svc, bool open);

/* composition.c — 한 키를 한 transaction으로 처리한다 */
HRESULT Lab_ApplyStep(LabTextService *svc, LabContextState *st,
                      const HangulStep *step, LabSessionResult *result);
HRESULT Lab_FinalizeComposition(LabTextService *svc, LabContextState *st,
                                LabCompositionCommand cmd);
void    Lab_ForgetComposition(LabContextState *st);
void    Lab_InitCompositionSink(LabTextService *svc);

/* display_attribute.c */
extern const GUID GUID_LabDisplayAttributeInput;
HRESULT Lab_ApplyInputAttribute(LabTextService *svc, TfEditCookie ec,
                                ITfContext *ctx, ITfRange *range);
void    Lab_InitDisplayAttribute(LabTextService *svc);
HRESULT Lab_RegisterDisplayAttributeCategory(bool add);

/* diagnostics.c — 기본 꺼짐. JAMOTONG_LAB_TRACE=1 일 때만 기록 (RFC-0009 §11.3).
   개인정보 원칙: 친 글자를 남기지 않는다. 길이·HRESULT·능력만 (§11.2). */
void Lab_Trace(const char *fmt, ...);
void Lab_TraceSession(const char *what, const LabSessionResult *r);
void Lab_TraceContextCaps(ITfContext *ctx);

/* dllmain.c */
extern HINSTANCE g_lab_instance;
void Lab_DllAddRef(void);
void Lab_DllRelease(void);

/* register.c */
HRESULT Lab_RegisterServer(void);
HRESULT Lab_UnregisterServer(void);

#endif /* LAB_TIP_H */
