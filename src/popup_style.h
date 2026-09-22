#pragma once
// 팝업 공통 — 작업영역 클램프·DPI 배율·고대비 색 (RFC-0008 W2-03).
//   후보창·코드 입력·조합 칩이 같이 쓴다. 순수 함수(ClampRect·Scale·PickColors)는 네이티브 시험 대상.
#include <windows.h>
#include <stdbool.h>

// 팝업 색 한 벌. 평소 색은 각 팝업이 정하고(소유자가 고른 모양 그대로), 고대비일 때만 시스템 색으로 바꾼다.
typedef struct {
    COLORREF bg, text, dim, accent, selBg, selText;
} PopupColors;

// (x,y,w,h) 를 작업영역 work 안으로 옮긴다. 아래로 넘치면 anchorTop(캐럿 줄 위) 위로 뒤집고,
// 그래도 안 되면 바닥에 맞춘다. 오른쪽으로 넘치면 왼쪽으로 민다.
void Popup_ClampRect(const RECT *work, int anchorTop, int w, int h, int *x, int *y);

// (x,y) 에 가장 가까운 모니터의 작업영역. 실패하면 false.
bool Popup_WorkAreaAt(int x, int y, RECT *out);

// (x,y) 가 속한 모니터(가장 가까운)의 작업영역으로 Popup_ClampRect.
void Popup_ClampToMonitor(int anchorTop, int w, int h, int *x, int *y);

// 100%(96 DPI) 기준 픽셀 → dpi 픽셀. dpi 0 은 96 으로 본다.
int  Popup_Scale(int px, UINT dpi);

// 창의 DPI. 호스트가 DPI 를 모르면 96(Windows 가 비트맵 확대), 알면 창이 놓인 자리의 DPI
// (2026-08-24 결정: DPI 컨텍스트는 설정하지 않고, 인지 호스트에서는 GetDpiForWindow 로 스스로 맞춘다).
UINT Popup_WindowDpi(HWND hwnd);

// 고대비 테마가 켜져 있는가.
bool Popup_HighContrast(void);

// hc 이면 시스템 색(창/글자/회색 글자/선택), 아니면 normal 그대로.
void Popup_PickColors(bool hc, const PopupColors *normal, PopupColors *out);
