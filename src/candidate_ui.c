#include "candidate_ui.h"
#include "hanja_dict.h"   // HunumDict_Find — 후보 옆 훈음(뜻·음) 표시
#include <stdio.h>

#include "ui_element.h"   // RFC-0012 Phase 3: 창을 띄우기 전 UIElementMgr 게이트

static HWND g_hwndCandi = NULL;
static bool g_active = false;    // 후보 세션 활성 (자체 창 유무와 무관 — 호스트가 그릴 수도)
static bool g_ownDraw = true;    // 우리 창을 그려도 되는가 (BeginUIElement 의 답)
static bool g_hostShown = true;  // 호스트가 ITfUIElement::Show 로 지정한 표시 상태
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

// 배치 앵커(캐럿 기준 좌표) — 화면 클램프·페이지 리사이즈가 공유한다.
static int g_anchorX = 0, g_anchorY = 0, g_anchorTop = 0;

// 후보창 표시 중에만 설치하는 저수준 키보드 훅 — 일부 터미널(PuTTY)은 후보창이 뜬 상태에서
// 키를 TSF 키 싱크로 넘기지 않아 키보드 탐색이 죽는다(마우스만 동작; 실기 2026-07-24).
// 탐색 키를 여기서 직접 처리·차단하면 호스트의 키 라우팅과 무관하게 동작한다.
static HHOOK g_kbHook = NULL;
#define CANDMSG_HOOKKEY (WM_APP + 1)   // 훅 → 창으로 넘기는 탐색 키 (훅 콜백은 즉시 반환해야 함)

extern HINSTANCE g_hInst;

static HFONT g_candFont = NULL;   // 후보창 글꼴 캐시 (매 WM_PAINT 생성/파괴 낭비 제거)

// 후보창 스타일 — 설정(IME Options)에서 지정. 후보·훈음·페이지 표시·X버튼까지 이 글꼴/크기 하나.
static wchar_t g_face[32] = L"Malgun Gothic";
static int     g_fontPx   = 24;

// 전 요소가 공유하는 파생 메트릭: 행 높이/여백은 글꼴 크기에서만 나온다.
#define ROW_H   (g_fontPx + 8)
#define PAD_TOP 6

static int  PageItemCount(void);      // 전방 선언 (WndProc 마우스 처리에서 사용)
static void SelectIndex(int realIdx);

void CandidateUI_SetStyle(const wchar_t *face, int sizePx) {
    if (sizePx < 12) sizePx = 12;
    if (sizePx > 72) sizePx = 72;
    if (face && face[0] && (wcscmp(face, g_face) != 0 || sizePx != g_fontPx)) {
        wcsncpy(g_face, face, 31); g_face[31] = L'\0';
        g_fontPx = sizePx;
        if (g_candFont) { DeleteObject(g_candFont); g_candFont = NULL; }   // 캐시 무효화
    } else if ((!face || !face[0]) && sizePx != g_fontPx) {
        g_fontPx = sizePx;
        if (g_candFont) { DeleteObject(g_candFont); g_candFont = NULL; }
    }
}

static void EnsureCandFont(void) {
    if (!g_candFont)
        g_candFont = CreateFontW(g_fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, g_face);
}

// i번째 후보의 표시 문자열. 단일 문자 후보는 훈음(뜻·음) → 음 순으로 표기하고 항상 코드포인트를
// 병기한다(사용자 요청 2026-07-24): "N. 家  집 가  U+5BB6" / "N. 特  특  U+7279" /
// 훈음·음 모두 없음: "N. ★  U+2605". 여러 글자(한자 단어) 후보에는 코드포인트를 붙이지 않는다.
// BMP 밖 단일 글자(서로게이트 쌍)도 단일 문자로 취급해 U+XXXXX를 표기한다.
static void FormatCandLine(int i, int numberInPage, wchar_t *buf, int cap) {
    const wchar_t *cand = g_candidates[i] ? g_candidates[i] : L"";
    unsigned cp = 0;
    if (cand[0] && !cand[1]) cp = (unsigned)cand[0];   // BMP 단일 문자
    else if (cand[0] >= 0xD800 && cand[0] <= 0xDBFF && cand[1] >= 0xDC00 && cand[1] <= 0xDFFF && !cand[2])
        cp = 0x10000u + (((unsigned)cand[0] - 0xD800u) << 10) + ((unsigned)cand[1] - 0xDC00u);
    if (cp) {
        if (cp <= 0xFFFF) {
            const wchar_t *hunum = HunumDict_Find((wchar_t)cp);
            if (hunum) { swprintf(buf, cap, L"%d. %s  %s  U+%04X", numberInPage, cand, hunum, cp); return; }
            wchar_t rd = HanjaDict_ReadingOf((wchar_t)cp);   // 훈음 미수록 → 음(kHangul)이라도 표시
            if (rd) { swprintf(buf, cap, L"%d. %s  %c  U+%04X", numberInPage, cand, rd, cp); return; }
        }
        swprintf(buf, cap, L"%d. %s  U+%04X", numberInPage, cand, cp);
    } else {
        swprintf(buf, cap, L"%d. %s", numberInPage, cand);   // 한자 단어: 코드포인트 없음
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
    int cap480 = (g_fontPx * 30 > 480) ? g_fontPx * 30 : 480;   // 폭 상한(글꼴 크기 비례)
    return (w > cap480) ? cap480 : w;
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

    int y = PAD_TOP;
    for (int i = start; i < end; i++) {
        wchar_t buf[256];
        FormatCandLine(i, (i - start) + 1, buf, 256);
        if (i - start == g_sel) {   // 선택 하이라이트 (↑↓로 이동, Enter로 확정)
            RECT hl = { 2, y - 2, rc.right - 2, y + ROW_H - 3 };
            HBRUSH hb = CreateSolidBrush(RGB(203, 224, 252));
            FillRect(hdc, &hl, hb);
            DeleteObject(hb);
        }
        SetTextColor(hdc, RGB(0, 0, 0));
        TextOutW(hdc, 10, y, buf, (int)wcslen(buf));
        y += ROW_H;
    }

    // 페이지 표시 — 후보와 같은 글꼴·크기(색만 회색). "현재/전체" 형식.
    wchar_t pageBuf[32];
    int totalPages = (g_count + g_perPage - 1) / g_perPage;
    swprintf(pageBuf, 32, L"[%d/%d]", g_page + 1, totalPages);
    SetTextColor(hdc, RGB(128, 128, 128));
    TextOutW(hdc, 10, y + 3, pageBuf, (int)wcslen(pageBuf));

    // 우상단 닫기(X) 버튼 — 키보드가 막혀도 마우스로 탈출 가능
    SetTextColor(hdc, RGB(160, 160, 160));
    TextOutW(hdc, rc.right - 16, 2, L"\x2715", 1);   // ✕

    SelectObject(hdc, hOldFont);
}

// 화면(모니터 작업영역) 안으로 클램프해 배치한다. 아래로 넘치면 캐럿 '위'로 뒤집고,
// 그래도 안 되면 작업영역 하단에 맞춘다(가장자리에서 후보창이 잘리던 실기 2026-07-24).
static void PlaceCandWindow(void) {
    if (!g_hwndCandi) return;
    int h = (g_perPage + 1) * ROW_H + PAD_TOP * 2 + 4;
    int x = g_anchorX, y = g_anchorY;
    POINT pt = { x, y };
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi; mi.cbSize = sizeof(mi);
    if (mon && GetMonitorInfoW(mon, &mi)) {
        if (x + g_winW > mi.rcWork.right) x = mi.rcWork.right - g_winW;
        if (x < mi.rcWork.left)           x = mi.rcWork.left;
        if (y + h > mi.rcWork.bottom) {
            int above = g_anchorTop - h - 4;   // 캐럿 줄 위로 뒤집기 (조합 줄을 덮지 않음)
            y = (above >= mi.rcWork.top) ? above : mi.rcWork.bottom - h;
        }
        if (y < mi.rcWork.top) y = mi.rcWork.top;
    }
    SetWindowPos(g_hwndCandi, HWND_TOPMOST, x, y, g_winW, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// 저수준 키보드 훅: 탐색 키만 차단하고 CANDMSG_HOOKKEY로 창에 넘긴다(훅 콜백은 오래 걸리면
// OS가 떼어내므로 실제 처리는 창 프로시저에서). 합성 입력(LLKHF_INJECTED — 코드 자판
// SendInput 포함)은 건드리지 않는다. 탐색 키 외(문자 등)는 통과 → 기존 TSF 싱크 경로가
// 처리한다(정상 호스트에서는 훅이 먼저 먹으므로 이중 처리 없음 — 집합이 서로소).
static LRESULT CALLBACK CandKbHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_hwndCandi) {
        const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT*)lParam;
        if (!(k->flags & LLKHF_INJECTED)) {
            UINT vk = (UINT)k->vkCode;
            bool nav = (vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_UP || vk == VK_DOWN ||
                        vk == VK_LEFT || vk == VK_RIGHT || vk == VK_PRIOR || vk == VK_NEXT ||
                        vk == VK_SPACE || (vk >= '1' && vk <= '9'));
            if (nav) {
                if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
                    PostMessageW(g_hwndCandi, CANDMSG_HOOKKEY, (WPARAM)vk, 0);
                return 1;   // keydown/keyup 모두 차단 — 앱(터미널 너머 원격 포함)에 새지 않게
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static void InstallKbHook(void) {
    if (!g_kbHook)
        g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, CandKbHookProc, g_hInst, 0);
}
static void RemoveKbHook(void) {
    if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = NULL; }
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
            int row = (my - PAD_TOP) / ROW_H;                           // 항목 줄 클릭 → 선택
            if (row >= 0 && row < PageItemCount()) SelectIndex(g_page * g_perPage + row);
            return 0;
        }
        case CANDMSG_HOOKKEY:   // 저수준 훅이 차단·전달한 탐색 키 (PuTTY류 키 라우팅 폴백)
            CandidateUI_HandleKey((UINT)wParam);
            return 0;
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

void CandidateUI_Show(int x, int y, int caretTop, wchar_t **candidates, int count, int replaceLen, CandidateSelectCallback onSelect, CandidateCancelCallback onCancel, void *ctx) {
    g_candidates = candidates;
    g_count = count;
    g_replaceLen = replaceLen;
    g_onSelect = onSelect;
    g_onCancel = onCancel;
    g_ctx = ctx;
    g_page = 0;
    g_sel = 0;
    g_winW = MeasurePageWidth();   // 훈음 길이에 맞춘 동적 너비
    g_anchorX = x; g_anchorY = y;
    g_anchorTop = (caretTop < y) ? caretTop : y;   // 뒤집기 기준(캐럿 줄 위) — 방어적 정규화

    g_active = true;
    g_hostShown = true;
    g_ownDraw = UiElem_BeginCandidate() ? true : false;   // 호스트가 그린다면 우리 창·훅 생략
    if (g_ownDraw) {
        int h = (g_perPage + 1) * ROW_H + PAD_TOP * 2 + 4;
        if (!g_hwndCandi) {
            g_hwndCandi = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                L"JamotongCandidateUI", L"", WS_POPUP | WS_BORDER,
                x, y, g_winW, h, NULL, NULL, g_hInst, NULL);
        }
        PlaceCandWindow();   // 모니터 작업영역 클램프(+SHOWWINDOW)
        InvalidateRect(g_hwndCandi, NULL, TRUE);
        InstallKbHook();     // PuTTY류 키 라우팅 폴백 — 자체 창 표시 중에만
    }
    UiElem_UpdateCandidate(0x3F);   // 첫 갱신 = 전체 비트 (uiless 문서: 첫 Update 는 all-bits)
}

// 페이지 이동/선택 변경 후 크기·내용 갱신
static void RefreshCandWindow(void) {
    UiElem_UpdateCandidate(0x04|0x10|0x20);   // SELECTION|PAGEINDEX|CURRENTPAGE
    if (!g_hwndCandi) return;
    g_winW = MeasurePageWidth();
    PlaceCandWindow();   // 폭 변화·화면 클램프 반영 (앵커 기준 재배치)
    InvalidateRect(g_hwndCandi, NULL, TRUE);
}

void CandidateUI_Hide(void) {
    UiElem_EndCandidate();   // 게이트 종료 (began 아니면 no-op) — EndUIElement 는 의무
    RemoveKbHook();   // 표시 중에만 유지되는 키 라우팅 폴백 해제
    if (g_hwndCandi) {
        DestroyWindow(g_hwndCandi);
        g_hwndCandi = NULL;
    }
    g_candidates = NULL;
    g_count = 0;
    g_active = false;
    g_ownDraw = true;
}

void CandidateUI_Cancel(void) {
    if (!g_active) return;
    if (g_onCancel) g_onCancel(g_ctx);   // 컨텍스트 정리(pic Release)를 콜백이 수행
    CandidateUI_Hide();
}

bool CandidateUI_IsVisible(void) {
    return g_active;   // 자체 창이 없어도(호스트가 그림) 키 라우팅은 우리가 계속 한다
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
        if (g_sel + 1 < PageItemCount()) { g_sel++; UiElem_UpdateCandidate(0x04); InvalidateRect(g_hwndCandi, NULL, TRUE); }
        else {
            int totalPages = (g_count + g_perPage - 1) / g_perPage;
            if (g_page < totalPages - 1) { g_page++; g_sel = 0; RefreshCandWindow(); }
        }
        return true;
    }
    if (vKey == VK_UP) {
        if (g_sel > 0) { g_sel--; UiElem_UpdateCandidate(0x04); InvalidateRect(g_hwndCandi, NULL, TRUE); }
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

// ── UI element 요소 접근자 (ui_element.c 의 ITfCandidateListUIElement 가 읽는다) ──────
int  CandidateUI_ElemCount(void)      { return g_count; }
int  CandidateUI_ElemSelection(void)  { return g_active ? g_page * g_perPage + g_sel : -1; }
const wchar_t *CandidateUI_ElemString(int idx) {
    return (g_candidates && idx >= 0 && idx < g_count) ? g_candidates[idx] : NULL;
}
int  CandidateUI_ElemPerPage(void)    { return g_perPage; }
int  CandidateUI_ElemPage(void)       { return g_page; }
void CandidateUI_ElemHostShow(BOOL show) {
    g_hostShown = show ? true : false;
    if (g_hwndCandi) ShowWindow(g_hwndCandi, show ? SW_SHOWNOACTIVATE : SW_HIDE);
}
BOOL CandidateUI_ElemIsShown(void)    { return (g_hwndCandi != NULL) && g_hostShown; }
