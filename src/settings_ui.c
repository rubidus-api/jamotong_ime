#include "settings_ui.h"
#include "klay.h"      // Klay_Load — Add 버튼으로 .jmt 자판 불러오기
#include <commctrl.h>
#include <stdio.h>

extern HINSTANCE g_hInst;   // 이 모듈(DLL/EXE) 핸들 — 윈도 클래스 소유자.
                            // GetModuleHandleW(NULL)=EXE 인스턴스를 쓰면 DLL WndProc과 소유자가 어긋나
                            // DLL 언로드 후 낡은 클래스가 남아 재등록 실패/댕글링 WndProc이 된다.

static HWND g_hwndSettings = NULL;
static HANDLE g_hThreadSettings = NULL;
static JamotongConfig *g_pRealConfig = NULL;

// 임시 설정 상태 (트랜잭션/Revert 용도)
static JamotongConfig g_TempConfig;
static JamotongConfig g_LastSavedConfig;

// DPI 스케일링 전역 변수
static int g_ManualDpiScale = 0; // 0 = Auto, 100, 125, 150...
static int g_CurrentDpi = 96;

#define ID_BTN_APPLY   1001
#define ID_BTN_CANCEL  1002
#define ID_BTN_REVERT  1003
#define ID_BTN_RESET   1004
#define ID_BTN_IMPORT  1005
#define ID_BTN_EXPORT  1006
#define ID_CMB_DPI     1007

// Layouts Group
#define ID_LST_LAYOUTS      1008
#define ID_BTN_LAYOUT_UP    1009
#define ID_BTN_LAYOUT_DOWN  1010
#define ID_BTN_LAYOUT_DEL   1011
#define ID_BTN_LAYOUT_TOGGLE 1016
#define ID_BTN_LAYOUT_ADD    1017

// 탭 + IME 옵션 컨트롤
#define ID_TAB               1030
#define ID_CHK_FULLWIDTH     1031
#define ID_CHK_JAMODELETE    1032
#define ID_LBL_LAYOUT_HINT   1035
#define ID_LBL_SHORTCUT_HINT 1036
#define ID_CHK_PREVIEW       1040   // 조합 미리보기 오버레이 (RFC-0002)
#define ID_LBL_PVFONT        1041   // 미리보기 글꼴 표시
#define ID_BTN_PVFONT_SET    1042   // 글꼴 선택(ChooseFont)
#define ID_CMB_PVSIZE        1043   // 미리보기 글꼴 크기 (Auto / 고정 px, 직접 입력 가능)

// 탭 소속 태그 (컨트롤 GWLP_USERDATA). TAB_ALWAYS=탭전환과 무관하게 항상 표시.
enum { TAB_LAYOUTS = 0, TAB_SHORTCUTS = 1, TAB_OPTIONS = 2, TAB_GENERAL = 3, TAB_ALWAYS = 99 };
static int g_curTab = 0;

// Shortcuts Group — 위 콤보로 기능을 고르고 아래 리스트에서 그 기능의 단축키를 추가/삭제
#define ID_LST_SHORTCUTS    1012
#define ID_BTN_SHORTCUT_ADD 1013
#define ID_BTN_SHORTCUT_DEL 1014
#define ID_BTN_SHORTCUT_EDIT 1015
#define ID_CMB_SCFN         1044   // 기능 선택 콤보 (ShortcutFn 순서)

// 기능 콤보 표시 이름 (ShortcutFn 인덱스와 동일 순서)
static const wchar_t *g_scFnNames[SC_FN_COUNT] = {
    L"Layout switch (Korean/English toggle)",
    L"Hanja / special character conversion",
    L"Unicode code point input",
    L"Open IME settings (this window)",
};
static int g_curScFn = 0;   // 현재 선택된 기능 (DPI 재구성에도 유지)

static HFONT g_hUiFont = NULL;   // Segoe UI 12pt (DPI 스케일) — 모든 컨트롤에 적용

// 헬퍼: 픽셀 스케일링 (100% = 96 DPI 기준)
static int ScaleX(int x) { return MulDiv(x, g_CurrentDpi, 96); }
static int ScaleY(int y) { return MulDiv(y, g_CurrentDpi, 96); }

// ── 테마(다크/라이트) ─────────────────────────────────────────────────────
static bool     g_dark = false;
static COLORREF g_clrBg, g_clrText, g_clrCtl;
static HBRUSH   g_brBg = NULL, g_brCtl = NULL;

static void InitTheme(void) {
    DWORD v = 1, sz = sizeof(v); HKEY hk;   // AppsUseLightTheme: 1=light, 0=dark
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &hk) == ERROR_SUCCESS) {
        RegQueryValueExW(hk, L"AppsUseLightTheme", NULL, NULL, (BYTE*)&v, &sz);
        RegCloseKey(hk);
    }
    g_dark = (v == 0);
    if (g_dark) { g_clrBg = RGB(32,32,32); g_clrText = RGB(235,235,235); g_clrCtl = RGB(45,45,45); }
    else        { g_clrBg = GetSysColor(COLOR_BTNFACE); g_clrText = GetSysColor(COLOR_WINDOWTEXT); g_clrCtl = GetSysColor(COLOR_WINDOW); }
    if (g_brBg)  DeleteObject(g_brBg);
    if (g_brCtl) DeleteObject(g_brCtl);
    g_brBg  = CreateSolidBrush(g_clrBg);
    g_brCtl = CreateSolidBrush(g_clrCtl);
}

// 컨트롤에 다크/라이트 uxtheme 적용 (스크롤바·테두리 색). uxtheme.dll 동적 로드.
static void ThemeControl(HWND h) {
    static HRESULT (WINAPI *pSet)(HWND, LPCWSTR, LPCWSTR) = NULL;
    static int tried = 0;
    if (!tried) { tried = 1; HMODULE ux = LoadLibraryW(L"uxtheme.dll"); if (ux) pSet = (void*)GetProcAddress(ux, "SetWindowTheme"); }
    if (pSet) pSet(h, g_dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
}
static BOOL CALLBACK ThemeChildProc(HWND hChild, LPARAM lp) { (void)lp; ThemeControl(hChild); return TRUE; }

static void ApplyDarkTitleBar(HWND hwnd) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    HRESULT (WINAPI *pSet)(HWND, DWORD, LPCVOID, DWORD) = (void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (pSet) { BOOL d = g_dark; pSet(hwnd, 20, &d, sizeof(d)); }   // DWMWA_USE_IMMERSIVE_DARK_MODE = 20
    FreeLibrary(dwm);
}

// 좌우 구분 가상키 → 사람이 읽는 이름
static void VKeyName(UINT vk, wchar_t *out, int cap) {
    switch (vk) {
        case VK_SPACE:    lstrcpynW(out, L"Space", cap); return;
        case VK_RETURN:   lstrcpynW(out, L"Enter", cap); return;
        case VK_TAB:      lstrcpynW(out, L"Tab", cap); return;
        case VK_HANGUL:   lstrcpynW(out, L"Hangul", cap); return;
        case VK_HANJA:    lstrcpynW(out, L"Hanja", cap); return;
        case VK_LMENU:    lstrcpynW(out, L"Left Alt", cap); return;
        case VK_RMENU:    lstrcpynW(out, L"Right Alt", cap); return;
        case VK_LCONTROL: lstrcpynW(out, L"Left Ctrl", cap); return;
        case VK_RCONTROL: lstrcpynW(out, L"Right Ctrl", cap); return;
        case VK_LSHIFT:   lstrcpynW(out, L"Left Shift", cap); return;
        case VK_RSHIFT:   lstrcpynW(out, L"Right Shift", cap); return;
        case VK_LWIN:     lstrcpynW(out, L"Left Win", cap); return;
        case VK_RWIN:     lstrcpynW(out, L"Right Win", cap); return;
        case VK_ESCAPE:   lstrcpynW(out, L"Esc", cap); return;
    }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { out[0] = (wchar_t)vk; out[1] = 0; return; }
    if (vk >= VK_F1 && vk <= VK_F24) { swprintf(out, cap, L"F%u", vk - VK_F1 + 1); return; }
    swprintf(out, cap, L"0x%02X", vk);
}

static void FormatShortcutStr(ShortcutKey *sk, wchar_t *buf, int maxLen) {
    buf[0] = L'\0';
    if (sk->mods & SMOD_CTRL)  wcscat(buf, L"Ctrl + ");
    if (sk->mods & SMOD_ALT)   wcscat(buf, L"Alt + ");
    if (sk->mods & SMOD_SHIFT) wcscat(buf, L"Shift + ");
    if (sk->mods & SMOD_GUI)   wcscat(buf, L"Win + ");
    wchar_t kn[32]; VKeyName(sk->vKey, kn, 32);
    wcsncat(buf, kn, maxLen - (int)wcslen(buf) - 1);
}

// ── 단축키 캡처(핫키 녹화) ───────────────────────────────────────────────
// 팝업을 띄우고 사용자가 누른 키 조합을 좌우 구분해 기록한다. 모디파이어만 눌렀다 떼면
// (예: 오른쪽 Alt) 그 자체가 단축키가 되고, 일반 키를 누르면 {키, 현재 모디파이어}로 기록.
static ShortcutKey g_capResult;
static bool g_capDone;
static UINT g_capPendingMod;

static bool IsModVK(UINT vk) {
    return vk==VK_LSHIFT||vk==VK_RSHIFT||vk==VK_LCONTROL||vk==VK_RCONTROL||
           vk==VK_LMENU||vk==VK_RMENU||vk==VK_LWIN||vk==VK_RWIN;
}
static UINT SkModBit(UINT vk) {
    switch (vk) {
        case VK_LSHIFT: case VK_RSHIFT: return SMOD_SHIFT;
        case VK_LCONTROL: case VK_RCONTROL: return SMOD_CTRL;
        case VK_LMENU: case VK_RMENU: return SMOD_ALT;
        case VK_LWIN: case VK_RWIN: return SMOD_GUI;
    }
    return 0;
}

static LRESULT CALLBACK CaptureProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_KEYDOWN: case WM_SYSKEYDOWN: {
            UINT rvk = Config_ResolveVK(w, l);
            if (rvk == VK_ESCAPE) { g_capResult.vKey = 0; g_capDone = true; DestroyWindow(h); return 0; }
            if (IsModVK(rvk)) { g_capPendingMod = rvk; }   // 모디파이어 → 주 키를 기다림
            else {
                g_capResult.vKey = rvk;
                g_capResult.mods = Config_CurrentMods() & ~SkModBit(rvk);
                g_capDone = true; DestroyWindow(h);
            }
            return 0;
        }
        case WM_KEYUP: case WM_SYSKEYUP: {
            UINT rvk = Config_ResolveVK(w, l);
            if (!g_capDone && g_capPendingMod && rvk == g_capPendingMod) {
                g_capResult.vKey = g_capPendingMod; g_capResult.mods = 0;   // 모디파이어 단독 (Right Alt 등)
                g_capDone = true; DestroyWindow(h);
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
            RECT rc; GetClientRect(h, &rc);
            FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
            SetBkMode(hdc, TRANSPARENT);
            HFONT oldFont = g_hUiFont ? (HFONT)SelectObject(hdc, g_hUiFont) : NULL;   // 설정창과 동일 글꼴
            DrawTextW(hdc, L"Press the key or combination to use.\n(single keys like Right Alt / Hangul work too - Esc = cancel)",
                      -1, &rc, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
            if (oldFont) SelectObject(hdc, oldFont);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_DESTROY:   // 외부 요인으로 파괴돼도 모달 루프가 반드시 종료되도록 (행 방지)
            g_capDone = true;
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// 부모 창을 모달로 잠그고 캡처 팝업을 띄운다. true = 캡처됨(*out 채움).
static bool CaptureShortcut(HWND parent, ShortcutKey *out) {
    static bool s_reg = false;
    if (!s_reg) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = CaptureProc; wc.hInstance = g_hInst;
        wc.lpszClassName = L"JamotongCaptureClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc); s_reg = true;
    }
    g_capDone = false; g_capPendingMod = 0; g_capResult.vKey = 0; g_capResult.mods = 0;
    RECT pr; GetWindowRect(parent, &pr);
    int w = ScaleX(380), ht = ScaleY(140);
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - ht) / 2;
    HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, L"JamotongCaptureClass",
        L"Set Shortcut", WS_POPUP | WS_CAPTION, x, y, w, ht, parent, NULL, g_hInst, NULL);
    if (!h) return false;
    EnableWindow(parent, FALSE);
    ShowWindow(h, SW_SHOW); SetForegroundWindow(h); SetFocus(h);
    MSG msg;
    while (!g_capDone && GetMessageW(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    // 모달 루프가 WM_QUIT을 삼켰으면 바깥(설정창) 루프를 위해 되던진다 — 안 하면 설정 스레드가
    // 종료 신호를 잃고 GetMessage에서 영원히 대기(SettingsUI_Shutdown 타임아웃→스레드 잔류).
    if (!g_capDone && msg.message == WM_QUIT) PostQuitMessage((int)msg.wParam);
    EnableWindow(parent, TRUE); SetForegroundWindow(parent);
    if (out && g_capResult.vKey) { *out = g_capResult; return true; }
    return false;
}

static void RefreshLists(HWND hwnd) {
    HWND hLstLayouts = GetDlgItem(hwnd, ID_LST_LAYOUTS);
    HWND hLstShortcuts = GetDlgItem(hwnd, ID_LST_SHORTCUTS);
    
    SendMessageW(hLstLayouts, LB_RESETCONTENT, 0, 0);
    SendMessageW(hLstShortcuts, LB_RESETCONTENT, 0, 0);
    
    for (int i = 0; i < g_TempConfig.layoutCount; i++) {
        wchar_t buf[96];
        // 같은 계열의 채움/빈 사각형으로 사용 여부 표시 (◼=켜짐, ◻=꺼짐 — 모양 통일)
        swprintf(buf, 96, L"%ls  %ls", g_TempConfig.layouts[i].enabled ? L"\x25FC" : L"\x25FB",
                 g_TempConfig.layouts[i].name ? g_TempConfig.layouts[i].name : L"?");
        SendMessageW(hLstLayouts, LB_ADDSTRING, 0, (LPARAM)buf);
    }
    
    // 단축키 리스트 = 현재 선택된 기능(콤보)의 목록
    ShortcutList *sl = &g_TempConfig.shortcuts[g_curScFn];
    for (int i = 0; i < sl->count; i++) {
        wchar_t buf[64];
        FormatShortcutStr(&sl->keys[i], buf, 64);
        SendMessageW(hLstShortcuts, LB_ADDSTRING, 0, (LPARAM)buf);
    }
}

// 자판 사용(켜짐) 토글. 최소 1개는 켜진 상태를 유지한다.
static void ToggleLayoutEnabled(HWND hwnd, int sel) {
    if (sel < 0 || sel >= g_TempConfig.layoutCount) return;
    if (g_TempConfig.layouts[sel].enabled) {
        int on = 0;
        for (int i = 0; i < g_TempConfig.layoutCount; i++) if (g_TempConfig.layouts[i].enabled) on++;
        if (on <= 1) { MessageBoxW(hwnd, L"At least one layout must stay On.", L"Info", MB_OK); return; }
    }
    g_TempConfig.layouts[sel].enabled = !g_TempConfig.layouts[sel].enabled;
    RefreshLists(hwnd);
    SendMessageW(GetDlgItem(hwnd, ID_LST_LAYOUTS), LB_SETCURSEL, sel, 0);
}

// 컨트롤 생성 + 탭 소속 태그. 좌표는 논리값(ScaleX/Y로 스케일).
static HWND MkCtl(HWND parent, LPCWSTR cls, LPCWSTR txt, DWORD style, DWORD ex,
                  int x, int y, int w, int h, int id, int tab) {
    HWND c = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | style,
                             ScaleX(x), ScaleY(y), ScaleX(w), ScaleY(h),
                             parent, (HMENU)(INT_PTR)id, NULL, NULL);
    if (c) SetWindowLongPtrW(c, GWLP_USERDATA, tab);
    return c;
}
// 직접 자식만 순회한다. EnumChildWindows는 손자까지 재귀하므로 콤보 박스 내부의
// 에디트 자식(USERDATA=0=TAB_LAYOUTS)까지 숨겨버림 — 크기 콤보가 "Auto" 대신
// 빈칸으로 보이던 원인.
static void ShowTab(HWND hwnd, int sel) {
    g_curTab = sel;
    for (HWND h = GetWindow(hwnd, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
        LONG_PTR t = GetWindowLongPtrW(h, GWLP_USERDATA);
        ShowWindow(h, (t == sel || t == TAB_ALWAYS) ? SW_SHOW : SW_HIDE);
    }
}

// 창 논리 크기 (세로는 리사이즈로 늘어남)
#define WIN_W 340
#define WIN_H_MIN 356
static int g_winH = WIN_H_MIN;   // 현재 논리 높이

// UI 생성 — 탭 4개(Layouts/Shortcuts/IME Options/General) + 하단 Apply/Cancel(항상 표시)
static void CreateControls(HWND hwnd) {
    const int W = WIN_W, H = g_winH, BB = 40;   // BB=하단 바 높이
    const int listH = H - BB - 96;              // 리스트 높이(세로 리사이즈 반영)

    // 탭 컨트롤 (항상 표시)
    HWND tab = MkCtl(hwnd, L"SysTabControl32", NULL, 0, 0, 6, 6, W - 12, H - BB - 8, ID_TAB, TAB_ALWAYS);
    TCITEMW ti = {0}; ti.mask = TCIF_TEXT;
    ti.pszText = (LPWSTR)L"Layouts";     SendMessageW(tab, TCM_INSERTITEMW, 0, (LPARAM)&ti);
    ti.pszText = (LPWSTR)L"Shortcuts";   SendMessageW(tab, TCM_INSERTITEMW, 1, (LPARAM)&ti);
    ti.pszText = (LPWSTR)L"IME Options"; SendMessageW(tab, TCM_INSERTITEMW, 2, (LPARAM)&ti);
    ti.pszText = (LPWSTR)L"General";     SendMessageW(tab, TCM_INSERTITEMW, 3, (LPARAM)&ti);

    // ── Tab: Layouts ──
    MkCtl(hwnd, L"LISTBOX", NULL, LBS_NOTIFY | WS_VSCROLL, WS_EX_CLIENTEDGE,
          14, 40, 224, listH, ID_LST_LAYOUTS, TAB_LAYOUTS);
    MkCtl(hwnd, L"BUTTON", L"On/Off", BS_PUSHBUTTON, 0, 244, 40, 82, 26, ID_BTN_LAYOUT_TOGGLE, TAB_LAYOUTS);
    MkCtl(hwnd, L"BUTTON", L"\x25B2", BS_PUSHBUTTON, 0, 244, 70, 82, 26, ID_BTN_LAYOUT_UP, TAB_LAYOUTS);
    MkCtl(hwnd, L"BUTTON", L"\x25BC", BS_PUSHBUTTON, 0, 244, 100, 82, 26, ID_BTN_LAYOUT_DOWN, TAB_LAYOUTS);
    MkCtl(hwnd, L"BUTTON", L"Add",    BS_PUSHBUTTON, 0, 244, 130, 82, 26, ID_BTN_LAYOUT_ADD, TAB_LAYOUTS);
    MkCtl(hwnd, L"BUTTON", L"Del",    BS_PUSHBUTTON, 0, 244, 160, 82, 26, ID_BTN_LAYOUT_DEL, TAB_LAYOUTS);
    MkCtl(hwnd, L"STATIC", L"Double-click a row to turn it On/Off.", 0, 0,
          14, 44 + listH, 312, 18, ID_LBL_LAYOUT_HINT, TAB_LAYOUTS);

    // ── Tab: Shortcuts (위 = 기능 콤보, 아래 = 그 기능의 단축키 목록) ──
    MkCtl(hwnd, L"STATIC", L"Function:", 0, 0, 14, 40, 312, 18, 0, TAB_SHORTCUTS);
    HWND hCmbFn = MkCtl(hwnd, L"COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_VSCROLL, 0,
                        14, 60, 312, 200, ID_CMB_SCFN, TAB_SHORTCUTS);
    for (int f = 0; f < SC_FN_COUNT; f++)
        SendMessageW(hCmbFn, CB_ADDSTRING, 0, (LPARAM)g_scFnNames[f]);
    SendMessageW(hCmbFn, CB_SETCURSEL, g_curScFn, 0);
    MkCtl(hwnd, L"LISTBOX", NULL, LBS_NOTIFY | WS_VSCROLL, WS_EX_CLIENTEDGE,
          14, 92, 224, listH - 52, ID_LST_SHORTCUTS, TAB_SHORTCUTS);
    MkCtl(hwnd, L"BUTTON", L"Add",  BS_PUSHBUTTON, 0, 244, 92, 82, 26, ID_BTN_SHORTCUT_ADD, TAB_SHORTCUTS);
    MkCtl(hwnd, L"BUTTON", L"Edit", BS_PUSHBUTTON, 0, 244, 122, 82, 26, ID_BTN_SHORTCUT_EDIT, TAB_SHORTCUTS);
    MkCtl(hwnd, L"BUTTON", L"Del",  BS_PUSHBUTTON, 0, 244, 152, 82, 26, ID_BTN_SHORTCUT_DEL, TAB_SHORTCUTS);
    MkCtl(hwnd, L"STATIC", L"Shortcuts for the selected function. Add captures a new key.", 0, 0,
          14, 44 + listH, 312, 18, ID_LBL_SHORTCUT_HINT, TAB_SHORTCUTS);

    // ── Tab: IME Options ──
    MkCtl(hwnd, L"BUTTON", L"Full-width (fullwidth Latin/symbols)", BS_AUTOCHECKBOX, 0,
          14, 46, 312, 22, ID_CHK_FULLWIDTH, TAB_OPTIONS);
    MkCtl(hwnd, L"BUTTON", L"Backspace deletes one jamo at a time", BS_AUTOCHECKBOX, 0,
          14, 74, 312, 22, ID_CHK_JAMODELETE, TAB_OPTIONS);
    MkCtl(hwnd, L"BUTTON", L"Show composition preview (floating)", BS_AUTOCHECKBOX, 0,
          14, 102, 312, 22, ID_CHK_PREVIEW, TAB_OPTIONS);
    // 라벨은 한 줄 전체를 쓰고 컨트롤은 그 아랫줄 — 좁은 열에서 라벨이 잘리던 문제 방지
    MkCtl(hwnd, L"STATIC", L"Preview font:", 0, 0, 14, 132, 312, 18, 0, TAB_OPTIONS);
    MkCtl(hwnd, L"STATIC", L"", SS_CENTERIMAGE | SS_SUNKEN, 0, 14, 152, 222, 22, ID_LBL_PVFONT, TAB_OPTIONS);
    MkCtl(hwnd, L"BUTTON", L"Set...", BS_PUSHBUTTON, 0, 244, 150, 82, 26, ID_BTN_PVFONT_SET, TAB_OPTIONS);
    MkCtl(hwnd, L"STATIC", L"Preview size (px, Auto = caret height):", 0, 0, 14, 184, 312, 18, 0, TAB_OPTIONS);
    HWND hCmbPv = MkCtl(hwnd, L"COMBOBOX", NULL, CBS_DROPDOWN | WS_VSCROLL, 0,
                        14, 204, 120, 200, ID_CMB_PVSIZE, TAB_OPTIONS);
    {   // Auto + 흔한 px 크기 (직접 입력도 가능 — 8~96 클램프)
        SendMessageW(hCmbPv, CB_ADDSTRING, 0, (LPARAM)L"Auto");
        static const wchar_t *szs[] = { L"12", L"14", L"16", L"18", L"20", L"24", L"28", L"32", L"40", L"48" };
        for (int i = 0; i < 10; i++) SendMessageW(hCmbPv, CB_ADDSTRING, 0, (LPARAM)szs[i]);
        if (g_TempConfig.options.previewFontSize <= 0) {
            SendMessageW(hCmbPv, CB_SETCURSEL, 0, 0);
        } else {
            wchar_t b[8]; swprintf(b, 8, L"%d", g_TempConfig.options.previewFontSize);
            SetWindowTextW(hCmbPv, b);
        }
    }

    // ── Tab: General (DPI, Import/Export, Reset/Revert) ──
    MkCtl(hwnd, L"STATIC", L"DPI:", SS_CENTERIMAGE, 0, 14, 46, 36, 22, 0, TAB_GENERAL);
    HWND hCmbDpi = MkCtl(hwnd, L"COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_VSCROLL, 0,
                         54, 44, 100, 200, ID_CMB_DPI, TAB_GENERAL);
    SendMessageW(hCmbDpi, CB_ADDSTRING, 0, (LPARAM)L"Auto");
    SendMessageW(hCmbDpi, CB_ADDSTRING, 0, (LPARAM)L"100%");
    SendMessageW(hCmbDpi, CB_ADDSTRING, 0, (LPARAM)L"125%");
    SendMessageW(hCmbDpi, CB_ADDSTRING, 0, (LPARAM)L"150%");
    SendMessageW(hCmbDpi, CB_ADDSTRING, 0, (LPARAM)L"200%");
    SendMessageW(hCmbDpi, CB_SETCURSEL, 0, 0);
    MkCtl(hwnd, L"BUTTON", L"Import", BS_PUSHBUTTON, 0, 14, 82, 90, 26, ID_BTN_IMPORT, TAB_GENERAL);
    MkCtl(hwnd, L"BUTTON", L"Export", BS_PUSHBUTTON, 0, 110, 82, 90, 26, ID_BTN_EXPORT, TAB_GENERAL);
    MkCtl(hwnd, L"BUTTON", L"Reset",  BS_PUSHBUTTON, 0, 14, 116, 90, 26, ID_BTN_RESET, TAB_GENERAL);
    MkCtl(hwnd, L"BUTTON", L"Revert", BS_PUSHBUTTON, 0, 110, 116, 90, 26, ID_BTN_REVERT, TAB_GENERAL);

    // ── Bottom bar (always) ── (Apply 버튼은 캡션 좌우로 ~0.1em 여유폭)
    MkCtl(hwnd, L"BUTTON", L"Apply && Save", BS_DEFPUSHBUTTON, 0, W - 198, H - 34, 106, 26, ID_BTN_APPLY, TAB_ALWAYS);
    MkCtl(hwnd, L"BUTTON", L"Cancel", BS_PUSHBUTTON, 0, W - 86, H - 34, 74, 26, ID_BTN_CANCEL, TAB_ALWAYS);

    // 옵션 컨트롤 초기 상태 반영
    SendMessageW(GetDlgItem(hwnd, ID_CHK_FULLWIDTH),  BM_SETCHECK, g_TempConfig.options.fullWidth ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(hwnd, ID_CHK_JAMODELETE), BM_SETCHECK, g_TempConfig.options.jamoDelete ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(hwnd, ID_CHK_PREVIEW),    BM_SETCHECK, g_TempConfig.options.showPreview ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(GetDlgItem(hwnd, ID_LBL_PVFONT),
                   g_TempConfig.options.previewFont[0] ? g_TempConfig.options.previewFont : L"Malgun Gothic");
    ShowTab(hwnd, g_curTab);
}

// 리사이즈 시 탭·리스트·안내·하단바 재배치 (세로 크기에 맞춰 리스트가 늘어남 → 스크롤 자동)
static void LayoutControls(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom, BB = ScaleY(40), h;
    HWND c;
    if ((c = GetDlgItem(hwnd, ID_TAB)))           MoveWindow(c, ScaleX(6), ScaleY(6), W - ScaleX(12), H - BB - ScaleY(8), TRUE);
    int listH = H - BB - ScaleY(96); if (listH < ScaleY(60)) listH = ScaleY(60);
    if ((c = GetDlgItem(hwnd, ID_LST_LAYOUTS)))   MoveWindow(c, ScaleX(14), ScaleY(40), ScaleX(224), listH, TRUE);
    int listH2 = listH - ScaleY(52); if (listH2 < ScaleY(40)) listH2 = ScaleY(40);   // 단축키 리스트(기능 콤보 아래)
    if ((c = GetDlgItem(hwnd, ID_LST_SHORTCUTS))) MoveWindow(c, ScaleX(14), ScaleY(92), ScaleX(224), listH2, TRUE);
    h = ScaleY(40) + listH + ScaleY(4);
    if ((c = GetDlgItem(hwnd, ID_LBL_LAYOUT_HINT)))   MoveWindow(c, ScaleX(14), h, ScaleX(312), ScaleY(18), TRUE);
    if ((c = GetDlgItem(hwnd, ID_LBL_SHORTCUT_HINT))) MoveWindow(c, ScaleX(14), h, ScaleX(312), ScaleY(18), TRUE);
    if ((c = GetDlgItem(hwnd, ID_BTN_APPLY)))     MoveWindow(c, W - ScaleX(198), H - ScaleY(34), ScaleX(106), ScaleY(26), TRUE);
    if ((c = GetDlgItem(hwnd, ID_BTN_CANCEL)))    MoveWindow(c, W - ScaleX(86),  H - ScaleY(34), ScaleX(74),  ScaleY(26), TRUE);
    InvalidateRect(hwnd, NULL, TRUE);
}

// 모든 자식 컨트롤에 글꼴 적용
static BOOL CALLBACK SetChildFontProc(HWND hChild, LPARAM lp) {
    SendMessageW(hChild, WM_SETFONT, (WPARAM)lp, TRUE);
    return TRUE;
}
static BOOL CALLBACK DestroyChildProc(HWND hChild, LPARAM lp) { (void)lp; DestroyWindow(hChild); return TRUE; }

// DPI 변경 시 UI 즉시 재구성: 자식 파괴 → 글꼴/컨트롤 재생성 → 목록/창크기 갱신 → DPI콤보 선택 복원
static void RebuildUI(HWND hwnd, int dpiComboSel) {
    EnumChildWindows(hwnd, DestroyChildProc, 0);
    int fontH = -MulDiv(12, g_CurrentDpi, 72);
    if (g_hUiFont) DeleteObject(g_hUiFont);
    g_hUiFont = CreateFontW(fontH, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    CreateControls(hwnd);
    EnumChildWindows(hwnd, SetChildFontProc, (LPARAM)g_hUiFont);
    EnumChildWindows(hwnd, ThemeChildProc, 0);
    RefreshLists(hwnd);
    SendMessageW(GetDlgItem(hwnd, ID_CMB_DPI), CB_SETCURSEL, dpiComboSel, 0);
    RECT rc = { 0, 0, ScaleX(WIN_W), ScaleY(g_winH) };
    AdjustWindowRectEx(&rc, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE,
                       (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hwnd, NULL, TRUE);
}

// 윈도우 프로시저
static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            InitTheme();                 // 다크/라이트 색 준비
            ApplyDarkTitleBar(hwnd);     // 제목표시줄 다크
            // DPI 초기화 (Windows 10 1607+)
            {
                UINT (WINAPI *pGetDpiForWindow)(HWND) = (void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
                if (pGetDpiForWindow && g_ManualDpiScale == 0) {
                    g_CurrentDpi = pGetDpiForWindow(hwnd);
                } else if (g_ManualDpiScale > 0) {
                    g_CurrentDpi = MulDiv(96, g_ManualDpiScale, 100);
                }
            }
            // 기본 글꼴: Segoe UI 12pt (DPI 스케일). 미설정 시 낡은 시스템 비트맵 글꼴이라 가독성/HiDPI 불량.
            {
                int fontH = -MulDiv(12, g_CurrentDpi, 72);   // 12pt → 픽셀 (음수=문자 높이)
                if (g_hUiFont) DeleteObject(g_hUiFont);
                g_hUiFont = CreateFontW(fontH, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            }
            CreateControls(hwnd);
            EnumChildWindows(hwnd, SetChildFontProc, (LPARAM)g_hUiFont);
            EnumChildWindows(hwnd, ThemeChildProc, 0);   // 컨트롤 다크/라이트 테마
            RefreshLists(hwnd);
            // 창 크기를 DPI 스케일된 컨텐츠에 맞춤. (버그: 컨트롤은 ScaleX/Y로 스케일되는데
            // 창은 560x400 고정이라 HiDPI(125%+)에서 컨트롤이 창 밖으로 넘쳐 레이아웃이 깨짐.)
            {
                RECT rc = { 0, 0, ScaleX(WIN_W), ScaleY(g_winH) };
                DWORD dwStyle   = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
                DWORD dwExStyle = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
                AdjustWindowRectEx(&rc, dwStyle, FALSE, dwExStyle);
                SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam; RECT rc; GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, g_brBg); return 1;   // 창 배경을 테마색으로 칠함
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            SetTextColor((HDC)wParam, g_clrText); SetBkColor((HDC)wParam, g_clrBg);
            return (LRESULT)(INT_PTR)g_brBg;
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLOREDIT:
            SetTextColor((HDC)wParam, g_clrText); SetBkColor((HDC)wParam, g_clrCtl);
            return (LRESULT)(INT_PTR)g_brCtl;

        case WM_SIZE:
            if (HIWORD(lParam) > 0) g_winH = MulDiv(HIWORD(lParam), 96, g_CurrentDpi);   // 논리높이 기억(DPI 재구성 대비)
            LayoutControls(hwnd);   // 세로 리사이즈 시 탭/리스트/하단바 재배치
            return 0;
        case WM_GETMINMAXINFO: {    // 최소 크기 제한
            MINMAXINFO *mmi = (MINMAXINFO*)lParam;
            mmi->ptMinTrackSize.x = ScaleX(WIN_W) + GetSystemMetrics(SM_CXSIZEFRAME) * 2;
            mmi->ptMinTrackSize.y = ScaleY(WIN_H_MIN);
            return 0;
        }
        case WM_NOTIFY:
            if (((LPNMHDR)lParam)->idFrom == ID_TAB && ((LPNMHDR)lParam)->code == TCN_SELCHANGE)
                ShowTab(hwnd, (int)SendMessageW(GetDlgItem(hwnd, ID_TAB), TCM_GETCURSEL, 0, 0));
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_CHK_FULLWIDTH:
                    g_TempConfig.options.fullWidth =
                        (SendMessageW((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    break;
                case ID_CHK_JAMODELETE:
                    g_TempConfig.options.jamoDelete =
                        (SendMessageW((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    break;
                case ID_CHK_PREVIEW:
                    g_TempConfig.options.showPreview =
                        (SendMessageW((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    break;
                case ID_CMB_PVSIZE:
                    if (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_EDITCHANGE) {
                        wchar_t b[16] = {0};
                        GetWindowTextW((HWND)lParam, b, 16);
                        if (HIWORD(wParam) == CBN_SELCHANGE) {   // 선택 직후엔 에디트가 아직 갱신 전
                            int sel = (int)SendMessageW((HWND)lParam, CB_GETCURSEL, 0, 0);
                            if (sel >= 0) SendMessageW((HWND)lParam, CB_GETLBTEXT, sel, (LPARAM)b);
                        }
                        int v = _wtoi(b);   // "Auto"/비숫자 → 0
                        g_TempConfig.options.previewFontSize = (v <= 0) ? 0 : (v < 8 ? 8 : (v > 96 ? 96 : v));
                    }
                    break;
                case ID_BTN_PVFONT_SET: {   // 미리보기 글꼴 선택 (공용 대화상자; face만 채택)
                    LOGFONTW lf = {0};
                    lf.lfHeight = -16; lf.lfCharSet = DEFAULT_CHARSET;
                    wcsncpy(lf.lfFaceName, g_TempConfig.options.previewFont, LF_FACESIZE - 1);
                    CHOOSEFONTW cf = {0};
                    cf.lStructSize = sizeof(cf);
                    cf.hwndOwner = hwnd;
                    cf.lpLogFont = &lf;
                    cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_NOSCRIPTSEL;
                    if (ChooseFontW(&cf) && lf.lfFaceName[0]) {
                        wcsncpy(g_TempConfig.options.previewFont, lf.lfFaceName, 31);
                        g_TempConfig.options.previewFont[31] = L'\0';
                        SetWindowTextW(GetDlgItem(hwnd, ID_LBL_PVFONT), g_TempConfig.options.previewFont);
                    }
                    break;
                }
                case ID_CMB_DPI:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int sel = (int)SendMessageW((HWND)lParam, CB_GETCURSEL, 0, 0);
                        static const int scales[] = { 0, 100, 125, 150, 200 };   // 콤보 순서: Auto/100/125/150/200
                        if (sel >= 0 && sel < 5) {
                            g_ManualDpiScale = scales[sel];
                            if (g_ManualDpiScale == 0) {
                                UINT (WINAPI *pGetDpi)(HWND) = (void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
                                g_CurrentDpi = pGetDpi ? (int)pGetDpi(hwnd) : 96;
                            } else {
                                g_CurrentDpi = MulDiv(96, g_ManualDpiScale, 100);
                            }
                            RebuildUI(hwnd, sel);   // 즉시 재구성
                        }
                    }
                    break;
                case ID_BTN_IMPORT: {
                    wchar_t szFile[MAX_PATH] = {0};
                    OPENFILENAMEW ofn = {0};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrFilter = L"Jamotong Config (*.ini)\0*.ini\0All Files\0*.*\0";
                    ofn.nFilterIndex = 1;
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) {
                        // 병합 로드: 파일의 순서/켜짐/단축키/옵션을 현재 temp에 이름 매칭으로 반영.
                        // (리소스 포인터는 재배열만 되고 해제/생성이 없어 누수·빈 껍데기 자판이 없음)
                        if (Config_LoadFromFile(&g_TempConfig, szFile)) {
                            RefreshLists(hwnd);
                            MessageBoxW(hwnd, L"Configuration imported successfully.", L"Success", MB_OK);
                        } else {
                            MessageBoxW(hwnd, L"Failed to import configuration.", L"Error", MB_ICONERROR);
                        }
                    }
                    break;
                }
                case ID_BTN_EXPORT: {
                    wchar_t szFile[MAX_PATH] = {0};
                    OPENFILENAMEW ofn = {0};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrFilter = L"Jamotong Config (*.ini)\0*.ini\0All Files\0*.*\0";
                    ofn.nFilterIndex = 1;
                    ofn.Flags = OFN_OVERWRITEPROMPT;
                    if (GetSaveFileNameW(&ofn)) {
                        // 기본 확장자 추가 (.ini)
                        if (!wcschr(szFile, L'.')) wcscat(szFile, L".ini");
                        if (Config_SaveToFile(&g_TempConfig, szFile)) {
                            MessageBoxW(hwnd, L"Configuration exported successfully.", L"Success", MB_OK);
                        } else {
                            MessageBoxW(hwnd, L"Failed to export configuration.", L"Error", MB_ICONERROR);
                        }
                    }
                    break;
                }
                case ID_LST_LAYOUTS:
                    if (HIWORD(wParam) == LBN_DBLCLK)
                        ToggleLayoutEnabled(hwnd, (int)SendMessageW((HWND)lParam, LB_GETCURSEL, 0, 0));
                    break;
                case ID_BTN_LAYOUT_TOGGLE:
                    ToggleLayoutEnabled(hwnd, (int)SendMessageW(GetDlgItem(hwnd, ID_LST_LAYOUTS), LB_GETCURSEL, 0, 0));
                    break;
                case ID_BTN_LAYOUT_ADD: {   // .jmt 자판 파일 불러와 목록에 추가 (켜진 상태로)
                    if (g_TempConfig.layoutCount >= 8) { MessageBoxW(hwnd, L"Maximum of 8 layouts reached.", L"Info", MB_OK); break; }
                    wchar_t szFile[MAX_PATH] = {0};
                    OPENFILENAMEW ofn = {0};
                    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
                    ofn.lpstrFile = szFile; ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrFilter = L"Jamotong Layout (*.jmt)\0*.jmt\0All Files\0*.*\0";
                    ofn.nFilterIndex = 1; ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) {
                        LayoutConfig lc; memset(&lc, 0, sizeof(lc));
                        if (Klay_Load(szFile, &lc)) {
                            lc.enabled = true;
                            g_TempConfig.layouts[g_TempConfig.layoutCount++] = lc;
                            RefreshLists(hwnd);
                            // 사용자 자판 저장소로 복사 → 재시작 후에도 자동 로드 (RFC-0004 P0-2).
                            // 이미 저장소/DLL 옆에 있는 파일이면 복사 실패(ERROR_FILE_EXISTS)여도 영속.
                            bool persisted = false;
                            wchar_t udir[MAX_PATH];
                            if (Config_UserLayoutDir(udir, MAX_PATH)) {
                                const wchar_t *base = wcsrchr(szFile, L'\\');
                                base = base ? base + 1 : szFile;
                                wchar_t dst[MAX_PATH];
                                _snwprintf(dst, MAX_PATH, L"%ls\\%ls", udir, base);
                                if (CopyFileW(szFile, dst, FALSE) || GetLastError() == ERROR_FILE_EXISTS)
                                    persisted = true;
                            }
                            if (!persisted)
                                MessageBoxW(hwnd,
                                    L"Loaded for this session, but copying to the user layout store failed.\n"
                                    L"It will disappear after sign-out. Copy the .jmt file manually to\n"
                                    L"%APPDATA%\\Jamotong\\layouts or next to jamotong.dll.",
                                    L"Warning", MB_ICONWARNING);
                        } else {
                            MessageBoxW(hwnd, L"Failed to load the layout (.jmt) file.\n"
                                              L"Check its Type/Key/Combine lines (invalid entries reject the file).",
                                        L"Error", MB_ICONERROR);
                        }
                    }
                    break;
                }
                case ID_BTN_LAYOUT_UP: {
                    HWND hLst = GetDlgItem(hwnd, ID_LST_LAYOUTS);
                    int sel = SendMessageW(hLst, LB_GETCURSEL, 0, 0);
                    if (sel > 0 && sel < g_TempConfig.layoutCount) {
                        LayoutConfig tmp = g_TempConfig.layouts[sel - 1];
                        g_TempConfig.layouts[sel - 1] = g_TempConfig.layouts[sel];
                        g_TempConfig.layouts[sel] = tmp;
                        RefreshLists(hwnd);
                        SendMessageW(hLst, LB_SETCURSEL, sel - 1, 0);
                    }
                    break;
                }
                case ID_BTN_LAYOUT_DOWN: {
                    HWND hLst = GetDlgItem(hwnd, ID_LST_LAYOUTS);
                    int sel = SendMessageW(hLst, LB_GETCURSEL, 0, 0);
                    if (sel >= 0 && sel < g_TempConfig.layoutCount - 1) {
                        LayoutConfig tmp = g_TempConfig.layouts[sel + 1];
                        g_TempConfig.layouts[sel + 1] = g_TempConfig.layouts[sel];
                        g_TempConfig.layouts[sel] = tmp;
                        RefreshLists(hwnd);
                        SendMessageW(hLst, LB_SETCURSEL, sel + 1, 0);
                    }
                    break;
                }
                case ID_BTN_LAYOUT_DEL: {
                    HWND hLst = GetDlgItem(hwnd, ID_LST_LAYOUTS);
                    int sel = SendMessageW(hLst, LB_GETCURSEL, 0, 0);
                    if (sel < 0 || g_TempConfig.layoutCount <= 1) break;   // 최소 1개는 유지
                    if (g_TempConfig.layouts[sel].enabled) {   // 마지막 enabled 삭제 금지 (RFC-0004 P0-3)
                        int on = 0;
                        for (int i = 0; i < g_TempConfig.layoutCount; i++)
                            if (g_TempConfig.layouts[i].enabled) on++;
                        if (on <= 1) {
                            MessageBoxW(hwnd, L"At least one layout must stay On.", L"Info", MB_OK);
                            break;
                        }
                    }
                    // 임시(Add 후 미적용) 자판 리소스는 내부에서 해제 후 제거 — 누수 방지
                    Config_RemoveEditedLayout(&g_TempConfig, sel, g_pRealConfig);
                    RefreshLists(hwnd);
                    break;
                }
                case ID_CMB_SCFN:   // 기능 선택 변경 → 그 기능의 단축키 목록 표시
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int sel = (int)SendMessageW((HWND)lParam, CB_GETCURSEL, 0, 0);
                        if (sel >= 0 && sel < SC_FN_COUNT) { g_curScFn = sel; RefreshLists(hwnd); }
                    }
                    break;
                case ID_BTN_SHORTCUT_ADD: {
                    ShortcutList *sl = &g_TempConfig.shortcuts[g_curScFn];
                    if (sl->count < SHORTCUTS_MAX) {
                        ShortcutKey sk;
                        if (CaptureShortcut(hwnd, &sk)) {
                            sl->keys[sl->count++] = sk;
                            RefreshLists(hwnd);
                        }
                    } else {
                        MessageBoxW(hwnd, L"Maximum of 8 shortcuts reached.", L"Info", MB_OK);
                    }
                    break;
                }
                case ID_LST_SHORTCUTS:
                    if (HIWORD(wParam) != LBN_DBLCLK) break;   // 더블클릭만 편집으로
                    // fall through
                case ID_BTN_SHORTCUT_EDIT: {
                    HWND hLst = GetDlgItem(hwnd, ID_LST_SHORTCUTS);
                    ShortcutList *sl = &g_TempConfig.shortcuts[g_curScFn];
                    int sel = SendMessageW(hLst, LB_GETCURSEL, 0, 0);
                    if (sel >= 0 && sel < sl->count) {
                        ShortcutKey sk;
                        if (CaptureShortcut(hwnd, &sk)) {
                            sl->keys[sel] = sk;
                            RefreshLists(hwnd);
                            SendMessageW(hLst, LB_SETCURSEL, sel, 0);
                        }
                    }
                    break;
                }
                case ID_BTN_SHORTCUT_DEL: {
                    HWND hLst = GetDlgItem(hwnd, ID_LST_SHORTCUTS);
                    ShortcutList *sl = &g_TempConfig.shortcuts[g_curScFn];
                    int sel = SendMessageW(hLst, LB_GETCURSEL, 0, 0);
                    if (sel < 0 || sel >= sl->count) break;
                    if (g_curScFn == SC_FN_ROTATE && sl->count <= 1) {   // 자판 전환은 최소 1개 유지
                        MessageBoxW(hwnd, L"At least one layout-switch shortcut must remain.", L"Info", MB_OK);
                        break;
                    }
                    for (int i = sel; i < sl->count - 1; i++) sl->keys[i] = sl->keys[i + 1];
                    sl->count--;
                    RefreshLists(hwnd);
                    break;
                }
                case ID_BTN_CANCEL:
                    DestroyWindow(hwnd);
                    break;
                case ID_BTN_APPLY:
                    // 트랜잭션 적용: 편집으로 떨어낸(삭제/교체) 레이아웃의 리소스를 해제한 뒤 채택
                    if (g_pRealConfig) {
                        Config_ApplyEdited(g_pRealConfig, &g_TempConfig);   // 내부에서 g_configLock
                        EnterCriticalSection(&g_configLock);   // 스냅샷/저장도 입력 스레드와 직렬화
                        g_LastSavedConfig = *g_pRealConfig;
                        // 사용자 설정 파일에 저장 → 다음 활성화/다른 프로세스·"옵션" 버튼에 반영.
                        wchar_t cfgPath[MAX_PATH];
                        if (Config_UserPath(cfgPath, MAX_PATH)) Config_SaveToFile(g_pRealConfig, cfgPath);
                        LeaveCriticalSection(&g_configLock);
                    }
                    DestroyWindow(hwnd);
                    break;
                case ID_BTN_RESET:
                    Config_DiscardEdited(&g_TempConfig, g_pRealConfig);   // temp 고유 리소스 해제 후 재로드
                    Config_LoadDefault(&g_TempConfig);
                    RefreshLists(hwnd);
                    MessageBoxW(hwnd, L"Reset to factory defaults.", L"Info", MB_OK);
                    break;
                case ID_BTN_REVERT:
                    Config_DiscardEdited(&g_TempConfig, g_pRealConfig);   // temp 고유 리소스 해제 후 복원
                    g_TempConfig = g_LastSavedConfig;
                    RefreshLists(hwnd);
                    MessageBoxW(hwnd, L"Reverted to last saved state.", L"Info", MB_OK);
                    break;
            }
            return 0;

        case WM_DESTROY:
            // 적용 없이 닫힘(취소/X): live가 소유하지 않는 temp 리소스(예: import·reset 잔여) 정리
            if (g_pRealConfig) Config_DiscardEdited(&g_TempConfig, g_pRealConfig);
            if (g_hUiFont) { DeleteObject(g_hUiFont); g_hUiFont = NULL; }
            if (g_brBg)  { DeleteObject(g_brBg);  g_brBg = NULL; }
            if (g_brCtl) { DeleteObject(g_brCtl); g_brCtl = NULL; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// 스레드 시작점
static DWORD WINAPI SettingsThreadProc(LPVOID lpParam) {
    (void)lpParam;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);   // 탭 컨트롤 클래스 등록

    // Per-Monitor V2 DPI 인식 강제 활성화 (에러 무시)
    BOOL (WINAPI *pSetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT) = 
        (void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    if (pSetProcessDpiAwarenessContext) {
        pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    WNDCLASSW wc = {0};
    wc.lpfnWndProc   = SettingsWndProc;
    wc.hInstance     = g_hInst;
    wc.lpszClassName = L"JamotongSettingsClass";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    // 트랜잭션 버퍼 복사 (입력 스레드의 자판 전환/설정 적용과 직렬화)
    if (g_pRealConfig) {
        EnterCriticalSection(&g_configLock);
        g_TempConfig = *g_pRealConfig;
        g_LastSavedConfig = *g_pRealConfig;
        LeaveCriticalSection(&g_configLock);
    }

    g_hwndSettings = CreateWindowExW(
        0, wc.lpszClassName, L"Jamotong IME Settings",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,   // 세로 리사이즈 가능(WS_THICKFRAME 유지)
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 400,
        NULL, NULL, wc.hInstance, NULL
    );

    if (!g_hwndSettings) return 0;

    ShowWindow(g_hwndSettings, SW_SHOW);
    UpdateWindow(g_hwndSettings);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_hwndSettings = NULL;
    return 0;
}

void SettingsUI_Show(JamotongConfig *pCurrentConfig) {
    if (g_hwndSettings) {
        // 이미 창이 열려있으면 최상단으로 가져옴
        SetForegroundWindow(g_hwndSettings);
        return;
    }
    if (g_hThreadSettings) {   // 이전(종료된) 설정 스레드 핸들 정리 — 재오픈마다 핸들 누수되던 것 수정
        WaitForSingleObject(g_hThreadSettings, 0);
        CloseHandle(g_hThreadSettings);
        g_hThreadSettings = NULL;
    }
    g_pRealConfig = pCurrentConfig;
    g_hThreadSettings = CreateThread(NULL, 0, SettingsThreadProc, NULL, 0, NULL);
}

void SettingsUI_Shutdown(void) {
    if (g_hwndSettings) {
        SendMessageW(g_hwndSettings, WM_CLOSE, 0, 0);
    }
    if (g_hThreadSettings) {
        WaitForSingleObject(g_hThreadSettings, 1000);
        CloseHandle(g_hThreadSettings);
        g_hThreadSettings = NULL;
    }
}
