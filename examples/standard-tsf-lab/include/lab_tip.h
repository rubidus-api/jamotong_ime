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
    LONG            ref;
    ITfThreadMgr   *thread_mgr;
    TfClientId      client_id;
    DWORD           thread_sink_cookie;
    LabContextState *contexts;
    uint32_t        next_generation;
} LabTextService;

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

/* dllmain.c */
extern HINSTANCE g_lab_instance;
void Lab_DllAddRef(void);
void Lab_DllRelease(void);

/* register.c */
HRESULT Lab_RegisterServer(void);
HRESULT Lab_UnregisterServer(void);

#endif /* LAB_TIP_H */
