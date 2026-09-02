#pragma once
// RFC-0012 Phase 3 — TSF UI element 글루. 모든 자체 UI(한자 후보창·조합 미리보기 칩·코드입력
// 팝업)는 화면에 뜨기 **전에** `ITfUIElementMgr::BeginUIElement` 를 지나고, 호스트의 답
// (*pbShow) 을 존중한다:
//   TRUE  = 우리가 우리 창을 그린다 (기존 동작; EndUIElement 는 반드시 부른다)
//   FALSE = 호스트(UI-less 앱·게임·Store 앱)가 그린다/숨긴다 — 우리는 창을 만들지 않고,
//           후보 목록은 UpdateUIElement 로 내용을 계속 넘긴다(ITfCandidateListUIElement).
// 후보 이동은 이미 페이지 단위다(공식 문서 요구) — 스크롤 아님.
//
// 킬스위치 config.options.useUIElements (config.ini `UseUIElements=0`) — 끄면 전부 기존 동작.
// LL 훅·기존 창 경로는 유지한다(RFC-0012 §4: 대체물 실기 PASS 전 우회로 제거 금지).
#include "jamotong.h"

void UiElem_Attach(JamotongTextService *obj);   // TIP activate (threadMgr 뒤). UIElementMgr QI.
void UiElem_Detach(JamotongTextService *obj);   // TIP deactivate.

// 후보창: 세션 시작/내용변경/끝. Begin 은 "우리 창을 그려도 되는가"를 돌려준다.
BOOL UiElem_BeginCandidate(void);               // TRUE=자체 창 그리기 허용 (mgr 없음/킬스위치 포함)
void UiElem_UpdateCandidate(DWORD tfCluieFlags);// 내용/선택/페이지 변경 통지 (began 아니면 no-op)
void UiElem_EndCandidate(void);

// 커스텀 요소(칩·코드입력): 보이기 직전 Begin(허용 여부 반환), 숨길 때 End.
BOOL UiElem_BeginChip(void);
void UiElem_EndChip(void);
BOOL UiElem_BeginCode(void);
void UiElem_EndCode(void);

// 후보창 쪽이 요소에 내줄 자료 접근자 (candidate_ui.c 구현)
int  CandidateUI_ElemCount(void);
int  CandidateUI_ElemSelection(void);           // 전체 인덱스 (page*perPage+sel)
const wchar_t *CandidateUI_ElemString(int idx); // idx 범위 밖이면 NULL
int  CandidateUI_ElemPerPage(void);
int  CandidateUI_ElemPage(void);
void CandidateUI_ElemHostShow(BOOL show);       // 호스트의 ITfUIElement::Show 반영 (자체 창 표시/숨김)
BOOL CandidateUI_ElemIsShown(void);
