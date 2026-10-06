#pragma once
#include <windows.h>
#include <stdbool.h>

// 콜백 함수 (index: 선택된 후보 인덱스, str: 선택된 문자열)
typedef void (*CandidateSelectCallback)(int index, const wchar_t *str, void *ctx);
typedef void (*CandidateCancelCallback)(void *ctx);
// 병음 방식의 글쇠 (Enter·Esc·`[`·`]`): 창은 이미 닫혔다. index = 그때 하이라이트된 후보.
typedef void (*CandidateKeyCallback)(UINT vk, int index, void *ctx);

bool CandidateUI_Initialize(void);
void CandidateUI_Uninitialize(void);

// 후보창 열기
// 후보창 스타일 지정 — 후보·훈음·페이지 표시 전 요소가 이 글꼴/크기 하나를 쓴다(12~72px 클램프).
// Show 전에 호출(설정 변경 반영). face=NULL/빈 문자열이면 글꼴은 유지하고 크기만 갱신.
void CandidateUI_SetStyle(const wchar_t *face, int sizePx);
// (x,y)=후보창 좌상단 앵커(보통 캐럿 아래), caretTop=캐럿 줄의 위쪽 y — 화면 아래로 넘치면
// 이 위로 뒤집어 배치한다. 창은 모니터 작업영역 안으로 클램프된다.
// 반환: 표시했는가(RFC-0008 W1-08). false 면 아무것도 열리지 않았고 콜백도 불리지 않는다 —
// 호출자가 넘긴 문맥(ctx 가 쥔 참조)은 호출자가 정리한다.
// ※ candidates 배열과 그 문자열은 **복사하지 않는다** — 창이 닫힐 때까지 호출자가 살려 두어야
//   한다(스택 배열을 넘기면 첫 줄만 살아남는 식으로 깨진다. 실기 2026-09-23).
// 포커스 창이 없을 때(UWP CoreWindow 스레드 등) 소유자로 쓸 창 — ITfContextView::GetWnd 의 창.
// Microsoft IME 지침: 후보창은 소유된 창이어야 앱 위에 보이고, 소유자는 GetWnd 로 얻는다.
void CandidateUI_SetViewWindow(HWND hwnd);
bool CandidateUI_Show(int x, int y, int caretTop, wchar_t **candidates, int count, int replaceLen, CandidateSelectCallback onSelect, CandidateCancelCallback onCancel, void *ctx);
// 중국어 병음 방식의 글쇠 (2026-10-03). Show 바로 앞에 부른다 — NULL 이면 예전(한자·가나) 방식이고, 창을 닫으면 풀린다.
//   사이띄개 = 하이라이트된 후보, `-` `=` = 쪽 넘김, 숫자 = 고르기(Shift 를 누른 숫자는 문장부호라 넘긴다),
//   Enter·Esc·`[`·`]` = onKey. 글자·백스페이스·문장부호는 HandleKey 가 false 를 돌려 입력기가 읽기를 고치게 한다.
void CandidateUI_SetPinyinKeys(CandidateKeyCallback onKey);
// 일본어 문절 편집의 글쇠 (0.73.0, RFC-0022 P2). 정하면 이 창에서: ↑↓·사이띄개 = 후보 옮기기(가로 후보줄이어도), 숫자 = 고르기,
//   Esc = 접기(onCancel) 는 창이 하고, ←→(문절 옮기기, Shift 와 함께면 길이)·엔터·백스페이스와 그 밖의 글쇠는 창을 닫고 onKey 로
//   넘긴다 — vk 에 Shift 면 CAND_KEY_SHIFT 를 더해서, index = 하이라이트한 후보. ←→·엔터·백스페이스는 여기서 끝나고(먹는다),
//   그 밖의 글쇠는 onKey 뒤에 입력기가 새 입력으로 이어 처리한다.
#define CAND_KEY_SHIFT 0x1000u
void CandidateUI_SetSegmentKeys(CandidateKeyCallback onKey);
// V 모드 (0.65.0): 숫자와 - = 를 고르기·쪽 넘김이 아니라 입력으로 보낸다 (사이띄개·화살표·엔터·Esc 는 그대로). Show 앞에, 창을 닫으면 풀린다.
void CandidateUI_SetDigitsToInput(bool on);
// 후보 옆에 붙일 작은 글 (0.66.0: 중국어 후보의 성조 병음). candidates 와 같은 길이의 배열, NULL·빈 문자열이면 없음.
//   복사하지 않는다 — 창이 닫힐 때까지 호출자가 살려 둔다. Show 앞에, 창을 닫으면 풀린다.
void CandidateUI_SetNotes(wchar_t **notes);
// 머리줄에 보일 바꾸는 것 (병음 읽기·한자로 바꿀 한글), 그리고 가로 후보줄 (RFC-0020 P2). Show 앞에, 창을 닫으면 풀린다.
void CandidateUI_SetTitle(const wchar_t *title);
void CandidateUI_SetHorizontal(bool on);

// 키보드 이벤트 가로채기
// true를 반환하면 UI가 이벤트를 소모한 것
bool CandidateUI_HandleKey(UINT vKey);

// 후보창 닫기 (콜백 없이 조용히)
void CandidateUI_Hide(void);

// 후보창 취소 — onCancel 콜백을 부르고 닫는다 (포커스 이동·X 버튼 등 외부 요인 종료용.
// Hide와 달리 컨텍스트 정리(pic Release 등)가 콜백에서 일어나 리소스가 안 샌다.)
void CandidateUI_Cancel(void);

// 현재 후보창이 열려있는지 여부
bool CandidateUI_IsVisible(void);

// 저장해둔 replaceLen 가져오기 (몇 글자를 지워야 하는지)
int CandidateUI_GetReplaceLen(void);
