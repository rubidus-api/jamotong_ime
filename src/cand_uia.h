#pragma once
// 후보창의 UI 자동화 제공자 (RFC-0008 W2-03 / B10).
//   내레이터 같은 화면 읽기 도구는 창을 **UI 자동화**로 본다. 우리 후보창은 직접 그린 팝업이라
//   아무 정보도 없었다 — 뜬 것도, 무엇이 골라졌는지도 알 수 없었다. 최소한의 제공자를 달아
//   "목록"이라는 종류와 지금 후보를 이름으로 알린다.
#include <windows.h>

// WM_GETOBJECT 응답. 후보창 WndProc 이 그대로 넘긴다.
LRESULT CandUia_OnGetObject(HWND hwnd, WPARAM wParam, LPARAM lParam);
// 지금 읽어 줄 이름 (예: "후보 2/9: 儺"). 바뀌면 자동화 이벤트도 함께 낸다.
void    CandUia_SetName(HWND hwnd, const wchar_t *name);
// 창이 사라질 때 제공자를 놓는다.
void    CandUia_Release(void);
