#include "candidate_ui.h"
#include "popup_style.h"
#include "cand_uia.h"
#include "jamo_class.h"   // RFC-0008 W2-05
#include "hanja_dict.h"   // HunumDict_Find — 후보 옆 훈음(뜻·음) 표시
#include <stdio.h>

#include "ui_element.h"   // RFC-0012 Phase 3: 창을 띄우기 전 UIElementMgr 게이트
#include "edit_session.h"   // JamoDiag (JAMO_DIAG 빌드에서만 기록)


// ── RFC-0008 W0-03 S2: 이 UI 창의 소유 스레드 ──────────────────────────────────────
// 창은 프로세스당 하나가 맞다(한 번에 하나만 보인다 — 스레드마다 만들면 그게 회귀다).
// 다만 **만든 스레드만** 만져야 한다: 다른 입력 스레드가 같은 HWND 를 조작하면 cross-thread
// 창 조작이 되고, 그 스레드가 죽은 뒤엔 해제된 창을 만지게 된다.
// 실기(2026-09-21, 메모장·UWP 검색·헬퍼 경로)에서 교차 호출은 0 이었다. 그건 '차단해도 안전하다'가
// 아니라 '아직 못 봤다'는 뜻이다. 그래서 두 갈래로 나눈다 (B5, 2026-09-24):
//   - **키 처리**는 막는다. 남의 스레드가 우리 창의 키를 먹는 것은 어차피 옳지 않고, 안 먹으면
//     그 키는 응용으로 갈 뿐이라 잃는 것이 없다.
//   - **보이기·숨기기·그리기**는 막지 않는다. 여기서 막으면 우리가 못 본 정상 경로에서 창이 안
//     뜨거나 뜬 채 남는 새 회귀가 된다.
// 어느 쪽이든 **횟수는 배포판에서도 센다**(UiGuard_CrossThread) — 진단 빌드는 배포하지 않으므로,
// 세지 않으면 '로그에 찍히면 올린다'는 계획이 영원히 결론에 못 이른다.
static DWORD g_ownerTid = 0;
static HWND g_viewWnd = NULL;   // ITfContextView::GetWnd — 포커스 창이 없을 때의 소유자 후보
void CandidateUI_SetViewWindow(HWND hwnd) { g_viewWnd = hwnd; }

static void OwnerThreadClaim(void) { g_ownerTid = GetCurrentThreadId(); }

static bool OwnerThreadGuard(const char *what) {
    DWORD me = GetCurrentThreadId();
    if (g_ownerTid && g_ownerTid != me) {
        UiGuard_CrossThread(what, (unsigned long)g_ownerTid, (unsigned long)me);
        return false;
    }
    return true;
}

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
static void UpdateUiaName(void);   // 화면 읽기 도구에 넘길 이름 (B10)
static int g_winW = 220;     // 페이지 내용에 맞춘 창 너비

static CandidateSelectCallback g_onSelect = NULL;
static CandidateCancelCallback g_onCancel = NULL;
static CandidateKeyCallback g_onKey = NULL;   // 병음 방식이면 있다 (CandidateUI_SetPinyinKeys)
static CandidateKeyCallback g_onSegKey = NULL;   // 일본어 문절 편집이면 있다 (CandidateUI_SetSegmentKeys)
static int g_hookShift = -1;                   // 훅이 넘긴 글쇠를 처리하는 동안: 그때의 Shift (0/1), 아니면 -1
static bool g_digitsToInput = false;          // V 모드: 숫자·- = 는 입력기로
static wchar_t **g_notes = NULL;              // 후보 옆의 작은 글 (성조 병음)
// RFC-0020 P2 (0.69.0): 머리줄·가로 후보줄·마우스 올림. 창을 닫으면 풀린다.
static wchar_t g_title[64];                   // 바꾸고 있는 것 (병음 읽기·한자로 바꿀 한글)
static bool g_horizontal = false;             // 가로 후보줄 (중국어, 자판 선택)
static int g_hover = -1;                      // 마우스를 올린 줄 (페이지 안 자리)
static bool g_tracking = false;               // WM_MOUSELEAVE 를 기다리는 중
static RECT g_rcPrev, g_rcNext, g_rcClose;    // 쪽 단추·닫기 단추 (클라이언트 좌표)
static RECT g_cell[9];                        // 가로 후보줄의 칸 (클라이언트 좌표)
static int g_rowsTop = 0;                     // 세로 줄들이 시작하는 y
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
static int     g_fontPx   = 24;   // 100%(96 DPI) 기준 — 실제 픽셀은 창 DPI 로 배율 (W2-03)
static UINT    g_dpi      = 96;   // 마지막으로 그린 창의 DPI (DPI 를 모르는 호스트는 늘 96)

// 전 요소가 공유하는 파생 메트릭: 행 높이/여백은 글꼴 크기에서만 나온다(모두 DPI 배율 — W2-03).
#define FONT_PX Popup_Scale(g_fontPx, g_dpi)
#define ROW_H   (FONT_PX + Popup_Scale(10, g_dpi))
#define PAD_TOP Popup_Scale(6, g_dpi)
#define TEXT_X  Popup_Scale(12, g_dpi)
#define XBTN_SZ Popup_Scale(16, g_dpi)   // 우상단 닫기(X) 버튼 한 변
#define NOTE_PX FONT_PX   // 머리줄·주석·쪽 표시도 같은 글꼴·같은 크기 — 흐린 색으로만 나눈다 (사용자 요청 2026-07-24, T014)
#define HDR_H   (NOTE_PX + Popup_Scale(12, g_dpi))   // 머리줄 (바꾸는 것 + 닫기)
#define FOOT_H  (NOTE_PX + Popup_Scale(12, g_dpi))   // 쪽 표시 ◀ 1/8 ▶
#define GAP     Popup_Scale(14, g_dpi)              // 칸 사이

static int  PageItemCount(void);      // 전방 선언 (WndProc 마우스 처리에서 사용)
static void RefreshCandWindow(void);  // 전방 선언 (쪽 단추·휠)
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
        g_candFont = CreateFontW(FONT_PX, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, g_face);
}

// ── 모양 (RFC-0020 P2): 시스템 다크/라이트를 따르고, 고대비면 시스템 색 ─────────────────────────
typedef struct { COLORREF bg, text, dim, accent, selBg, selText, hoverBg, border; } CandColors;
static bool SystemDark(void) {
    DWORD v = 1, sz = sizeof v; HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        RegQueryValueExW(hk, L"AppsUseLightTheme", NULL, NULL, (BYTE *)&v, &sz);
        RegCloseKey(hk);
    }
    return v == 0;
}
static void PickCandColors(CandColors *c) {
    if (Popup_HighContrast()) {   // 고대비: 시스템 색 그대로 (W2-03)
        const PopupColors normal = { 0 };
        PopupColors pc; Popup_PickColors(true, &normal, &pc);
        c->bg = pc.bg; c->text = pc.text; c->dim = pc.dim; c->accent = pc.accent; c->selBg = pc.selBg; c->selText = pc.selText;
        c->hoverBg = pc.bg; c->border = pc.text;
        return;
    }
    if (SystemDark()) {
        c->bg = RGB(43, 43, 43); c->text = RGB(240, 240, 240); c->dim = RGB(160, 160, 160); c->accent = RGB(76, 194, 255);
        c->selBg = RGB(52, 72, 92); c->selText = RGB(255, 255, 255); c->hoverBg = RGB(58, 58, 58); c->border = RGB(78, 78, 78);
    } else {
        c->bg = RGB(250, 250, 250); c->text = RGB(26, 26, 26); c->dim = RGB(112, 112, 112); c->accent = RGB(0, 95, 184);
        c->selBg = RGB(220, 234, 250); c->selText = RGB(0, 0, 0); c->hoverBg = RGB(238, 238, 238); c->border = RGB(214, 214, 214);
    }
}
// Windows 11 둥근 모서리·테두리 색 (dwmapi 를 동적으로 — 없으면 네모 그대로). 그림자는 창 클래스의 CS_DROPSHADOW.
static bool g_roundedOk = false;
static void ApplyRoundedCorners(HWND hwnd, COLORREF border) {
    typedef HRESULT (WINAPI *DwmSetAttr)(HWND, DWORD, LPCVOID, DWORD);
    static DwmSetAttr fn = NULL; static bool tried = false;
    if (!tried) { tried = true; HMODULE m = LoadLibraryW(L"dwmapi.dll"); if (m) fn = (DwmSetAttr)(void *)GetProcAddress(m, "DwmSetWindowAttribute"); }
    g_roundedOk = false;
    if (!fn || !hwnd) return;
    DWORD pref = 3;   /* DWMWCP_ROUNDSMALL */
    if (SUCCEEDED(fn(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &pref, sizeof pref))) {
        g_roundedOk = true;
        fn(hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof border);
    }
}

// 후보 옆의 주석 (RFC-0020 P2 — 후보와 나눠 흐린 작은 글로 맞춘 칸에). 없으면 빈 문자열.
//   병음 후보창: 성조 병음(있으면). 한자 후보창: 단일 문자는 훈음(뜻·음) → 음 순, 그리고 늘 부호값(사용자 요청 2026-07-24)
//   "집 가  U+5BB6" / "특  U+7279" / "U+2605". 여러 글자(한자 단어)에는 코드포인트를 붙이지 않는다. BMP 밖 단일 글자도 U+XXXXX.
static void CandNote(int i, wchar_t *buf, int cap) {
    buf[0] = L'\0';
    const wchar_t *cand = g_candidates[i] ? g_candidates[i] : L"";
    if (g_onKey || g_notes) {   // 순차 입력의 후보창(중국어·일본어): 준 주석만 — 부호값은 한자 후보창의 것이다
        if (g_notes && g_notes[i] && g_notes[i][0]) lstrcpynW(buf, g_notes[i], cap);
        return;
    }
    unsigned cp = 0;
    if (cand[0] && !cand[1]) cp = (unsigned)cand[0];
    else if (cand[0] >= 0xD800 && cand[0] <= 0xDBFF && cand[1] >= 0xDC00 && cand[1] <= 0xDFFF && !cand[2])
        cp = 0x10000u + (((unsigned)cand[0] - 0xD800u) << 10) + ((unsigned)cand[1] - 0xDC00u);
    if (!cp) return;
    if (cp <= 0xFFFF) {
        const wchar_t *hunum = HunumDict_Find((wchar_t)cp);
        if (hunum) { swprintf(buf, cap, L"%s  U+%04X", hunum, cp); return; }
        wchar_t rd = HanjaDict_ReadingOf((wchar_t)cp);
        if (rd) { swprintf(buf, cap, L"%c  U+%04X", rd, cp); return; }
    }
    swprintf(buf, cap, L"U+%04X", cp);
}
static int TextW(HDC hdc, const wchar_t *t) { SIZE sz = {0}; GetTextExtentPoint32W(hdc, t, (int)wcslen(t), &sz); return sz.cx; }

// 한 쪽의 칸 너비들: 번호·후보·주석 (세로) — 가로 후보줄은 칸마다
typedef struct { int numW, candW, noteW; } PageMetrics;
static PageMetrics MeasurePage(HDC hdc) {
    PageMetrics m = { 0, 0, 0 };
    int start = g_page * g_perPage, end = start + g_perPage;
    if (end > g_count) end = g_count;
    HFONT of = (HFONT)SelectObject(hdc, g_candFont);
    m.numW = TextW(hdc, L"9");
    for (int i = start; i < end; i++) { int w = TextW(hdc, g_candidates[i] ? g_candidates[i] : L""); if (w > m.candW) m.candW = w; }
    SelectObject(hdc, g_candFont);
    for (int i = start; i < end; i++) { wchar_t n[96]; CandNote(i, n, 96); int w = n[0] ? TextW(hdc, n) : 0; if (w > m.noteW) m.noteW = w; }
    SelectObject(hdc, of);
    return m;
}

// 현재 페이지 내용에 맞는 창 너비
static int MeasurePageWidth(void) {
    EnsureCandFont();
    int w = Popup_Scale(180, g_dpi);
    HDC hdc = GetDC(NULL);
    if (hdc) {
        if (g_horizontal) {   // 가로 후보줄: 머리줄(읽기 + 쪽 표시 + 닫기)과 칸들 중 넓은 쪽
            int start = g_page * g_perPage, end = start + g_perPage;
            if (end > g_count) end = g_count;
            HFONT of = (HFONT)SelectObject(hdc, g_candFont);
            int row = Popup_Scale(4, g_dpi);   // 그리는 것과 같은 셈 (DrawCandidateUI 의 칸): 여백 8 + 번호 + 4 + 후보 + 8, 칸 사이 2
            for (int i = start; i < end; i++) {
                wchar_t num[4]; swprintf(num, 4, L"%d", i - start + 1);
                row += Popup_Scale(8, g_dpi) + TextW(hdc, num) + Popup_Scale(4, g_dpi) + TextW(hdc, g_candidates[i] ? g_candidates[i] : L"")
                     + Popup_Scale(8, g_dpi) + Popup_Scale(2, g_dpi);
            }
            row += Popup_Scale(4, g_dpi);
            SelectObject(hdc, g_candFont);
            int head = TEXT_X + TextW(hdc, g_title) + GAP + TextW(hdc, L"\x25C0 99/99 \x25B6") + GAP + XBTN_SZ + Popup_Scale(8, g_dpi);
            SelectObject(hdc, of);
            w = row > head ? row : head;
        } else {
            PageMetrics m = MeasurePage(hdc);
            int body = TEXT_X + m.numW + Popup_Scale(10, g_dpi) + m.candW + (m.noteW ? GAP + m.noteW : 0) + TEXT_X;
            HFONT of = (HFONT)SelectObject(hdc, g_candFont);
            int head = TEXT_X + TextW(hdc, g_title) + GAP + XBTN_SZ + Popup_Scale(8, g_dpi);
            SelectObject(hdc, of);
            if (body > w) w = body;
            if (head > w) w = head;
        }
        ReleaseDC(NULL, hdc);
    }
    int cap = g_horizontal ? Popup_Scale(1200, g_dpi)
                           : ((FONT_PX * 30 > Popup_Scale(480, g_dpi)) ? FONT_PX * 30 : Popup_Scale(480, g_dpi));   // 폭 상한(글꼴 크기 비례)
    return (w > cap) ? cap : w;
}
static int CandWindowHeight(void) {
    if (g_horizontal) return HDR_H + ROW_H + PAD_TOP;
    return HDR_H + g_perPage * ROW_H + FOOT_H;
}

static void FillRoundRect(HDC hdc, const RECT *r, COLORREF c, int radius) {
    HBRUSH b = CreateSolidBrush(c);
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ ob = SelectObject(hdc, b), op = SelectObject(hdc, p);
    RoundRect(hdc, r->left, r->top, r->right, r->bottom, radius, radius);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(b); DeleteObject(p);
}

static void DrawCandidateUI(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    CandColors col; PickCandColors(&col);
    HBRUSH bgb = CreateSolidBrush(col.bg);
    FillRect(hdc, &rc, bgb);
    DeleteObject(bgb);
    if (!g_roundedOk) { HBRUSH fb = CreateSolidBrush(col.border); FrameRect(hdc, &rc, fb); DeleteObject(fb); }   // 둥근 모서리가 없으면 테두리를 그린다

    EnsureCandFont();
    HFONT hOldFont = (HFONT)SelectObject(hdc, g_candFont);
    SetBkMode(hdc, TRANSPARENT);
    int start = g_page * g_perPage;
    int end = start + g_perPage;
    if (end > g_count) end = g_count;
    int totalPages = (g_count + g_perPage - 1) / g_perPage;
    const int radius = Popup_Scale(8, g_dpi);

    // 머리줄: 바꾸는 것(흐림) · (가로면 쪽 표시) · 닫기
    SetTextColor(hdc, col.dim);
    RECT ht = { TEXT_X, 0, rc.right - XBTN_SZ - Popup_Scale(10, g_dpi), HDR_H };
    DrawTextW(hdc, g_title, -1, &ht, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SetRect(&g_rcClose, rc.right - XBTN_SZ - Popup_Scale(8, g_dpi), 0, rc.right, HDR_H);
    DrawTextW(hdc, L"\x2715", 1, &g_rcClose, DT_SINGLELINE | DT_VCENTER | DT_CENTER);   // ✕

    wchar_t pageBuf[32];
    swprintf(pageBuf, 32, L"%d/%d", g_page + 1, totalPages);
    SIZE ps = {0}; GetTextExtentPoint32W(hdc, pageBuf, (int)wcslen(pageBuf), &ps);
    int arrowW = TextW(hdc, L"\x25C0") + Popup_Scale(10, g_dpi);
    // 쪽 표시 자리: 세로 = 아래 줄 가운데, 가로 = 머리줄 오른쪽(닫기 왼쪽)
    int py0, py1, pcx;
    if (g_horizontal) { py0 = 0; py1 = HDR_H; pcx = g_rcClose.left - Popup_Scale(8, g_dpi) - arrowW - ps.cx / 2; }
    else { py0 = rc.bottom - FOOT_H; py1 = rc.bottom; pcx = rc.right / 2; }
    SetRect(&g_rcPrev, pcx - ps.cx / 2 - arrowW, py0, pcx - ps.cx / 2, py1);
    SetRect(&g_rcNext, pcx + ps.cx / 2, py0, pcx + ps.cx / 2 + arrowW, py1);
    RECT pr = { pcx - ps.cx / 2, py0, pcx + ps.cx / 2 + 1, py1 };
    SetTextColor(hdc, col.dim);
    DrawTextW(hdc, pageBuf, -1, &pr, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    SetTextColor(hdc, g_page > 0 ? col.accent : col.border);
    DrawTextW(hdc, L"\x25C0", 1, &g_rcPrev, DT_SINGLELINE | DT_VCENTER | DT_CENTER);   // ◀
    SetTextColor(hdc, g_page < totalPages - 1 ? col.accent : col.border);
    DrawTextW(hdc, L"\x25B6", 1, &g_rcNext, DT_SINGLELINE | DT_VCENTER | DT_CENTER);   // ▶

    if (g_horizontal) {   // 가로 후보줄: 한 줄에 아홉
        int x = Popup_Scale(4, g_dpi), y = HDR_H;
        for (int i = start; i < end; i++) {
            int k = i - start;
            wchar_t num[4]; swprintf(num, 4, L"%d", k + 1);
            SelectObject(hdc, g_candFont);
            int nw = TextW(hdc, num), cw = TextW(hdc, g_candidates[i] ? g_candidates[i] : L"");
            SetRect(&g_cell[k], x, y, x + Popup_Scale(8, g_dpi) + nw + Popup_Scale(4, g_dpi) + cw + Popup_Scale(8, g_dpi), y + ROW_H);
            if (k == g_sel) FillRoundRect(hdc, &g_cell[k], col.selBg, radius);
            else if (k == g_hover) FillRoundRect(hdc, &g_cell[k], col.hoverBg, radius);
            int tx = g_cell[k].left + Popup_Scale(8, g_dpi), ty = y + (ROW_H - FONT_PX) / 2 - 1;
            SetTextColor(hdc, k == g_sel ? col.accent : col.dim);
            TextOutW(hdc, tx, ty, num, (int)wcslen(num));
            SetTextColor(hdc, k == g_sel ? col.selText : col.text);
            TextOutW(hdc, tx + nw + Popup_Scale(4, g_dpi), ty, g_candidates[i] ? g_candidates[i] : L"", (int)wcslen(g_candidates[i] ? g_candidates[i] : L""));
            x = g_cell[k].right + Popup_Scale(2, g_dpi);
        }
    } else {   // 세로: 번호 · 후보 · 주석 (맞춘 칸)
        PageMetrics m = MeasurePage(hdc);
        int xNum = TEXT_X, xCand = xNum + m.numW + Popup_Scale(10, g_dpi), xNote = xCand + m.candW + GAP;
        g_rowsTop = HDR_H;
        int y = g_rowsTop;
        for (int i = start; i < end; i++) {
            int k = i - start;
            RECT row = { Popup_Scale(4, g_dpi), y + 1, rc.right - Popup_Scale(4, g_dpi), y + ROW_H - 1 };
            if (k == g_sel) {
                FillRoundRect(hdc, &row, col.selBg, radius);
                RECT bar = { row.left + Popup_Scale(1, g_dpi), row.top + ROW_H / 4, row.left + Popup_Scale(4, g_dpi), row.bottom - ROW_H / 4 };
                FillRoundRect(hdc, &bar, col.accent, Popup_Scale(3, g_dpi));   // 강조색 막대
            } else if (k == g_hover) FillRoundRect(hdc, &row, col.hoverBg, radius);
            wchar_t num[4]; swprintf(num, 4, L"%d", k + 1);
            SelectObject(hdc, g_candFont);
            int ty = y + (ROW_H - FONT_PX) / 2 - 1;
            SetTextColor(hdc, k == g_sel ? col.accent : col.dim);
            TextOutW(hdc, xNum, ty, num, (int)wcslen(num));
            const wchar_t *cand = g_candidates[i] ? g_candidates[i] : L"";
            SetTextColor(hdc, k == g_sel ? col.selText : col.text);
            TextOutW(hdc, xCand, ty, cand, (int)wcslen(cand));
            wchar_t note[96]; CandNote(i, note, 96);
            if (note[0]) {
                SelectObject(hdc, g_candFont);
                SetTextColor(hdc, col.dim);
                TextOutW(hdc, xNote, y + (ROW_H - NOTE_PX) / 2, note, (int)wcslen(note));
            }
            y += ROW_H;
        }
    }
    SelectObject(hdc, hOldFont);
}

// 화면(모니터 작업영역) 안으로 클램프해 배치한다. 아래로 넘치면 캐럿 '위'로 뒤집고,
// 그래도 안 되면 작업영역 하단에 맞춘다(가장자리에서 후보창이 잘리던 실기 2026-07-24).
static void PlaceCandWindow(void) {
    if (!g_hwndCandi) return;
    int h = CandWindowHeight();
    int x = g_anchorX, y = g_anchorY;
    Popup_ClampToMonitor(g_anchorTop, g_winW, h, &x, &y);   // 작업영역 클램프 (세 팝업 공통, W2-03)
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
            bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            bool nav = (vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_UP || vk == VK_DOWN ||
                        vk == VK_LEFT || vk == VK_RIGHT || vk == VK_PRIOR || vk == VK_NEXT ||
                        vk == VK_SPACE || (vk >= '1' && vk <= '9'));
            if (g_onKey) {   // 병음 방식: - = [ ] 도 우리 것, Shift 를 누른 숫자·기호는 문장부호라 입력기로
                if (!shift && (vk == VK_OEM_MINUS || vk == VK_OEM_PLUS || vk == VK_OEM_4 || vk == VK_OEM_6)) nav = true;
                if (shift && (vk >= '1' && vk <= '9')) nav = false;
                if (g_digitsToInput && ((vk >= '0' && vk <= '9') || vk == VK_OEM_MINUS || vk == VK_OEM_PLUS)) nav = false;
            }
            if (g_onSegKey && shift && (vk >= '1' && vk <= '9')) nav = false;   // 문절 편집: Shift+숫자는 문장부호 — 입력기로
            if (nav) {
                if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
                    PostMessageW(g_hwndCandi, CANDMSG_HOOKKEY, (WPARAM)vk, shift ? 1 : 0);   // Shift 는 지금의 것 — 받을 때는 이미 뗐을 수 있다
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


// 마우스가 가리키는 후보 (페이지 안 자리), 없으면 -1
static int HitItem(POINT pt) {
    int n = PageItemCount();
    if (g_horizontal) {
        for (int k = 0; k < n && k < 9; k++) if (PtInRect(&g_cell[k], pt)) return k;
        return -1;
    }
    if (pt.y < g_rowsTop) return -1;
    int k = (pt.y - g_rowsTop) / ROW_H;
    return (k >= 0 && k < n) ? k : -1;
}

static bool g_inHide = false;   // 우리 Hide 가 부수는 중인가 (소유자 파괴로 끌려가는 경우와 구별)

static LRESULT CALLBACK CandidateWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_NCDESTROY:
            // RFC-0008 W1-08: 소유자(대상 최상위 창)가 먼저 파괴되면 우리 창도 끌려간다. 핸들을 비우고,
            // 열려 있던 후보는 취소로 닫아 콜백이 문맥 참조를 정리하게 한다.
            if (hwnd == g_hwndCandi) {
                g_hwndCandi = NULL;
                if (!g_inHide && g_active) CandidateUI_Cancel();
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            DrawCandidateUI(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {   // 마우스: 닫기=취소, ◀ ▶=쪽, 후보=선택 (키보드 없이도 탈출/선택 가능)
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            if (PtInRect(&g_rcClose, pt)) {
                if (g_onCancel) g_onCancel(g_ctx);
                CandidateUI_Hide();
                return 0;
            }
            int totalPages = (g_count + g_perPage - 1) / g_perPage;
            if (PtInRect(&g_rcPrev, pt)) { if (g_page > 0) { g_page--; g_sel = 0; RefreshCandWindow(); } return 0; }
            if (PtInRect(&g_rcNext, pt)) { if (g_page < totalPages - 1) { g_page++; g_sel = 0; RefreshCandWindow(); } return 0; }
            int k = HitItem(pt);
            if (k >= 0) SelectIndex(g_page * g_perPage + k);
            return 0;
        }
        case WM_MOUSEMOVE: {     // 올린 줄을 옅게 (RFC-0020 P2)
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            int k = HitItem(pt);
            if (!g_tracking) { TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd, 0 }; g_tracking = TrackMouseEvent(&t) != 0; }
            if (k != g_hover) { g_hover = k; InvalidateRect(hwnd, NULL, FALSE); }
            return 0;
        }
        case WM_MOUSELEAVE:
            g_tracking = false;
            if (g_hover != -1) { g_hover = -1; InvalidateRect(hwnd, NULL, FALSE); }
            return 0;
        case WM_MOUSEWHEEL: {    // 휠이 우리 창에 오면 쪽을 넘긴다. 휠은 보통 포커스 창(응용)으로 가고, 전역 마우스 훅은
                                 //   쓰지 않는다(B10 — 모든 앱에 실리는 입력기가 데스크톱의 마우스를 가로채면 안 된다).
                                 //   그래서 쪽 넘김은 ◀ ▶ 단추와 글쇠(PgUp/PgDn, - =)가 몫이다.
            int totalPages = (g_count + g_perPage - 1) / g_perPage;
            short delta = (short)HIWORD(wParam);
            if (delta < 0 && g_page < totalPages - 1) { g_page++; g_sel = 0; RefreshCandWindow(); }
            else if (delta > 0 && g_page > 0) { g_page--; g_sel = 0; RefreshCandWindow(); }
            return 0;
        }
        case CANDMSG_HOOKKEY:   // 저수준 훅이 차단·전달한 탐색 키 (PuTTY류 키 라우팅 폴백)
            g_hookShift = lParam ? 1 : 0;
            CandidateUI_HandleKey((UINT)wParam);
            g_hookShift = -1;
            return 0;
        case WM_GETOBJECT:          // 화면 읽기 도구가 이 창을 물을 때 (UIA, B10)
            return CandUia_OnGetObject(hwnd, wParam, lParam);
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
    wc.style = CS_DROPSHADOW;   // 그림자 (RFC-0020 P2)
    return Jamo_EnsureClass(&wc);   // W2-05
}

void CandidateUI_Uninitialize(void) {
    CandidateUI_Hide();
    if (g_candFont) { DeleteObject(g_candFont); g_candFont = NULL; }
    UnregisterClassW(L"JamotongCandidateUI", g_hInst);
}

bool CandidateUI_Show(int x, int y, int caretTop, wchar_t **candidates, int count, int replaceLen, CandidateSelectCallback onSelect, CandidateCancelCallback onCancel, void *ctx) {
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
    JamoDiag("CAND show ownDraw=%d x=%d y=%d", (int)g_ownDraw, x, y);
    if (g_ownDraw) {
        int h = CandWindowHeight();
        if (!g_hwndCandi) {
            // RFC-0008 W1-08: 소유자 = 이 스레드의 포커스 창의 최상위 창(GetFocus 는 호출 스레드 큐 기준이라
            // 다른 스레드 창과 입력 큐가 묶일 일이 없다). 소유자가 없으면 예전처럼 소유자 없는 팝업.
            //   포커스 창이 없으면(UWP CoreWindow 등) 문서 뷰의 창(ITfContextView::GetWnd)을 소유자로 한다 —
            //   Microsoft IME 지침 "Owned window". 소유자 없는 최상위 창은 AppContainer 에서 합성되지 않았다.
            HWND focus = GetFocus();
            HWND owner = focus ? GetAncestor(focus, GA_ROOT)
                               : (g_viewWnd && IsWindow(g_viewWnd) ? GetAncestor(g_viewWnd, GA_ROOT) : NULL);
            g_hwndCandi = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                L"JamotongCandidateUI", L"", WS_POPUP,   // 테두리는 DWM(둥근 모서리) 또는 우리가 그린다 (RFC-0020 P2)
                x, y, g_winW, h, owner, NULL, g_hInst, NULL);
            JamoDiag("CAND create hwnd=%p owner=%p err=%lu", (void*)g_hwndCandi, (void*)owner,
                     (unsigned long)GetLastError());
            if (!g_hwndCandi) {
                // 표시 실패를 호출자에게 알린다 — 키만 먹고 아무것도 안 뜨던 상태를 없앤다.
                UiElem_EndCandidate();
                g_active = false; g_candidates = NULL; g_count = 0;
                g_onSelect = NULL; g_onCancel = NULL; g_ctx = NULL;
                return false;
            }
            OwnerThreadClaim();   // 이 창은 이 스레드 것이다 (W0-03 S2)
        }
        // 창이 놓인 자리의 DPI 로 글꼴·크기를 맞춘다 (아직 안 보이는 새 창 — 깜빡임 없음, W2-03)
        UINT dpi = Popup_WindowDpi(g_hwndCandi);
        if (dpi != g_dpi) {
            g_dpi = dpi;
            if (g_candFont) { DeleteObject(g_candFont); g_candFont = NULL; }
                g_winW = MeasurePageWidth();
        }
        { CandColors cc; PickCandColors(&cc); ApplyRoundedCorners(g_hwndCandi, cc.border); }   // Win11 둥근 모서리·테두리
        PlaceCandWindow();   // 모니터 작업영역 클램프(+SHOWWINDOW)
        InvalidateRect(g_hwndCandi, NULL, TRUE);
        InstallKbHook();     // PuTTY류 키 라우팅 폴백 — 자체 창 표시 중에만
    }
    JamoDiag("CAND after-place hwnd=%p vis=%d", (void*)g_hwndCandi,
             g_hwndCandi ? (int)IsWindowVisible(g_hwndCandi) : -1);
    UiElem_UpdateCandidate(0x3F);   // 첫 갱신 = 전체 비트 (uiless 문서: 첫 Update 는 all-bits)
    if (g_hwndCandi) {
        NotifyWinEvent(EVENT_OBJECT_IME_SHOW, g_hwndCandi, OBJID_CLIENT, CHILDID_SELF);   // 접근성 (W1-08)
        UpdateUiaName();                                                                  // 화면 읽기 도구 (B10)
    }
    return true;
}

// 페이지 이동/선택 변경 후 크기·내용 갱신
// 읽기 도구에 넘길 이름: "후보 2/9: 儺" — 지금 골라진 것과 몇 번째인지 (B10).
static void UpdateUiaName(void) {
    if (!g_hwndCandi || !g_candidates || g_count <= 0) return;
    int idx = g_page * g_perPage + g_sel;
    if (idx < 0 || idx >= g_count) idx = 0;
    const wchar_t *cur = g_candidates[idx] ? g_candidates[idx] : L"";
    wchar_t name[256];
    _snwprintf(name, 256, L"%d/%d %ls", idx + 1, g_count, cur);
    name[255] = L'\0';
    CandUia_SetName(g_hwndCandi, name);
}

static void RefreshCandWindow(void) {
    UiElem_UpdateCandidate(0x04|0x10|0x20);   // SELECTION|PAGEINDEX|CURRENTPAGE
    if (!g_hwndCandi) return;
    NotifyWinEvent(EVENT_OBJECT_IME_CHANGE, g_hwndCandi, OBJID_CLIENT, CHILDID_SELF);   // 접근성 (W1-08)
    UpdateUiaName();                                                                    // 화면 읽기 도구 (B10)
    g_winW = MeasurePageWidth();
    PlaceCandWindow();   // 폭 변화·화면 클램프 반영 (앵커 기준 재배치)
    InvalidateRect(g_hwndCandi, NULL, TRUE);
}

void CandidateUI_Hide(void) {
    JamoDiag("CAND hide tid=%lu owner-tid=%lu hwnd=%p", (unsigned long)GetCurrentThreadId(),
             (unsigned long)g_ownerTid, (void*)g_hwndCandi);
    OwnerThreadGuard("Hide");
    UiElem_EndCandidate();   // 게이트 종료 (began 아니면 no-op) — EndUIElement 는 의무
    RemoveKbHook();   // 표시 중에만 유지되는 키 라우팅 폴백 해제
    CandUia_Release();   // 화면 읽기 도구용 제공자 (창이 사라진다)
    if (g_hwndCandi) {
        NotifyWinEvent(EVENT_OBJECT_IME_HIDE, g_hwndCandi, OBJID_CLIENT, CHILDID_SELF);   // 접근성 (W1-08)
        g_inHide = true;
        DestroyWindow(g_hwndCandi);   // WM_NCDESTROY 가 핸들을 비운다
        g_inHide = false;
        g_hwndCandi = NULL;
    }
    g_candidates = NULL;
    g_count = 0;
    g_active = false;
    g_ownDraw = true;
    g_onKey = NULL;   // 병음 방식은 이 창과 함께 끝난다 — 다음 Show 앞에 다시 정한다
    g_onSegKey = NULL;
    g_digitsToInput = false;
    g_notes = NULL;
    g_title[0] = L'\0';
    g_horizontal = false;
    g_hover = -1;
    g_tracking = false;
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
        // 창을 먼저 닫고 콜백을 부른다 — 콜백이 곧바로 새 후보창을 열 수 있게(순차 입력의 이어 변환:
        //   앞부분을 고르면 나머지 읽기의 후보를 다시 띄운다). 콜백이 쓰는 것(문자열·문맥·교체 길이)은 미리 잡아 둔다.
        void (*cb)(int, const wchar_t *, void *) = g_onSelect;
        void *ctx = g_ctx;
        const wchar_t *str = g_candidates[realIdx];
        CandidateUI_Hide();
        if (cb) cb(realIdx, str, ctx);
    }
}

void CandidateUI_SetPinyinKeys(CandidateKeyCallback onKey) { g_onKey = onKey; }
void CandidateUI_SetSegmentKeys(CandidateKeyCallback onKey) { g_onSegKey = onKey; }
void CandidateUI_SetDigitsToInput(bool on) { g_digitsToInput = on; }
void CandidateUI_SetTitle(const wchar_t *title) { lstrcpynW(g_title, title ? title : L"", 64); }
void CandidateUI_SetHorizontal(bool on) { g_horizontal = on; }
void CandidateUI_SetNotes(wchar_t **notes) { g_notes = notes; }

// 병음 방식의 글쇠. 처리했으면 true, 입력기에 넘길 글쇠면 false.
static bool HandlePinyinKey(UINT vKey) {
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    int totalPages = (g_count + g_perPage - 1) / g_perPage;
    if (vKey == VK_SPACE) { SelectIndex(g_page * g_perPage + g_sel); return true; }
    if (g_digitsToInput && ((vKey >= '0' && vKey <= '9') || vKey == VK_OEM_MINUS || vKey == VK_OEM_PLUS)) return false;   // V 모드
    if (vKey >= '1' && vKey <= '9') {
        if (shift) return false;                               // ! @ # … 는 문장부호
        SelectIndex(g_page * g_perPage + (vKey - '1'));
        return true;
    }
    if (!shift && vKey == VK_OEM_PLUS)  { if (g_page < totalPages - 1) { g_page++; g_sel = 0; RefreshCandWindow(); } return true; }
    if (!shift && vKey == VK_OEM_MINUS) { if (g_page > 0) { g_page--; g_sel = 0; RefreshCandWindow(); } return true; }
    if (vKey == VK_RETURN || vKey == VK_ESCAPE || (!shift && (vKey == VK_OEM_4 || vKey == VK_OEM_6))) {
        CandidateKeyCallback cb = g_onKey;   // 창을 먼저 닫는다 — 콜백이 새 후보창을 열 수 있게
        void *ctx = g_ctx;
        int idx = g_page * g_perPage + g_sel;
        CandidateUI_Hide();
        if (cb) cb(vKey, idx, ctx);
        return true;
    }
    return false;                                              // 글자·백스페이스·문장부호 — 입력기가 읽기를 고친다
}

bool CandidateUI_HandleKey(UINT vKey) {
    if (!OwnerThreadGuard("HandleKey")) return false;   // 남의 스레드면 이 키는 응용의 것이다 (B5)
    if (!g_hwndCandi) return false;
    if (g_onSegKey) {   // 일본어 문절 편집 (0.73.0): ←→ 는 문절의 것이라 후보는 ↑↓·사이띄개로 옮긴다
        if (vKey == VK_SHIFT || vKey == VK_LSHIFT || vKey == VK_RSHIFT || vKey == VK_CONTROL || vKey == VK_LCONTROL || vKey == VK_RCONTROL
            || vKey == VK_MENU || vKey == VK_LMENU || vKey == VK_RMENU || vKey == VK_LWIN || vKey == VK_RWIN || vKey == VK_CAPITAL) return true;
        bool shift = g_hookShift >= 0 ? g_hookShift != 0 : (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (vKey == VK_SPACE) {   // 다음 후보 (끝에서는 처음으로)
            if (g_page * g_perPage + g_sel + 1 >= g_count) { g_page = 0; g_sel = 0; RefreshCandWindow(); return true; }
            vKey = VK_DOWN;
        }
        bool ours = vKey == VK_UP || vKey == VK_DOWN || vKey == VK_PRIOR || vKey == VK_NEXT || vKey == VK_ESCAPE
                    || (!shift && vKey >= '1' && vKey <= '9');
        if (!ours) {
            CandidateKeyCallback cb = g_onSegKey;   // 창을 먼저 닫는다 — 콜백이 새 후보창을 연다
            void *ctx = g_ctx;
            int idx = g_page * g_perPage + g_sel;
            CandidateUI_Hide();
            cb(vKey | (shift ? CAND_KEY_SHIFT : 0), idx, ctx);
            return vKey == VK_LEFT || vKey == VK_RIGHT || vKey == VK_RETURN || vKey == VK_BACK;
        }
    } else if (g_horizontal) {   // 가로 후보줄: ←→ = 후보 옮기기, ↑↓ = 쪽 (RFC-0020 P2)
        if (vKey == VK_RIGHT) vKey = VK_DOWN; else if (vKey == VK_LEFT) vKey = VK_UP;
        else if (vKey == VK_DOWN) vKey = VK_NEXT; else if (vKey == VK_UP) vKey = VK_PRIOR;
    }
    if (g_onKey) {
        bool nav = vKey == VK_UP || vKey == VK_DOWN || vKey == VK_LEFT || vKey == VK_RIGHT || vKey == VK_PRIOR || vKey == VK_NEXT;
        if (!nav) return HandlePinyinKey(vKey);
    }

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
