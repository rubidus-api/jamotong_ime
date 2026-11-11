#pragma once
// 설정창의 UI 자동화 제공자 (한 장 설정 페이지).
//   내레이터 같은 화면 읽기 도구는 창을 UI 자동화로 본다. 설정창은 블록을 직접 그려 자식 컨트롤이 없으므로,
//   지금 포커스를 가진 블록의 글을 창의 이름으로 알린다.
#include <windows.h>

// WM_GETOBJECT 응답. 설정창 WndProc 이 그대로 넘긴다.
LRESULT SettingsUia_OnGetObject(HWND hwnd, WPARAM wParam, LPARAM lParam);
// 지금 읽어 줄 이름 (예: "Fuzzy pinyin, checked"). 바뀌면 이름 변경·포커스 이벤트도 함께 낸다.
void    SettingsUia_SetName(HWND hwnd, const wchar_t *name);
// 창이 사라질 때 제공자를 놓는다.
void    SettingsUia_Release(void);
