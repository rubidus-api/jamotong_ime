#include "preedit_overlay.h"
#include "edit_session.h"   // JamoDiag (JAMO_DIAG 빌드에서만 기록)
#include <string.h>

extern HINSTANCE g_hInst;

// 상태 (입력 스레드 전용 — candidate_ui와 동일 단일스레드 계약)
static HWND    g_hwnd = NULL;
static wchar_t g_text[16];              // 조합 표시 문자열 (한글 preedit는 1~2자)
static HFONT   g_font = NULL;
static wchar_t g_fontFace[32];          // 현재 글꼴 캐시 키
static int     g_fontH = 0;

#define PAD_X 4                          // 칩 좌우 여백(px)
#define PAD_Y 2

// 다크/라이트에 무난한 칩 색 (은은한 남색 배경 + 흰 글자 + 밑줄)
#define CHIP_BG   RGB(37, 61, 97)
#define CHIP_TEXT RGB(255, 255, 255)
#define CHIP_LINE RGB(140, 180, 255)

static void EnsureFont(const wchar_t *face, int h) {
    if (h < 12) h = 12;
    if (h > 96) h = 96;
    if (g_font && g_fontH == h && wcscmp(g_fontFace, face) == 0) return;
    if (g_font) DeleteObject(g_font);
    g_font = CreateFontW(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, face);
    g_fontH = h;
    wcsncpy(g_fontFace, face, 31); g_fontFace[31] = L'\0';
}

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            HBRUSH bg = CreateSolidBrush(CHIP_BG);
            FillRect(hdc, &rc, bg);
            DeleteObject(bg);
            if (g_text[0] && g_font) {
                HFONT of = (HFONT)SelectObject(hdc, g_font);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, CHIP_TEXT);
                TextOutW(hdc, PAD_X, PAD_Y, g_text, (int)wcslen(g_text));
                SelectObject(hdc, of);
                // 조합 표시 밑줄 (인라인 조합의 관례를 칩 안에서 재현)
                HPEN pen = CreatePen(PS_SOLID, 1, CHIP_LINE);
                HGDIOBJ op = SelectObject(hdc, pen);
                MoveToEx(hdc, PAD_X, rc.bottom - 2, NULL);
                LineTo(hdc, rc.right - PAD_X, rc.bottom - 2);
                SelectObject(hdc, op);
                DeleteObject(pen);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;   // 포커스 탈취 금지 (WS_EX_NOACTIVATE 보강)
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// 클래스 등록(1회). 실패해도 다음 Show에서 재시도하지 않도록 s_tried로 1회만.
static bool EnsureClass(void) {
    static bool s_tried = false, s_ok = false;
    if (!s_tried) {
        s_tried = true;
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"JamotongPreeditOverlay";
        s_ok = RegisterClassW(&wc) != 0;
        if (!s_ok && GetLastError() == ERROR_CLASS_ALREADY_EXISTS) s_ok = true;
    }
    return s_ok;
}

void PreeditOverlay_Show(const RECT *rcCaret, const wchar_t *text, const wchar_t *fontFace, int fixedSize) {
    if (!rcCaret || !text || !text[0]) { PreeditOverlay_Hide(); return; }
    if (!EnsureClass()) return;

    // 글꼴 크기: 고정(fixedSize>0)이면 그 값, Auto면 캐럿(줄) 높이 근사.
    // (PuTTY 등 일부 앱은 캐럿 rect가 실제 글자보다 작게 보고됨 → 고정 크기 설정으로 해결)
    int lineH;
    if (fixedSize > 0) {
        lineH = fixedSize;
    } else {
        lineH = rcCaret->bottom - rcCaret->top;
        if (lineH <= 0) lineH = 20;
    }
    EnsureFont((fontFace && fontFace[0]) ? fontFace : L"Malgun Gothic", lineH);
    if (!g_font) return;

    wcsncpy(g_text, text, 15); g_text[15] = L'\0';

    if (!g_hwnd) {
        // 클릭 통과(TRANSPARENT)·포커스 비탈취(NOACTIVATE)·항상 위(TOPMOST)·작업표시줄 제외(TOOLWINDOW)
        g_hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
            L"JamotongPreeditOverlay", L"", WS_POPUP,
            0, 0, 10, 10, NULL, NULL, g_hInst, NULL);
        if (!g_hwnd) { JamoDiag("OVERLAY CreateWindow FAIL err=%lu", GetLastError()); return; }
        SetLayeredWindowAttributes(g_hwnd, 0, 235, LWA_ALPHA);   // 약간 비치는 칩
    }

    // 텍스트 폭 측정 → 칩 크기
    int w = lineH, h = lineH + PAD_Y * 2 + 2;   // 밑줄 여유 +2
    HDC hdc = GetDC(g_hwnd);
    if (hdc) {
        HFONT of = (HFONT)SelectObject(hdc, g_font);
        SIZE sz;
        if (GetTextExtentPoint32W(hdc, g_text, (int)wcslen(g_text), &sz)) w = sz.cx + PAD_X * 2;
        SelectObject(hdc, of);
        ReleaseDC(g_hwnd, hdc);
    }

    BOOL swp = SetWindowPos(g_hwnd, HWND_TOPMOST, rcCaret->left, rcCaret->top, w, h,
                            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    JamoDiag("OVERLAY show '%lc' at (%ld,%ld) %dx%d swp=%d vis=%d",
             text[0], rcCaret->left, rcCaret->top, w, h, (int)swp, (int)IsWindowVisible(g_hwnd));
    InvalidateRect(g_hwnd, NULL, TRUE);
}

void PreeditOverlay_Hide(void) {
    if (g_hwnd) ShowWindow(g_hwnd, SW_HIDE);   // 파괴 대신 숨김(재사용 — 창 생성 비용 절약)
    g_text[0] = L'\0';
}

void PreeditOverlay_Uninitialize(void) {
    if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = NULL; }
    if (g_font) { DeleteObject(g_font); g_font = NULL; }
    g_fontFace[0] = L'\0'; g_fontH = 0; g_text[0] = L'\0';
}
