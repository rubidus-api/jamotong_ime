#pragma once
#include "jamotong.h"

typedef struct {
    wchar_t committed[128];
    wchar_t composing[128];
} EditSessionData;

HRESULT RequestEditSession(JamotongTextService *pService, ITfContext *pContext, FsmResult fsmRes);
HRESULT RequestEditSessionData(JamotongTextService *pService, ITfContext *pContext, const EditSessionData *data);
// 플래그 지정판. 키 이벤트 밖(compartment 통지 등)에서는 TF_ES_SYNC 가 거부될 수 있으므로
// TF_ES_ASYNCDONTCARE | TF_ES_READWRITE 로 부른다(MS SampleIME 의 _TerminateComposition 패턴).
HRESULT RequestEditSessionDataEx(JamotongTextService *pService, ITfContext *pContext, const EditSessionData *data, DWORD esFlags);
// 커서 앞 최대 maxLen 글자를 읽는다. ※ outBuf 용량은 최소 maxLen+1 (널 종단 기록).
HRESULT RequestReadSessionString(JamotongTextService *pService, ITfContext *pContext, wchar_t *outBuf, int maxLen);
HRESULT RequestReplaceSessionString(JamotongTextService *pService, ITfContext *pContext, int replaceLen, const wchar_t *replacement);

// [RFC-0003] 현재 선택(블록)된 텍스트를 읽는다. 선택이 없거나 실패하면 outBuf[0]=0.
//   성공 시 선택 range의 화면 rect를 svc->lastCaretRect에 캡처(후보창 위치용).
//   ※ outBuf 용량은 최소 maxLen+1 (널 종단 기록).
HRESULT RequestReadSelectionString(JamotongTextService *pService, ITfContext *pContext, wchar_t *outBuf, int maxLen);
// 캐럿 자리만 다시 재서, 후보창을 띄울 때의 자리와 다르면 후보창을 닫는다 (light dismiss, B10).
//   메모장 같은 최신 편집기는 고전 캐럿(GetGUIThreadInfo)이 아예 없어 TSF 로만 잴 수 있다.
//   문서 배치 싱크 안에서 부르므로 **비동기 읽기 전용** 세션이다.
HRESULT RequestCaretMoveProbe(JamotongTextService *pService, ITfContext *pContext);
// 포커스가 EDIT 계열이면 그 HWND, 아니면 NULL. 삽입/교체 시점에 한 번 얻어 이후 EM_* 조작에
// 재사용한다(후보창 콜백 시점엔 포커스가 옮겨가 GetGUIThreadInfo가 딴 창을 주기 때문).
HWND EditCtl_FocusEditWindow(void);
// h(EDIT 계열)에서 캐럿 앞 word를 선택하고 읽어 검증(성공 시 선택 유지 → EM_REPLACESEL로 교체).
bool EditCtl_SelectWordBeforeCaret(HWND h, const wchar_t *word);
// h의 현재 선택을 str로 교체(빈 선택이면 캐럿에 삽입). AkelEdit는 TSF 삽입을 반영 안 해
// 커밋·교체 모두 이 경로가 신뢰성 있다.
bool EditCtl_ReplaceSelection(HWND h, const wchar_t *str);
// 선택을 그 끝의 캐럿으로 접는다 (우리가 잡은 선택을 교체하지 못했을 때 되돌리는 용도).
void EditCtl_CollapseSelectionToEnd(HWND h);
// 컨트롤 h 의 현재 선택 텍스트(최대 maxLen 자). 읽지 못하면 false.
bool EditCtl_ReadSelection(HWND h, wchar_t *outBuf, int maxLen);

void JamoDiag(const char *fmt, ...);   // JAMO_DIAG 빌드에서만 기록, 아니면 no-op

// UI 창을 소유 스레드가 아닌 곳에서 만졌다 — **배포판에서도** 센다 (RFC-0008 W0-03 S2, B5).
//   JamoDiag 는 진단 빌드에서만 살아 있고 진단 빌드는 배포하지 않는다. 그래서 "로그에 찍히면
//   그때 차단으로 올린다"는 계획은 영원히 결론에 못 이른다. 친 글자는 절대 남기지 않고, 횟수만
//   HKCU\Software\Jamotong 의 `UiCrossThread` 에 적는다(1·2·4·8… 번째에만 써서 레지스트리를 덜 건드린다).
void UiGuard_CrossThread(const char *what, unsigned long owner, unsigned long me);

// 지금 **우리 편집 세션 안**인가. 문서 편집 싱크(OnEndEdit)가 "이 변화는 우리가 낸 것"을
// 가려내는 데 쓴다 — 후보창을 우리 확정 때문에 닫으면 안 된다 (light dismiss, B10).
bool Jamotong_InOurEdit(void);
extern long g_ourEditDepth;   // 우리 편집 세션 깊이 (edit_session.c 소유, 인라인 조합도 올린다)
