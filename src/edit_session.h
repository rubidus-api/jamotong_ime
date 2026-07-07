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
