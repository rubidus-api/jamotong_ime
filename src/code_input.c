#include "code_input.h"
#include <stdio.h>

extern HINSTANCE g_hInst;

// 상태 (입력 스레드 전용 — candidate_ui와 동일 단일스레드 계약)
static HWND    g_hwnd = NULL;
static wchar_t g_hex[8];      // 입력 중 16진수 (최대 6자리)
static int     g_hexLen = 0;
static HFONT   g_fontUi = NULL, g_fontBig = NULL;

#define CI_W 240
#define CI_H 96

static unsigned CurCodepoint(void) {
    if (g_hexLen < 2) return 0;                     // 최소 2자리
    unsigned cp = 0;
    for (int i = 0; i < g_hexLen; i++) {
        wchar_t c = g_hex[i];
        cp = cp * 16 + (unsigned)(c <= L'9' ? c - L'0' : (c | 0x20) - L'a' + 10);
    }
    if (cp < 0x20 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;   // 제어/서로게이트 제외
    return cp;
}

static void EnsureFonts(void) {
    if (!g_fontUi)
        g_fontUi = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
    if (!g_fontBig)
        g_fontBig = CreateFontW(40, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
}

static LRESULT CALLBACK CodeInputWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
            EnsureFonts();
            SetBkMode(hdc, TRANSPARENT);

            HFONT of = (HFONT)SelectObject(hdc, g_fontUi);
            wchar_t line[32];
            swprintf(line, 32, L"U+%s_", g_hexLen ? g_hex : L"");
            SetTextColor(hdc, RGB(0, 0, 0));
            TextOutW(hdc, 10, 8, line, (int)wcslen(line));
            SetTextColor(hdc, RGB(128, 128, 128));
            TextOutW(hdc, 10, CI_H - 24, L"hex 2~6\xC790\xB9AC \xB2E4\xC74C Enter", 17);   // "hex 2~6자리 다음 Enter"

            // 실시간 미리보기 (팝업 안에서 — 문서 range 편집 불필요)
            unsigned cp = CurCodepoint();
            SelectObject(hdc, g_fontBig);
            if (cp) {
                wchar_t prev[3];
                if (cp <= 0xFFFF) { prev[0] = (wchar_t)cp; prev[1] = 0; }
                else {
                    unsigned v = cp - 0x10000;
                    prev[0] = (wchar_t)(0xD800 + (v >> 10)); prev[1] = (wchar_t)(0xDC00 + (v & 0x3FF)); prev[2] = 0;
                }
                SetTextColor(hdc, RGB(0, 60, 160));
                TextOutW(hdc, CI_W - 70, 24, prev, (int)wcslen(prev));
            }
            SelectObject(hdc, of);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool EnsureClass(void) {
    static bool s_tried = false, s_ok = false;
    if (!s_tried) {
        s_tried = true;
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = CodeInputWndProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"JamotongCodeInput";
        s_ok = RegisterClassW(&wc) != 0;
        if (!s_ok && GetLastError() == ERROR_CLASS_ALREADY_EXISTS) s_ok = true;
    }
    return s_ok;
}

void CodeInput_Show(int x, int y) {
    if (!EnsureClass()) return;
    g_hexLen = 0; g_hex[0] = L'\0';
    if (!g_hwnd) {
        g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"JamotongCodeInput", L"", WS_POPUP | WS_BORDER,
            x, y, CI_W, CI_H, NULL, NULL, g_hInst, NULL);
        if (!g_hwnd) return;
    }
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, CI_W, CI_H, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_hwnd, NULL, TRUE);
}

bool CodeInput_IsVisible(void) { return g_hwnd && IsWindowVisible(g_hwnd); }

// VK → 16진수 문자 (0-9, A-F; 숫자패드 포함). 아니면 0.
static wchar_t HexCharFromVK(UINT vKey, bool shift) {
    if (vKey >= '0' && vKey <= '9' && !shift) return (wchar_t)vKey;
    if (vKey >= VK_NUMPAD0 && vKey <= VK_NUMPAD9) return (wchar_t)('0' + (vKey - VK_NUMPAD0));
    if (vKey >= 'A' && vKey <= 'F') return (wchar_t)vKey;   // 대문자로 저장
    return 0;
}

bool CodeInput_HandleKey(UINT vKey, bool shift, unsigned *outCodepoint) {
    if (outCodepoint) *outCodepoint = 0;
    if (!CodeInput_IsVisible()) return false;

    if (vKey == VK_ESCAPE) { CodeInput_Hide(); return true; }
    if (vKey == VK_RETURN) {
        unsigned cp = CurCodepoint();
        if (cp && outCodepoint) *outCodepoint = cp;
        CodeInput_Hide();
        return true;
    }
    if (vKey == VK_BACK) {
        if (g_hexLen > 0) { g_hex[--g_hexLen] = L'\0'; InvalidateRect(g_hwnd, NULL, TRUE); }
        return true;
    }
    wchar_t hc = HexCharFromVK(vKey, shift);
    if (hc && g_hexLen < 6) {
        g_hex[g_hexLen++] = hc; g_hex[g_hexLen] = L'\0';
        InvalidateRect(g_hwnd, NULL, TRUE);
        return true;
    }
    return true;   // 열려 있는 동안 다른 키는 전부 소비 (앱 오입력 방지)
}

void CodeInput_Hide(void) {
    if (g_hwnd) ShowWindow(g_hwnd, SW_HIDE);
    g_hexLen = 0; g_hex[0] = L'\0';
}

void CodeInput_Uninitialize(void) {
    if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = NULL; }
    if (g_fontUi)  { DeleteObject(g_fontUi);  g_fontUi = NULL; }
    if (g_fontBig) { DeleteObject(g_fontBig); g_fontBig = NULL; }
}
