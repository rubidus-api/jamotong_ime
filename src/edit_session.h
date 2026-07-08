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
// 캐럿 앞의 word를 EDIT 메시지로 선택하고 읽어서 검증(성공 시 선택 유지 — 이어지는 삽입이
// 선택을 교체). CUAS에서 range 교체가 부분 적용되는 문제의 우회 경로 (단어 한자 변환용).
bool EditCtl_SelectWordBeforeCaret(const wchar_t *word);
// 포커스 EDIT 계열 컨트롤의 현재 선택을 str로 교체(EM_REPLACESEL). 비-EDIT면 false.
// TSF 삽입이 CUAS에서 선택을 부분 교체하던 문제의 우회 (블록/단어 한자 변환 공통).
bool EditCtl_ReplaceSelection(const wchar_t *str);

void JamoDiag(const char *fmt, ...);   // JAMO_DIAG 빌드에서만 기록, 아니면 no-op
