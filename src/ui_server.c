// ui_server.c — RFC-0015: 데스크톱 세션에서 도는 UI 헬퍼.
//
// UWP 호스트 안의 TIP 은 창을 띄워도 화면에 나타나지 않는다(매뉴얼 §14.7). 그래서 TIP 이
// named pipe 로 "이 후보들을 여기에 그려라"를 보내면 이 프로세스가 대신 그린다.
// **표시 전용**이다: 키도 마우스도 받지 않고, 돌려보내는 것도 없다. 받은 문자열은 그리기
// 외의 어떤 의미로도 해석하지 않는다.
#include "ui_server.h"
#include "ui_ipc.h"
#include <sddl.h>    // ConvertStringSecurityDescriptorToSecurityDescriptorW
#include <stdio.h>
#include <string.h>

#define WM_UISRV_SHOW   (WM_APP + 21)
#define WM_UISRV_UPDATE (WM_APP + 22)
#define WM_UISRV_HIDE   (WM_APP + 23)

#define ROW_H_MIN  18
#define PAD_X      10
#define PAD_TOP     6

static HWND    g_hwnd = NULL;
static HFONT   g_font = NULL;
static int     g_fontH = 0;
static wchar_t g_fontFace[JAMO_UIIPC_MAX_FACE] = L"Malgun Gothic";

// 받은 상태 (창 스레드에서만 만진다)
static wchar_t (*g_lines)[JAMO_UIIPC_MAX_CANDLEN] = NULL;
static int g_count = 0, g_perPage = 9, g_sel = 0;
static int g_anchorX = 100, g_anchorY = 100, g_caretTop = 96;

static void EnsureFont(int h) {
    if (h < 12) h = 12;
    if (h > 72) h = 72;
    if (g_font && g_fontH == h) return;
    if (g_font) DeleteObject(g_font);
    g_font = CreateFontW(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, g_fontFace);
    g_fontH = h;
}

static int PageStart(void) { return (g_perPage > 0) ? (g_sel / g_perPage) * g_perPage : 0; }

static int MeasureWidth(void) {
    int w = 120;
    HDC hdc = GetDC(g_hwnd);
    if (!hdc) return w;
    HFONT of = (HFONT)SelectObject(hdc, g_font);
    int start = PageStart();
    for (int i = start; i < g_count && i < start + g_perPage; i++) {
        SIZE sz;
        if (GetTextExtentPoint32W(hdc, g_lines[i], (int)wcslen(g_lines[i]), &sz) && sz.cx + PAD_X * 2 > w)
            w = sz.cx + PAD_X * 2;
    }
    SelectObject(hdc, of);
    ReleaseDC(g_hwnd, hdc);
    return w;
}

static void PlaceWindow(void) {
    if (!g_hwnd || g_count <= 0) return;
    int rowH = (g_fontH + 6 > ROW_H_MIN) ? g_fontH + 6 : ROW_H_MIN;
    int rows = g_count - PageStart();
    if (rows > g_perPage) rows = g_perPage;
    int h = rows * rowH + PAD_TOP * 2 + (g_count > g_perPage ? rowH : 0);
    int w = MeasureWidth();

    // 캐럿 아래가 기본, 화면 밖이면 캐럿 위로 뒤집는다(작업표시줄 검색처럼 바닥에 붙은 자리 대응).
    MONITORINFO mi; mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromPoint((POINT){g_anchorX, g_anchorY}, MONITOR_DEFAULTTONEAREST);
    int x = g_anchorX, y = g_anchorY;
    SetRect(&mi.rcWork, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    if (GetMonitorInfoW(mon, &mi)) {
        if (y + h > mi.rcWork.bottom) {
            int above = g_caretTop - h - 4;
            y = (above >= mi.rcWork.top) ? above : mi.rcWork.bottom - h;
        }
        if (y < mi.rcWork.top) y = mi.rcWork.top;
        if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
        if (x < mi.rcWork.left) x = mi.rcWork.left;
    }
    // 셸 팝업(작업표시줄 검색 등)은 일반 TOPMOST 창보다 위에 그려진다 — 캐럿 옆에 두면 통째로
    // 가려진다. 포그라운드 창과 겹치면 그 바깥(왼쪽 우선, 없으면 오른쪽)으로 피한다.
    // 캐럿에서 멀어지지만 "안 보이는 것"보다 낫다. 겹치지 않는 보통 UWP 앱에서는 그대로 둔다.
    HWND fg = GetForegroundWindow();
    RECT fr;
    if (fg && fg != g_hwnd && GetWindowRect(fg, &fr)) {
        RECT mine = { x, y, x + w, y + h }, inter;
        if (IntersectRect(&inter, &mine, &fr)) {
            if (fr.left - w - 4 >= mi.rcWork.left)        x = fr.left - w - 4;
            else if (fr.right + 4 + w <= mi.rcWork.right) x = fr.right + 4;
        }
    }
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_hwnd, NULL, TRUE);
}

static LRESULT CALLBACK SrvWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
            FrameRect(hdc, &rc, (HBRUSH)GetStockObject(GRAY_BRUSH));
            if (g_font && g_count > 0) {
                HFONT of = (HFONT)SelectObject(hdc, g_font);
                SetBkMode(hdc, TRANSPARENT);
                int rowH = (g_fontH + 6 > ROW_H_MIN) ? g_fontH + 6 : ROW_H_MIN;
                int start = PageStart();
                int y = PAD_TOP;
                for (int i = start; i < g_count && i < start + g_perPage; i++, y += rowH) {
                    if (i == g_sel) {
                        RECT hr = { 2, y - 2, rc.right - 2, y + rowH - 2 };
                        FillRect(hdc, &hr, (HBRUSH)(COLOR_HIGHLIGHT + 1));
                        SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
                    } else {
                        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
                    }
                    TextOutW(hdc, PAD_X, y, g_lines[i], (int)wcslen(g_lines[i]));
                }
                if (g_count > g_perPage) {   // 페이지 표시
                    wchar_t foot[32];
                    _snwprintf(foot, 32, L"[%d/%d]", PageStart() / g_perPage + 1,
                               (g_count + g_perPage - 1) / g_perPage);
                    SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
                    TextOutW(hdc, PAD_X, y, foot, (int)wcslen(foot));
                }
                SelectObject(hdc, of);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_UISRV_SHOW:   PlaceWindow(); return 0;
        case WM_UISRV_UPDATE: PlaceWindow(); return 0;
        case WM_UISRV_HIDE:   ShowWindow(hwnd, SW_HIDE); return 0;
        case WM_DESTROY:      PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// 파이프에서 받은 한 메시지를 상태에 반영한다. 신뢰 경계를 넘어온 값이라 전부 걸러 쓴다.
static void ApplyMessage(const unsigned char *buf, DWORD len) {
    if (len < sizeof(JamoUiMsgHeader)) return;
    const JamoUiMsgHeader *h = (const JamoUiMsgHeader*)buf;
    if (h->version != JAMO_UIIPC_VERSION) return;   // 모르는 판이면 조용히 무시

    if (h->msg == JAMO_UIMSG_HIDE) {
        g_count = 0;
        if (g_hwnd) PostMessageW(g_hwnd, WM_UISRV_HIDE, 0, 0);
        return;
    }
    if (h->msg == JAMO_UIMSG_UPDATE) {
        if (h->perPage > 0 && h->perPage <= JAMO_UIIPC_MAX_CAND) g_perPage = (int)h->perPage;
        if ((int)h->selection < g_count) g_sel = (int)h->selection;
        if (g_hwnd) PostMessageW(g_hwnd, WM_UISRV_UPDATE, 0, 0);
        return;
    }
    if (h->msg != JAMO_UIMSG_SHOW) return;

    UINT32 count = h->count;
    if (count == 0 || count > JAMO_UIIPC_MAX_CAND) return;
    size_t need = sizeof(JamoUiMsgHeader) + (size_t)count * JAMO_UIIPC_MAX_CANDLEN * sizeof(wchar_t);
    if (len < need) return;   // 길이가 안 맞으면 버린다

    if (!g_lines) {
        g_lines = (wchar_t (*)[JAMO_UIIPC_MAX_CANDLEN])
                  calloc(JAMO_UIIPC_MAX_CAND, sizeof(*g_lines));
        if (!g_lines) return;
    }
    const wchar_t *body = (const wchar_t*)(buf + sizeof(JamoUiMsgHeader));
    for (UINT32 i = 0; i < count; i++) {
        const wchar_t *src = body + (size_t)i * JAMO_UIIPC_MAX_CANDLEN;
        memcpy(g_lines[i], src, JAMO_UIIPC_MAX_CANDLEN * sizeof(wchar_t));
        g_lines[i][JAMO_UIIPC_MAX_CANDLEN - 1] = L'\0';   // 종료 보장(보낸 쪽을 믿지 않는다)
    }
    g_count = (int)count;
    g_perPage = (h->perPage > 0 && h->perPage <= JAMO_UIIPC_MAX_CAND) ? (int)h->perPage : 9;
    g_sel = ((int)h->selection < g_count) ? (int)h->selection : 0;
    g_anchorX = h->anchorX; g_anchorY = h->anchorY; g_caretTop = h->caretTop;
    if (h->fontFace[0]) {
        wchar_t face[JAMO_UIIPC_MAX_FACE];
        memcpy(face, h->fontFace, sizeof(face));
        face[JAMO_UIIPC_MAX_FACE - 1] = L'\0';
        wcsncpy(g_fontFace, face, JAMO_UIIPC_MAX_FACE - 1);
        g_fontFace[JAMO_UIIPC_MAX_FACE - 1] = L'\0';
        g_fontH = 0;   // 글꼴이 바뀌었으니 다시 만든다
    }
    EnsureFont(h->fontSize > 0 ? (int)h->fontSize : 20);
    if (g_hwnd) PostMessageW(g_hwnd, WM_UISRV_SHOW, 0, 0);
}

// 파이프 대기 루프 (자기 스레드). 한 번에 한 클라이언트, 끊기면 다시 만든다.
static DWORD WINAPI PipeThread(LPVOID param) {
    (void)param;
    DWORD sid = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sid)) sid = 0;
    wchar_t name[128];
    _snwprintf(name, 128, JAMO_UIIPC_PIPE_FMT, (unsigned long)sid);
    name[127] = L'\0';

    // 자기 사용자 + ALL APPLICATION PACKAGES(=UWP 호스트 안의 TIP)만 허용. 다른 사용자는 못 연다.
    SECURITY_ATTRIBUTES sa; SECURITY_DESCRIPTOR sd;
    PSECURITY_DESCRIPTOR psd = NULL;
    ULONG sdLen = 0;
    // D:(A;;GA;;;OW)(A;;GWGR;;;AC) — 소유자 전부, AppContainer 는 읽기/쓰기
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;OW)(A;;0x12019b;;;AC)", SDDL_REVISION_1, &psd, &sdLen)) {
        sa.nLength = sizeof(sa); sa.lpSecurityDescriptor = psd; sa.bInheritHandle = FALSE;
    } else {
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        sa.nLength = sizeof(sa); sa.lpSecurityDescriptor = &sd; sa.bInheritHandle = FALSE;
    }

    unsigned char *buf = (unsigned char*)malloc(64 * 1024);
    if (!buf) return 1;

    for (;;) {
        HANDLE pipe = CreateNamedPipeW(name, PIPE_ACCESS_INBOUND,
                                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                       1, 0, 64 * 1024, 0, &sa);
        if (pipe == INVALID_HANDLE_VALUE) { Sleep(1000); continue; }
        if (!ConnectNamedPipe(pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipe); continue;
        }
        for (;;) {
            DWORD got = 0;
            if (!ReadFile(pipe, buf, 64 * 1024, &got, NULL) || got == 0) break;
            ApplyMessage(buf, got);
        }
        // 클라이언트(호스트 프로세스)가 사라졌다 — 창을 치우고 다음 연결을 기다린다.
        g_count = 0;
        if (g_hwnd) PostMessageW(g_hwnd, WM_UISRV_HIDE, 0, 0);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

int UiServer_Run(HINSTANCE hInst) {
    // 세션당 하나만.
    DWORD sid = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sid)) sid = 0;
    wchar_t mname[128];
    _snwprintf(mname, 128, JAMO_UIIPC_MUTEX_FMT, (unsigned long)sid);
    mname[127] = L'\0';
    HANDLE mtx = CreateMutexW(NULL, TRUE, mname);
    if (!mtx || GetLastError() == ERROR_ALREADY_EXISTS) return 0;   // 이미 돈다 — 조용히 물러난다

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = SrvWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"JamotongUiHelper";
    RegisterClassW(&wc);

    // 입력을 받지 않는 표시 전용 창 (NOACTIVATE·TRANSPARENT: 클릭도 통과시킨다)
    g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                             L"JamotongUiHelper", L"", WS_POPUP | WS_BORDER,
                             0, 0, 10, 10, NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;
    EnsureFont(20);

    HANDLE th = CreateThread(NULL, 0, PipeThread, NULL, 0, NULL);
    if (th) CloseHandle(th);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
