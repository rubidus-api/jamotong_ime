// popup_style.c — 팝업 공통의 순수 계산 (RFC-0008 W2-03). Windows 호출은 popup_style_win.c.
#include "popup_style.h"

void Popup_ClampRect(const RECT *work, int anchorTop, int w, int h, int *x, int *y) {
    if (*x + w > work->right) *x = work->right - w;
    if (*x < work->left)      *x = work->left;          // 작업영역보다 넓으면 왼쪽 가장자리 우선
    if (*y + h > work->bottom) {
        int above = anchorTop - h - 4;                  // 캐럿 줄 위로 뒤집기 (조합 줄을 덮지 않음)
        *y = (above >= work->top) ? above : work->bottom - h;
    }
    if (*y < work->top) *y = work->top;
}

int Popup_Scale(int px, UINT dpi) {
    if (dpi == 0) dpi = 96;
    long long v = (long long)px * (long long)dpi;
    return (int)((v + (v >= 0 ? 48 : -48)) / 96);      // 반올림 (MulDiv 와 같게)
}

void Popup_PickColors(bool hc, const PopupColors *normal, PopupColors *out) {
    if (!hc) { *out = *normal; return; }
    out->bg      = GetSysColor(COLOR_WINDOW);
    out->text    = GetSysColor(COLOR_WINDOWTEXT);
    out->dim     = GetSysColor(COLOR_GRAYTEXT);
    out->accent  = GetSysColor(COLOR_WINDOWTEXT);      // 강조색도 테마 글자색 — 대비를 테마에 맡긴다
    out->selBg   = GetSysColor(COLOR_HIGHLIGHT);
    out->selText = GetSysColor(COLOR_HIGHLIGHTTEXT);
}
