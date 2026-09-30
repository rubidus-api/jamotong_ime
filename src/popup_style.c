// popup_style.c — 팝업 공통의 순수 계산 (RFC-0008 W2-03). Windows 호출은 popup_style_win.c.
#include "popup_style.h"

void Popup_ClampRect(const RECT *work, int anchorTop, int w, int h, int *x, int *y) {
    const int caretX = *x, lineBottom = *y;            // 캐럿 줄: 가로 caretX 부터, 세로 anchorTop..lineBottom
    bool sideways = false;
    if (*y + h > work->bottom) {
        int above = anchorTop - h - 4;                  // 캐럿 줄 위로 뒤집기 (조합 줄을 덮지 않음)
        if (above >= work->top) *y = above;
        else { *y = work->bottom - h; sideways = true; }   // 위도 아래도 모자라다 (B15)
    }
    if (*y < work->top) *y = work->top;
    // 위·아래 어디에도 들어가지 않으면 바닥에 맞춘 채 캐럿 옆으로 비켜 선다 — 캐럿 줄을 덮지 않게 (B15,
    //   150%·800px 에서 9 줄 후보창). 오른쪽이 되면 오른쪽, 아니면 왼쪽, 둘 다 안 되면 예전처럼 덮는다.
    if (sideways && *y < lineBottom && *y + h > anchorTop) {
        const int gap = POPUP_SIDE_GAP;
        if (caretX + gap + w <= work->right) *x = caretX + gap;
        else if (caretX - 8 - w >= work->left) *x = caretX - 8 - w;
    }
    if (*x + w > work->right) *x = work->right - w;
    if (*x < work->left)      *x = work->left;          // 작업영역보다 넓으면 왼쪽 가장자리 우선
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
