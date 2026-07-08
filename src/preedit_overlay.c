#include "preedit_overlay.h"
#include "edit_session.h"   // JamoDiag (JAMO_DIAG 빌드에서만 기록)
#include <string.h>

extern HINSTANCE g_hInst;

// 상태 (입력 스레드 전용 — candidate_ui와 동일 단일스레드 계약)
static HWND    g_hwnd = NULL;
static wchar_t g_text[64];              // 조합 표시 문자열 — 한글은 1~2자지만, 다국어(로마자
                                        // 시퀀스·단어 단위 preedit 등)를 위해 여러 글자 허용
static HFONT   g_font = NULL;
static wchar_t g_fontFace[32];          // 현재 글꼴 캐시 키
static int     g_fontH = 0;
static int     g_fixedSize = 0;         // 마지막 Show의 크기 설정 (0=Auto=캐럿 높이)
static RECT    g_shownRect;             // 마지막으로 적용한 캐럿 rect (화면 좌표)
static int     g_adjustLeft = 0;        // 사후 보정 잔여 횟수 (WM_TIMER)

#define PAD_X 4                          // 칩 좌우 여백(px)
#define PAD_Y 2

// 사후 자기보정: 호스트 앱의 삽입/렌더가 비동기(CUAS)거나 글자 크기가 섞인 문서라
// Show 시점의 캐럿 rect(위치·줄높이)가 낡을 수 있다 → 표시 직후 짧게 시스템 캐럿을
// 재표본해서 실제 캐럿과 다르면 즉시 따라간다 (다음 키 입력까지 기다리지 않음).
#define ADJUST_TIMER_ID  1
#define ADJUST_INTERVAL  40              // ms — 40/80/120ms 세 번 확인
#define ADJUST_TRIES     3

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

// 포그라운드 스레드의 시스템 캐럿 화면 rect. 옛 EDIT/CUAS 앱은 시스템 캐럿을 쓰므로
// 앱이 렌더를 마친 '지금'의 위치·줄높이를 정확히 보고한다 (사후 보정의 기준).
static BOOL GuitiCaretRect(RECT *out) {
    GUITHREADINFO gti; memset(&gti, 0, sizeof(gti)); gti.cbSize = sizeof(gti);
    if (!GetGUIThreadInfo(0, &gti) || !gti.hwndCaret) return FALSE;
    if (gti.rcCaret.bottom - gti.rcCaret.top <= 0) return FALSE;
    POINT tl = { gti.rcCaret.left, gti.rcCaret.top };
    POINT br = { gti.rcCaret.right, gti.rcCaret.bottom };
    if (!ClientToScreen(gti.hwndCaret, &tl) || !ClientToScreen(gti.hwndCaret, &br)) return FALSE;
    out->left = tl.x; out->top = tl.y; out->right = br.x; out->bottom = br.y;
    return TRUE;
}

// 캐럿 rect에 맞춰 글꼴(Auto=줄높이)·칩 크기·위치를 한 번에 적용.
// face=NULL이면 현재 글꼴 캐시 유지 (타이머 보정 경로).
static void PlaceChip(const RECT *rcCaret, const wchar_t *face) {
    int lineH = (g_fixedSize > 0) ? g_fixedSize : (rcCaret->bottom - rcCaret->top);
    if (lineH <= 0) lineH = 20;
    wchar_t f[32];
    wcsncpy(f, (face && face[0]) ? face : (g_fontFace[0] ? g_fontFace : L"Malgun Gothic"), 31);
    f[31] = L'\0';
    EnsureFont(f, lineH);
    if (!g_font || !g_hwnd) return;

    // 텍스트 폭 측정 → 칩 크기 (높이는 클램프 반영된 실제 글꼴 높이 기준)
    int w = g_fontH, h = g_fontH + PAD_Y * 2 + 2;   // 밑줄 여유 +2
    HDC hdc = GetDC(g_hwnd);
    if (hdc) {
        HFONT of = (HFONT)SelectObject(hdc, g_font);
        SIZE sz;
        if (GetTextExtentPoint32W(hdc, g_text, (int)wcslen(g_text), &sz)) w = sz.cx + PAD_X * 2;
        SelectObject(hdc, of);
        ReleaseDC(g_hwnd, hdc);
    }
    g_shownRect = *rcCaret;
    SetWindowPos(g_hwnd, HWND_TOPMOST, rcCaret->left, rcCaret->top, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_hwnd, NULL, TRUE);
}

static int IAbs(int v) { return v < 0 ? -v : v; }

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (wp == ADJUST_TIMER_ID) {
                if (--g_adjustLeft <= 0) KillTimer(hwnd, ADJUST_TIMER_ID);
                RECT rc;
                if (g_text[0] && IsWindowVisible(hwnd) && GuitiCaretRect(&rc)) {
                    // '위치가 실제로 움직였을 때만' 실캐럿을 따라간다(크기도 그때 새 rect 높이로).
                    // 높이만 다른 경우는 무시 — 편집세션 rect와 시스템 캐럿 rect가 같은 자리를
                    // 항상 다른 높이로 보고하는 앱(메모장)에서, 매 키마다 Show(큰)↔보정(작은)이
                    // 번갈아 그려져 칩이 여러 번 깜빡이던 실기 발견(2026-07-08)의 원인이었다.
                    // 낡은 좌표(CUAS)·다른 크기 영역으로의 이동은 위치도 함께 변하므로 계속 잡힌다.
                    BOOL moved = IAbs(rc.left - g_shownRect.left) > 2 || IAbs(rc.top - g_shownRect.top) > 2;
                    if (moved) {
                        JamoDiag("OVERLAY adjust h=%ld->%ld", g_shownRect.bottom - g_shownRect.top,
                                 rc.bottom - rc.top);
                        PlaceChip(&rc, NULL);
                    }
                }
                return 0;
            }
            break;
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

    if (!g_hwnd) {
        // 클릭 통과(TRANSPARENT)·포커스 비탈취(NOACTIVATE)·항상 위(TOPMOST)·작업표시줄 제외(TOOLWINDOW)
        g_hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
            L"JamotongPreeditOverlay", L"", WS_POPUP,
            0, 0, 10, 10, NULL, NULL, g_hInst, NULL);
        if (!g_hwnd) { JamoDiag("OVERLAY CreateWindow FAIL err=%lu", GetLastError()); return; }
        SetLayeredWindowAttributes(g_hwnd, 0, 235, LWA_ALPHA);   // 약간 비치는 칩
    }

    wcsncpy(g_text, text, 63); g_text[63] = L'\0';
    // 글꼴 크기: 고정(fixedSize>0)이면 그 값, Auto면 캐럿(줄) 높이 근사.
    // (PuTTY 등 일부 앱은 캐럿 rect가 실제 글자보다 작게 보고됨 → 고정 크기 설정으로 해결)
    g_fixedSize = fixedSize;
    PlaceChip(rcCaret, (fontFace && fontFace[0]) ? fontFace : L"Malgun Gothic");
    JamoDiag("OVERLAY show '%lc' at (%ld,%ld) h=%d vis=%d",
             text[0], rcCaret->left, rcCaret->top, g_fontH, (int)IsWindowVisible(g_hwnd));

    // 사후 자기보정 시작: 낡은 rect(비동기 렌더·혼합 글자크기)를 실캐럿으로 40ms 간격 재확인
    g_adjustLeft = ADJUST_TRIES;
    SetTimer(g_hwnd, ADJUST_TIMER_ID, ADJUST_INTERVAL, NULL);
}

void PreeditOverlay_Hide(void) {
    if (g_hwnd) {
        KillTimer(g_hwnd, ADJUST_TIMER_ID);
        ShowWindow(g_hwnd, SW_HIDE);   // 파괴 대신 숨김(재사용 — 창 생성 비용 절약)
    }
    g_adjustLeft = 0;
    g_text[0] = L'\0';
}

void PreeditOverlay_Uninitialize(void) {
    if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = NULL; }   // 타이머는 창 파괴와 함께 소멸
    if (g_font) { DeleteObject(g_font); g_font = NULL; }
    g_fontFace[0] = L'\0'; g_fontH = 0; g_text[0] = L'\0';
    g_adjustLeft = 0;
}
