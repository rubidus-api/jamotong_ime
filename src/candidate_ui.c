#include "candidate_ui.h"
#include "hanja_dict.h"   // HunumDict_Find — 후보 옆 훈음(뜻·음) 표시
#include <stdio.h>

static HWND g_hwndCandi = NULL;
static wchar_t **g_candidates = NULL;
static int g_count = 0;
static int g_replaceLen = 0;
static int g_page = 0;
static int g_perPage = 9;
static int g_sel = 0;        // 페이지 안 선택(하이라이트) 인덱스 (0-based)
static int g_winW = 220;     // 페이지 내용에 맞춘 창 너비

static CandidateSelectCallback g_onSelect = NULL;
static CandidateCancelCallback g_onCancel = NULL;
static void *g_ctx = NULL;

extern HINSTANCE g_hInst;

static HFONT g_candFont = NULL;   // 후보창 글꼴 캐시 (매 WM_PAINT 생성/파괴 낭비 제거)

static int  PageItemCount(void);      // 전방 선언 (WndProc 마우스 처리에서 사용)
static void SelectIndex(int realIdx);

static void EnsureCandFont(void) {
    if (!g_candFont)
        g_candFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
}

// i번째 후보의 표시 문자열. 단일 한자면 훈음(뜻·음) → 음만 → 코드포인트 순으로 폴백한다:
//   훈음 있음: "N. 家  집 가" / 훈음 없고 음만: "N. 特  특" / 둘 다 없음: "N. ★  U+2605".
static void FormatCandLine(int i, int numberInPage, wchar_t *buf, int cap) {
    const wchar_t *cand = g_candidates[i] ? g_candidates[i] : L"";
    if (cand[0] && !cand[1]) {   // 단일 문자 후보
        const wchar_t *hunum = HunumDict_Find(cand[0]);
        if (hunum) { swprintf(buf, cap, L"%d. %s  %s", numberInPage, cand, hunum); return; }
        wchar_t rd = HanjaDict_ReadingOf(cand[0]);   // 훈음 미수록 → 음(kHangul)이라도 표시
        if (rd)    swprintf(buf, cap, L"%d. %s  %c", numberInPage, cand, rd);
        else       swprintf(buf, cap, L"%d. %s  U+%04X", numberInPage, cand, (unsigned)cand[0]);
    } else {
        swprintf(buf, cap, L"%d. %s", numberInPage, cand);
    }
}

// 현재 페이지 내용에 맞는 창 너비 계산 (훈음 길이 반영)
static int MeasurePageWidth(void) {
    EnsureCandFont();
    int w = 200;
    HDC hdc = GetDC(NULL);
    if (hdc) {
        HFONT of = (HFONT)SelectObject(hdc, g_candFont);
        int start = g_page * g_perPage, end = start + g_perPage;
        if (end > g_count) end = g_count;
        for (int i = start; i < end; i++) {
            wchar_t buf[256]; SIZE sz;
            FormatCandLine(i, (i - start) + 1, buf, 256);
            if (GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &sz) && sz.cx + 24 > w) w = sz.cx + 24;
        }
        SelectObject(hdc, of);
        ReleaseDC(NULL, hdc);
    }
    return (w > 480) ? 480 : w;   // 폭 상한
}

static void DrawCandidateUI(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));

    EnsureCandFont();
    HFONT hOldFont = (HFONT)SelectObject(hdc, g_candFont);
    SetBkMode(hdc, TRANSPARENT);

    int start = g_page * g_perPage;
    int end = start + g_perPage;
    if (end > g_count) end = g_count;

    int y = 5;
    for (int i = start; i < end; i++) {
        wchar_t buf[256];
        FormatCandLine(i, (i - start) + 1, buf, 256);
        if (i - start == g_sel) {   // 선택 하이라이트 (↑↓로 이동, Enter로 확정)
            RECT hl = { 2, y - 2, rc.right - 2, y + 21 };
            HBRUSH hb = CreateSolidBrush(RGB(203, 224, 252));
            FillRect(hdc, &hl, hb);
            DeleteObject(hb);
        }
        SetTextColor(hdc, RGB(0, 0, 0));
        TextOutW(hdc, 10, y, buf, (int)wcslen(buf));
        y += 24;
    }

    // Page indicator
    wchar_t pageBuf[32];
    int totalPages = (g_count + g_perPage - 1) / g_perPage;
    swprintf(pageBuf, 32, L"[%d/%d]", g_page + 1, totalPages);
    SetTextColor(hdc, RGB(128, 128, 128));
    TextOutW(hdc, 10, y + 5, pageBuf, (int)wcslen(pageBuf));

    // 우상단 닫기(X) 버튼 — 키보드가 막혀도 마우스로 탈출 가능
    SetTextColor(hdc, RGB(160, 160, 160));
    TextOutW(hdc, rc.right - 16, 2, L"\x2715", 1);   // ✕

    SelectObject(hdc, hOldFont);
}

#define XBTN_SZ 16   // 우상단 닫기(X) 버튼 한 변

static LRESULT CALLBACK CandidateWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            DrawCandidateUI(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {   // 마우스: X=취소, 항목 줄=선택 (키보드 없이도 탈출/선택 가능)
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
            RECT rc; GetClientRect(hwnd, &rc);
            if (mx >= rc.right - XBTN_SZ - 4 && my <= XBTN_SZ + 4) {   // X 버튼
                if (g_onCancel) g_onCancel(g_ctx);
                CandidateUI_Hide();
                return 0;
            }
            int row = (my - 5) / 24;                                    // 항목 줄 클릭 → 선택
            if (row >= 0 && row < PageItemCount()) SelectIndex(g_page * g_perPage + row);
            return 0;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;   // 클릭해도 포커스 탈취 금지 (입력 앱 유지)
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

bool CandidateUI_Initialize(void) {
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = CandidateWndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"JamotongCandidateUI";
    RegisterClassW(&wc);
    return true;
}

void CandidateUI_Uninitialize(void) {
    CandidateUI_Hide();
    if (g_candFont) { DeleteObject(g_candFont); g_candFont = NULL; }
    UnregisterClassW(L"JamotongCandidateUI", g_hInst);
}

void CandidateUI_Show(int x, int y, wchar_t **candidates, int count, int replaceLen, CandidateSelectCallback onSelect, CandidateCancelCallback onCancel, void *ctx) {
    g_candidates = candidates;
    g_count = count;
    g_replaceLen = replaceLen;
    g_onSelect = onSelect;
    g_onCancel = onCancel;
    g_ctx = ctx;
    g_page = 0;
    g_sel = 0;
    g_winW = MeasurePageWidth();   // 훈음 길이에 맞춘 동적 너비

    int h = (g_perPage + 1) * 24 + 10;
    if (!g_hwndCandi) {
        g_hwndCandi = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"JamotongCandidateUI", L"", WS_POPUP | WS_VISIBLE | WS_BORDER,
            x, y, g_winW, h, NULL, NULL, g_hInst, NULL);
    } else {
        SetWindowPos(g_hwndCandi, HWND_TOPMOST, x, y, g_winW, h, SWP_SHOWWINDOW);
        InvalidateRect(g_hwndCandi, NULL, TRUE);
    }
}

// 페이지 이동/선택 변경 후 크기·내용 갱신
static void RefreshCandWindow(void) {
    if (!g_hwndCandi) return;
    int w = MeasurePageWidth();
    if (w != g_winW) {
        g_winW = w;
        SetWindowPos(g_hwndCandi, NULL, 0, 0, g_winW, (g_perPage + 1) * 24 + 10,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    InvalidateRect(g_hwndCandi, NULL, TRUE);
}

void CandidateUI_Hide(void) {
    if (g_hwndCandi) {
        DestroyWindow(g_hwndCandi);
        g_hwndCandi = NULL;
    }
    g_candidates = NULL;
    g_count = 0;
}

void CandidateUI_Cancel(void) {
    if (!g_hwndCandi) return;
    if (g_onCancel) g_onCancel(g_ctx);   // 컨텍스트 정리(pic Release)를 콜백이 수행
    CandidateUI_Hide();
}

bool CandidateUI_IsVisible(void) {
    return g_hwndCandi != NULL;
}

int CandidateUI_GetReplaceLen(void) {
    return g_replaceLen;
}

// 현재 페이지의 항목 수
static int PageItemCount(void) {
    int start = g_page * g_perPage;
    int n = g_count - start;
    return (n > g_perPage) ? g_perPage : (n < 0 ? 0 : n);
}

static void SelectIndex(int realIdx) {
    if (realIdx >= 0 && realIdx < g_count) {
        if (g_onSelect) g_onSelect(realIdx, g_candidates[realIdx], g_ctx);
        CandidateUI_Hide();
    }
}

bool CandidateUI_HandleKey(UINT vKey) {
    if (!g_hwndCandi) return false;

    if (vKey == VK_ESCAPE) {
        if (g_onCancel) g_onCancel(g_ctx);
        CandidateUI_Hide();
        return true;
    }

    if (vKey >= '1' && vKey <= '9') {              // 숫자 = 즉시 선택
        SelectIndex(g_page * g_perPage + (vKey - '1'));
        return true;
    }
    if (vKey == VK_RETURN) {                        // Enter = 하이라이트된 후보 선택
        SelectIndex(g_page * g_perPage + g_sel);
        return true;
    }

    if (vKey == VK_DOWN) {                          // ↑↓ = 페이지 안 선택 이동(끝에서 페이지 넘김)
        if (g_sel + 1 < PageItemCount()) { g_sel++; InvalidateRect(g_hwndCandi, NULL, TRUE); }
        else {
            int totalPages = (g_count + g_perPage - 1) / g_perPage;
            if (g_page < totalPages - 1) { g_page++; g_sel = 0; RefreshCandWindow(); }
        }
        return true;
    }
    if (vKey == VK_UP) {
        if (g_sel > 0) { g_sel--; InvalidateRect(g_hwndCandi, NULL, TRUE); }
        else if (g_page > 0) { g_page--; g_sel = PageItemCount() - 1; RefreshCandWindow(); }
        return true;
    }

    if (vKey == VK_RIGHT || vKey == VK_NEXT || vKey == VK_SPACE) {   // →/PgDn/Space = 다음 페이지
        int totalPages = (g_count + g_perPage - 1) / g_perPage;
        if (g_page < totalPages - 1) { g_page++; g_sel = 0; RefreshCandWindow(); }
        return true;
    }
    if (vKey == VK_LEFT || vKey == VK_PRIOR) {                        // ←/PgUp = 이전 페이지
        if (g_page > 0) { g_page--; g_sel = 0; RefreshCandWindow(); }
        return true;
    }

    // Ignore other keys while candidate UI is open
    return true;
}
