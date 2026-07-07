#pragma once
#include <windows.h>
#include <stdbool.h>

// 콜백 함수 (index: 선택된 후보 인덱스, str: 선택된 문자열)
typedef void (*CandidateSelectCallback)(int index, const wchar_t *str, void *ctx);
typedef void (*CandidateCancelCallback)(void *ctx);

bool CandidateUI_Initialize(void);
void CandidateUI_Uninitialize(void);

// 후보창 열기
void CandidateUI_Show(int x, int y, wchar_t **candidates, int count, int replaceLen, CandidateSelectCallback onSelect, CandidateCancelCallback onCancel, void *ctx);

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
