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
#include "tsf_property_guids.h"

/* ── MinGW-w64의 msctf.h에 없는 것들을 직접 선언한다 ──
   ITfDisplayAttributeProvider는 헤더에 아예 없고, TF_INVALID_GUIDATOM도 빠져 있다.
   값·시그니처는 Microsoft 공개 문서(msctf.h)와 같다. */
#ifndef TF_INVALID_GUIDATOM
#  define TF_INVALID_GUIDATOM ((TfGuidAtom)0)
#endif
#define LAB_INVALID_COOKIE ((DWORD)-1)

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
                                                         REFGUID, ITfDisplayAttributeInfo**);
} ITfDisplayAttributeProviderVtbl;
struct ITfDisplayAttributeProvider { const ITfDisplayAttributeProviderVtbl *lpVtbl; };
/* {fee47777-163c-4769-996a-6e9c50ad8f54} */
EXTERN_C const GUID IID_ITfDisplayAttributeProvider_Lab;
#endif

/* 실험체 전용 GUID — 제품과 절대 겹치지 않는다 */
extern const CLSID CLSID_LabService;
extern const GUID  GUID_LabProfile;

#if (defined(LAB_TRACE_BUILD) + defined(LAB_AKEL_CONTROL_BUILD) + \
     defined(LAB_AKEL_AE_NONE_BUILD) + defined(LAB_AKEL_INSERT_FIRST_BUILD) + \
     defined(LAB_AKEL_NO_SELECTION_BUILD) + defined(LAB_AKEL_META_CONTROL_BUILD) + \
     defined(LAB_AKEL_META_LANGID_BUILD) + defined(LAB_AKEL_META_READING_BUILD) + \
     defined(LAB_AKEL_META_BOTH_BUILD) + defined(LAB_AKEL_META_R2_CONTROL_BUILD) + \
     defined(LAB_AKEL_META_R2_LANGID_BUILD) + defined(LAB_AKEL_META_R2_READING_BUILD) + \
     defined(LAB_AKEL_META_R2_BOTH_BUILD) + defined(LAB_AKEL_META_R3_READING_BUILD) + \
     defined(LAB_AKEL_META_R3_LANGID_READING_BUILD) + \
     defined(LAB_AKEL_META_R3_READING_LANGID_BUILD) + \
     defined(LAB_AKEL_META_R3_LANGID_ONCE_READING_BUILD) + \
     defined(LAB_AKEL_META_R4_READING_PROBE_BUILD)) > 1
#  error "Select exactly one standard-lab build variant"
#endif

#if defined(LAB_AKEL_META_R4_READING_PROBE_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Meta R4 Reading Probe"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-meta-r4-reading-probe-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "meta-r4-reading-probe"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_META_R3_READING_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Meta R3 0 Reading"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-meta-r3-0-reading-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "meta-r3-reading"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_META_R3_LANGID_READING_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Meta R3 1 LangID Reading"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-meta-r3-1-langid-reading-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "meta-r3-langid-reading"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_META_R3_READING_LANGID_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Meta R3 2 Reading LangID"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-meta-r3-2-reading-langid-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "meta-r3-reading-langid"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_META_R3_LANGID_ONCE_READING_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Meta R3 3 LangID Once Reading"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-meta-r3-3-langid-once-reading-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "meta-r3-langid-once-reading"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_META_R2_CONTROL_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Meta R2 0 Control"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-meta-r2-0-control-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "meta-r2-control"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_META_R2_LANGID_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Meta R2 1 LangID"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-meta-r2-1-langid-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "meta-r2-langid"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_META_R2_READING_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Meta R2 2 Reading"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-meta-r2-2-reading-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "meta-r2-reading"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_META_R2_BOTH_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Meta R2 3 LangID Reading"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-meta-r2-3-langid-reading-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "meta-r2-langid-reading"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_META_CONTROL_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Meta 0 Control"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-meta-0-control-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "meta-control"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_META_LANGID_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Meta 1 LangID"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-meta-1-langid-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "meta-langid"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_META_READING_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Meta 2 Reading"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-meta-2-reading-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "meta-reading"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_META_BOTH_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Meta 3 LangID Reading"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-meta-3-langid-reading-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "meta-langid-reading"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_CONTROL_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Test 0 Control"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-test-0-control-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "control"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_AE_NONE_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Test 1 AE None"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-test-1-ae-none-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "ae-none"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_INSERT_FIRST_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Test 2 Insert First"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-test-2-insert-first-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "insert-first"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_AKEL_NO_SELECTION_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Test 3 No Selection"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-test-3-no-selection-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "no-selection"
#  define LAB_ALWAYS_TRACE       1
#elif defined(LAB_TRACE_BUILD)
#  define LAB_DISPLAY_NAME       L"Jamotong TSF Trace Lab"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-tsf-trace-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "trace-control"
#  define LAB_ALWAYS_TRACE       1
#else
#  define LAB_DISPLAY_NAME       L"Jamotong Standard TSF Lab"
#  define LAB_TRACE_FILE_FORMAT  L"jamotong-standard-lab-trace-%lu.jsonl"
#  define LAB_TRACE_VARIANT      "standard"
#endif

#if defined(LAB_AKEL_META_R4_READING_PROBE_BUILD)
#  define LAB_TRACE_SCHEMA       4
#  define LAB_TRACE_BUILD_ID     "akel-meta-r4-260724"
#elif defined(LAB_AKEL_META_R3_READING_BUILD) || \
    defined(LAB_AKEL_META_R3_LANGID_READING_BUILD) || \
    defined(LAB_AKEL_META_R3_READING_LANGID_BUILD) || \
    defined(LAB_AKEL_META_R3_LANGID_ONCE_READING_BUILD)
#  define LAB_TRACE_SCHEMA       3
#  define LAB_TRACE_BUILD_ID     "akel-meta-r3-260724"
#elif defined(LAB_AKEL_META_R2_CONTROL_BUILD) || defined(LAB_AKEL_META_R2_LANGID_BUILD) || \
    defined(LAB_AKEL_META_R2_READING_BUILD) || defined(LAB_AKEL_META_R2_BOTH_BUILD)
#  define LAB_TRACE_SCHEMA       2
#  define LAB_TRACE_BUILD_ID     "akel-meta-r2-260724"
#else
#  define LAB_TRACE_SCHEMA       1
#  define LAB_TRACE_BUILD_ID     LAB_TRACE_VARIANT
#endif

#define LAB_LANGID        MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)

#ifdef LAB_AKEL_META_R4_READING_PROBE_BUILD
typedef enum LabMetadataR4Mode {
    LAB_META_R4_INVALID = -1,
    LAB_META_R4_BASELINE = 0,
    LAB_META_R4_TRACE_CONTROL = 1,
    LAB_META_R4_READBACK = 2
} LabMetadataR4Mode;
#endif

/* Trace callbacks report the current transaction stage without recording input data. */
typedef enum LabTracePhase {
    LAB_TRACE_NONE = 0,
    LAB_TRACE_REQUEST,
    LAB_TRACE_EDIT_SESSION,
    LAB_TRACE_GET_RANGE,
    LAB_TRACE_INSERT_QUERY,
    LAB_TRACE_INSERT_TEXT,
    LAB_TRACE_START_COMPOSITION,
    LAB_TRACE_SET_TEXT,
    LAB_TRACE_METADATA,
    LAB_TRACE_DISPLAY_ATTRIBUTE,
    LAB_TRACE_SET_SELECTION,
    LAB_TRACE_COMMIT_PREFIX,
    LAB_TRACE_END_COMPOSITION
} LabTracePhase;

/* 컨텍스트별 상태 (RFC-0009 §5.1) — 탭/창/문서를 옮겨도 섞이지 않게 */
typedef struct LabContextState {
    struct LabContextState *next;
    ITfContext      *context;
    ITfComposition  *composition;
    HangulState      hangul;
    uint32_t         generation;
    DWORD            text_edit_sink_cookie;
    uint32_t         active_transaction;
    uint32_t         termination_epoch;
    LabTracePhase    trace_phase;
    /* R3 profile 3 schedules LANGID only on the first metadata update in this context.
       This intentionally survives an externally terminated composition. */
    bool              metadata_langid_once_done;
} LabContextState;

typedef struct LabTextService {
    ITfTextInputProcessor tip;      /* 첫 멤버 */
    ITfKeyEventSink       key_sink;
    ITfThreadMgrEventSink thread_sink;
    ITfCompositionSink    composition_sink;
    ITfTextEditSink       text_edit_sink;
    ITfDisplayAttributeProvider attr_provider;
    LONG            ref;
    ITfThreadMgr   *thread_mgr;
    TfClientId      client_id;
    DWORD           thread_sink_cookie;
    LabContextState *contexts;
    uint32_t        next_generation;
    uint32_t        next_transaction;
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
bool             Lab_RemoveContext(LabTextService *svc, ITfContext *ctx);
void             Lab_ReleaseContexts(LabTextService *svc);

/* text_service.c */
LabTextService *Lab_CreateService(void);
bool Lab_IsKeyboardOpen(LabTextService *svc);
bool Lab_IsKeyboardDisabled(LabTextService *svc, ITfContext *ctx);
bool Lab_SetKeyboardOpen(LabTextService *svc, bool open);
void Lab_InitTextEditSink(LabTextService *svc);

/* composition.c — 한 키를 한 transaction으로 처리한다 */
HRESULT Lab_ApplyStep(LabTextService *svc, LabContextState *st,
                      const HangulStep *step, LabSessionResult *result);
HRESULT Lab_FinalizeComposition(LabTextService *svc, LabContextState *st,
                                LabCompositionCommand cmd);
void    Lab_ForgetComposition(LabContextState *st);
void    Lab_InitCompositionSink(LabTextService *svc);
#ifdef LAB_AKEL_META_R4_READING_PROBE_BUILD
LabMetadataR4Mode Lab_GetMetadataR4Mode(void);
#endif

/* display_attribute.c */
extern const GUID GUID_LabDisplayAttributeInput;
HRESULT Lab_ApplyInputAttribute(LabTextService *svc, TfEditCookie ec,
                                ITfContext *ctx, ITfRange *range);
void    Lab_InitDisplayAttribute(LabTextService *svc);
HRESULT Lab_RegisterDisplayAttributeCategory(bool add);

/* diagnostics.c — trace build는 항상, 표준 build는 JAMOTONG_LAB_TRACE=1일 때만 기록.
   개인정보 원칙: 글자·키값·포인터·문서 내용은 기록하지 않는다. */
void Lab_TraceEvent(const char *event, const LabContextState *st, HRESULT hr, LONG value);
void Lab_TraceSession(const char *what, const LabContextState *st,
                      const LabSessionResult *r, HRESULT inner_hr);
void Lab_TraceContextCaps(const LabContextState *st);

/* dllmain.c */
extern HINSTANCE g_lab_instance;
void Lab_DllAddRef(void);
void Lab_DllRelease(void);

/* register.c */
HRESULT Lab_RegisterServer(void);
HRESULT Lab_UnregisterServer(void);

#endif /* LAB_TIP_H */
