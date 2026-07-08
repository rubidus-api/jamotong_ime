#pragma once
#include "jamotong.h"

typedef struct {
    wchar_t committed[128];
    wchar_t composing[128];
} EditSessionData;

HRESULT RequestEditSession(JamotongTextService *pService, ITfContext *pContext, FsmResult fsmRes);
HRESULT RequestEditSessionData(JamotongTextService *pService, ITfContext *pContext, const EditSessionData *data);
// 커서 앞 최대 maxLen 글자를 읽는다. ※ outBuf 용량은 최소 maxLen+1 (널 종단 기록).
HRESULT RequestReadSessionString(JamotongTextService *pService, ITfContext *pContext, wchar_t *outBuf, int maxLen);
HRESULT RequestReplaceSessionString(JamotongTextService *pService, ITfContext *pContext, int replaceLen, const wchar_t *replacement);

// [RFC-0003] 현재 선택(블록)된 텍스트를 읽는다. 선택이 없거나 실패하면 outBuf[0]=0.
//   성공 시 선택 range의 화면 rect를 svc->lastCaretRect에 캡처(후보창 위치용).
//   ※ outBuf 용량은 최소 maxLen+1 (널 종단 기록).
HRESULT RequestReadSelectionString(JamotongTextService *pService, ITfContext *pContext, wchar_t *outBuf, int maxLen);
// 포커스가 EDIT 계열이면 그 HWND, 아니면 NULL. 삽입/교체 시점에 한 번 얻어 이후 EM_* 조작에
// 재사용한다(후보창 콜백 시점엔 포커스가 옮겨가 GetGUIThreadInfo가 딴 창을 주기 때문).
HWND EditCtl_FocusEditWindow(void);
// h(EDIT 계열)에서 캐럿 앞 word를 선택하고 읽어 검증(성공 시 선택 유지 → EM_REPLACESEL로 교체).
bool EditCtl_SelectWordBeforeCaret(HWND h, const wchar_t *word);
// h의 현재 선택을 str로 교체(빈 선택이면 캐럿에 삽입). AkelEdit는 TSF 삽입을 반영 안 해
// 커밋·교체 모두 이 경로가 신뢰성 있다.
bool EditCtl_ReplaceSelection(HWND h, const wchar_t *str);

void JamoDiag(const char *fmt, ...);   // JAMO_DIAG 빌드에서만 기록, 아니면 no-op
