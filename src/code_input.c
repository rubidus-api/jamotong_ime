#include "code_input.h"
#include "edit_session.h"   // JamoDiag
#include "ui_element.h"
#include "hanja_dict.h"   // 한자 코드포인트 → 훈음/음 이름 표시
#include <stdio.h>

extern HINSTANCE g_hInst;

// 상태 (입력 스레드 전용 — candidate_ui와 동일 단일스레드 계약)

// ── RFC-0008 W0-03 S2: 이 UI 창의 소유 스레드 ──────────────────────────────────────
// 창은 프로세스당 하나가 맞다(한 번에 하나만 보인다 — 스레드마다 만들면 그게 회귀다).
// 다만 **만든 스레드만** 만져야 한다: 다른 입력 스레드가 같은 HWND 를 조작하면 cross-thread
// 창 조작이 되고, 그 스레드가 죽은 뒤엔 해제된 창을 만지게 된다.
// 지금은 **진단만** 남긴다. 실기(2026-09-21, 메모장·UWP 검색·헬퍼 경로)에서 교차 호출은 0 이었는데,
// 그건 '차단해도 안전하다'가 아니라 '아직 못 봤다'는 뜻이다 — 차단으로 올리면 우리가 못 본
// 정상 경로에서 후보창이 안 뜨는 새 회귀를 만든다. 로그에 실제로 찍히면 그때 올린다.
static DWORD g_ownerTid = 0;

static void OwnerThreadClaim(void) { g_ownerTid = GetCurrentThreadId(); }

static bool OwnerThreadGuard(const char *what) {
    DWORD me = GetCurrentThreadId();
    if (g_ownerTid && g_ownerTid != me) {
        JamoDiag("CODE cross-thread %s owner=%lu me=%lu", what,
                 (unsigned long)g_ownerTid, (unsigned long)me);
        return false;   // 호출자는 이 값을 아직 쓰지 않는다(진단 단계)
    }
    return true;
}

static HWND    g_hwnd = NULL;
static wchar_t g_hex[8];      // 입력 중 16진수 (최대 6자리)
static int     g_hexLen = 0;
static HFONT   g_fontUi = NULL, g_fontBig = NULL;

#define CI_W 300
#define CI_H 118

// 코드포인트의 사람이 읽는 이름: 한자면 훈음("집 가")·음("가"), 아니면 유니코드 블록명.
//   out 은 32자 이상. 빈 문자열이면 표시 생략.
static void CodepointName(unsigned cp, wchar_t *out, int cap) {
    out[0] = L'\0';
    bool cjk = (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0xF900 && cp <= 0xFAFF);
    if (cjk && cp <= 0xFFFF) {
        const wchar_t *hn = HunumDict_Find((wchar_t)cp);   // "집 가"
        if (hn && hn[0]) { lstrcpynW(out, hn, cap); return; }
        wchar_t rd = HanjaDict_ReadingOf((wchar_t)cp);      // 훈음 미수록 → 음만
        if (rd) { out[0] = rd; out[1] = L'\0'; return; }
    }
    const wchar_t *b =
        ((cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F)) ? L"Hangul" :
        (cjk) ? L"CJK (hanja)" :
        (cp < 0x80) ? L"Basic Latin" :
        (cp <= 0x24F) ? L"Latin" :
        (cp >= 0x3040 && cp <= 0x30FF) ? L"Kana" :
        (cp >= 0x2000 && cp <= 0x206F) ? L"Punctuation" :
        (cp >= 0x2190 && cp <= 0x21FF) ? L"Arrows" :
        (cp >= 0x2200 && cp <= 0x22FF) ? L"Math operators" :
        (cp >= 0x2460 && cp <= 0x24FF) ? L"Enclosed alphanumerics" :
        (cp >= 0x2500 && cp <= 0x257F) ? L"Box drawing" :
        (cp >= 0x2600 && cp <= 0x27BF) ? L"Symbols" :
        (cp >= 0x3000 && cp <= 0x303F) ? L"CJK symbols/punctuation" :
        (cp >= 0xFF00 && cp <= 0xFFEF) ? L"Fullwidth forms" :
        (cp >= 0x1F300 && cp <= 0x1FAFF) ? L"Emoji" : L"";
    lstrcpynW(out, b, cap);
}

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
            TextOutW(hdc, 10, CI_H - 22, L"hex 2-6 digits, then Enter", 26);

            unsigned cp = CurCodepoint();
            // 문자명 (한자면 훈음/음, 아니면 블록명) — hex 아래 회색 한 줄
            if (cp) {
                wchar_t name[40]; CodepointName(cp, name, 40);
                if (name[0]) {
                    SetTextColor(hdc, RGB(90, 90, 90));
                    TextOutW(hdc, 10, 32, name, (int)wcslen(name));
                }
            }

            // 실시간 미리보기 (팝업 안에서 — 문서 range 편집 불필요)
            SelectObject(hdc, g_fontBig);
            if (cp) {
                wchar_t prev[3];
                if (cp <= 0xFFFF) { prev[0] = (wchar_t)cp; prev[1] = 0; }
                else {
                    unsigned v = cp - 0x10000;
                    prev[0] = (wchar_t)(0xD800 + (v >> 10)); prev[1] = (wchar_t)(0xDC00 + (v & 0x3FF)); prev[2] = 0;
                }
                SetTextColor(hdc, RGB(0, 60, 160));
                TextOutW(hdc, CI_W - 66, 20, prev, (int)wcslen(prev));
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

// 창 없는 모드(UWP 호스트): 상태만 유지하고 그리기는 데스크톱 헬퍼가 한다(RFC-0015 Phase 2).
static bool g_windowless = false;
static bool g_wlActive = false;   // 창 없는 모드에서 '떠 있음' 상태

const wchar_t *CodeInput_DisplayText(void) {
    static wchar_t line[32];
    _snwprintf(line, 32, L"U+%ls_", g_hexLen ? g_hex : L"");
    line[31] = L'\0';
    return line;
}

void CodeInput_ShowWindowless(void) {   // 창을 만들지 않고 입력만 받는다
    g_windowless = true;
    g_wlActive = true;
    g_hexLen = 0; g_hex[0] = L'\0';
}

void CodeInput_Show(int x, int y) {
    g_windowless = false;
    g_wlActive = false;
    if (!UiElem_BeginCode()) return;   // RFC-0012 Phase 3 게이트 (재호출 안전)
    if (!EnsureClass()) return;
    g_hexLen = 0; g_hex[0] = L'\0';
    if (!g_hwnd) {
        g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"JamotongCodeInput", L"", WS_POPUP | WS_BORDER,
            x, y, CI_W, CI_H, NULL, NULL, g_hInst, NULL);
        if (!g_hwnd) return;
        OwnerThreadClaim();   // 이 창은 이 스레드 것이다 (W0-03 S2)
    }
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, CI_W, CI_H, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_hwnd, NULL, TRUE);
}

bool CodeInput_IsVisible(void) { return g_wlActive || (g_hwnd && IsWindowVisible(g_hwnd)); }

// VK → 16진수 문자 (0-9, A-F; 숫자패드 포함). 아니면 0.
static wchar_t HexCharFromVK(UINT vKey, bool shift) {
    if (vKey >= '0' && vKey <= '9' && !shift) return (wchar_t)vKey;
    if (vKey >= VK_NUMPAD0 && vKey <= VK_NUMPAD9) return (wchar_t)('0' + (vKey - VK_NUMPAD0));
    if (vKey >= 'A' && vKey <= 'F') return (wchar_t)vKey;   // 대문자로 저장
    return 0;
}

bool CodeInput_HandleKey(UINT vKey, bool shift, unsigned *outCodepoint) {
    OwnerThreadGuard("HandleKey");
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
        if (g_hexLen > 0) { g_hex[--g_hexLen] = L'\0'; if (g_hwnd) InvalidateRect(g_hwnd, NULL, TRUE); }
        return true;
    }
    wchar_t hc = HexCharFromVK(vKey, shift);
    if (hc && g_hexLen < 6) {
        g_hex[g_hexLen++] = hc; g_hex[g_hexLen] = L'\0';
        if (g_hwnd) InvalidateRect(g_hwnd, NULL, TRUE);
        return true;
    }
    return true;   // 열려 있는 동안 다른 키는 전부 소비 (앱 오입력 방지)
}

void CodeInput_Hide(void) {
    g_wlActive = false;   // 창 없는 모드도 함께 내린다
    UiElem_EndCode();
    if (g_hwnd) ShowWindow(g_hwnd, SW_HIDE);
    g_hexLen = 0; g_hex[0] = L'\0';
}

void CodeInput_Uninitialize(void) {
    if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = NULL; }
    if (g_fontUi)  { DeleteObject(g_fontUi);  g_fontUi = NULL; }
    if (g_fontBig) { DeleteObject(g_fontBig); g_fontBig = NULL; }
}
