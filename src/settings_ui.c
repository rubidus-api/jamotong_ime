#include "settings_ui.h"
#include "jamo_class.h"   // RFC-0008 W2-05
#include "jlay.h"      // 구운 자판 읽기 (Add 는 관리 앱을 불러 굽는다)
#include "seq_layout.h"   // 중국어 병음 방식인가 (Layout Options 의 문장부호 칸)
#include <commctrl.h>
#include <shellapi.h>   // ShellExecuteW — 사용자 구 파일을 메모장으로
#include <stdio.h>
#include <stdlib.h>

extern HINSTANCE g_hInst;   // 이 모듈(DLL/EXE) 핸들 — 윈도 클래스 소유자.
                            // GetModuleHandleW(NULL)=EXE 인스턴스를 쓰면 DLL WndProc과 소유자가 어긋나
                            // DLL 언로드 후 낡은 클래스가 남아 재등록 실패/댕글링 WndProc이 된다.

static HWND g_hwndSettings = NULL;
static HANDLE g_hThreadSettings = NULL;
static JamotongConfig *g_pRealConfig = NULL;

// 임시 설정 상태 (트랜잭션/Revert 용도)
static JamotongConfig g_TempConfig;

// ── RFC-0008 W1-06 남은 절반 (B8): 파일 작업은 [Apply & Save] 때만 ──────────────────────────
// Add 는 복사할 파일을 적어 두고, Import 는 번들 자판을 스테이징 폴더에 복원해 둔다. Apply 가 둘을 저장소에
// 반영하고, Cancel·창 닫기·Revert·Reset 은 버린다. (예전엔 누르는 즉시 저장소가 바뀌어 Cancel 해도 남았다.)
typedef struct { wchar_t src[MAX_PATH]; wchar_t name[128]; const wchar_t *layoutName; } PendingAdd;
static PendingAdd *g_pendingAdds = NULL;   // 자판 수에 제한이 없으니 이것도 늘어난다 (D3)
static int g_pendingAddCount = 0, g_pendingAddCap = 0;
static ConfigStagedLayouts g_stagedImport;

static bool GrowPendingAdds(void) {
    int cap = g_pendingAddCap ? g_pendingAddCap * 2 : 8;
    PendingAdd *p = (PendingAdd *)realloc(g_pendingAdds, (size_t)cap * sizeof(PendingAdd));
    if (!p) return false;
    g_pendingAdds = p;
    g_pendingAddCap = cap;
    return true;
}

static void DiscardPendingFileOps(void) {
    wchar_t stg[MAX_PATH];
    if (g_stagedImport.count > 0 && Config_StagingLayoutDir(stg, MAX_PATH))
        Config_DiscardStagedLayouts(&g_stagedImport, stg);
    g_stagedImport.count = 0;
    g_pendingAddCount = 0;
}

// Apply: 적어 둔 파일 작업을 저장소에 반영한다. 반환 = 실패 개수.
// 자판 컴파일러는 관리 앱(jamotong.exe)에 있다 — 설정창은 입력기 프로세스 안이라 텍스트 파서를
// 갖지 않는다(오너 결정 2026-09-23: 입력기는 구운 자판만 읽는다). DLL 옆의 관리 앱을 불러 굽고,
// 낸 말은 파일로 받아 그대로 보여 준다(줄·칸·코드·고치는 법이 그 안에 있다).
static bool RunLayoutBuilder(const wchar_t *args, const wchar_t *logPath, DWORD waitMs) {
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(g_hInst, exe, MAX_PATH)) return false;
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (!slash) return false;
    wcscpy(slash + 1, L"jamotong.exe");
    if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) return false;

    wchar_t cmd[MAX_PATH * 3];
    _snwprintf(cmd, MAX_PATH * 3, L"\"%ls\" %ls", exe, args);
    cmd[MAX_PATH * 3 - 1] = L'\0';

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hLog = INVALID_HANDLE_VALUE;
    if (logPath)
        hLog = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    STARTUPINFOW si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    PROCESS_INFORMATION pi = {0};
    if (hLog != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hLog;
        si.hStdError = hLog;
        si.hStdInput = NULL;
    }
    BOOL started = CreateProcessW(NULL, cmd, NULL, NULL, hLog != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW,
                                  NULL, NULL, &si, &pi);
    if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
    if (!started) return false;
    WaitForSingleObject(pi.hProcess, waitMs);
    DWORD rc = 1;
    GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return rc == 0;
}
// 로그 파일(관리 앱이 낸 진단)을 문자열로. 없으면 빈 문자열.
static void ReadLogText(const wchar_t *path, wchar_t *out, size_t cch) {
    out[0] = L'\0';
    FILE *f = _wfopen(path, L"rb");
    if (!f) return;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, out, (int)cch);
    out[cch - 1] = L'\0';
}

static int CommitPendingFileOps(void) {
    int failed = 0;
    wchar_t store[MAX_PATH];
    if (!Config_UserLayoutDir(store, MAX_PATH)) {
        failed = g_pendingAddCount + g_stagedImport.count;
        DiscardPendingFileOps();
        return failed;
    }
    for (int i = 0; i < g_pendingAddCount; i++) {
        wchar_t dst[MAX_PATH];
        _snwprintf(dst, MAX_PATH, L"%ls\\%ls", store, g_pendingAdds[i].name);
        dst[MAX_PATH - 1] = L'\0';
        if (_wcsicmp(dst, g_pendingAdds[i].src) == 0) continue;   // 이미 저장소의 그 파일
        if (!Config_CopyFileAtomic(g_pendingAdds[i].src, dst)) failed++;
    }
    g_pendingAddCount = 0;
    if (g_stagedImport.count > 0) {
        wchar_t stg[MAX_PATH];
        if (Config_StagingLayoutDir(stg, MAX_PATH))
            Config_CommitStagedLayouts(&g_stagedImport, stg, store);   // 이미 있는 이름은 덮어쓰지 않는다(의도)
        else failed += g_stagedImport.count;
    }
    g_stagedImport.count = 0;
    // 입력기는 구운 자판만 읽는다 — 저장소에 들어온 원본을 지금 굽는다 (관리 앱이 컴파일러다).
    {
        wchar_t args[MAX_PATH + 32];
        _snwprintf(args, MAX_PATH + 32, L"--build-dir \"%ls\"", store);
        args[MAX_PATH + 31] = L'\0';
        RunLayoutBuilder(args, NULL, 15000);
    }
    return failed;
}
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
enum { TAB_LAYOUTS = 0, TAB_LAYOUTOPTS = 1, TAB_SHORTCUTS = 2, TAB_OPTIONS = 3, TAB_GENERAL = 4, TAB_ALWAYS = 99 };
// Layout Options 탭 (2026-10-02, 오너: "자판별로 옵션 설정이 필요한 경우가 있어요 … 탭을 추가해 주세요").
//   선택은 자판에 붙는다(간체·번체 중국어도 서로 다른 자판이다) — 그래서 언어가 아니라 자판 이름으로 고른다.
#define ID_CMB_LOPT_LAYOUT   1050
#define ID_CHK_LOPT_SENTENCE 1051
#define ID_CHK_LOPT_SUGGEST  1052
#define ID_LBL_LOPT_HINT     1053
#define ID_CHK_LOPT_PUNCT    1054
#define ID_CHK_LOPT_FUZZY    1055
#define ID_CMB_LOPT_KEYS     1056
#define ID_BTN_LOPT_PHRASES  1057
#define ID_LBL_LOPT_KEYS     1058
#define ID_CHK_LOPT_EMOJI    1059
#define ID_CHK_LOPT_TONES    1060
#define ID_CHK_LOPT_BAR      1061
static int g_loptSel = 0;   // 고른 자판 (g_TempConfig.layouts 의 번호)
static int g_curTab = 0;

// Shortcuts Group — 위 콤보로 기능을 고르고 아래 리스트에서 그 기능의 단축키를 추가/삭제
#define ID_LST_SHORTCUTS    1012
#define ID_BTN_SHORTCUT_ADD 1013
#define ID_BTN_SHORTCUT_DEL 1014
#define ID_BTN_SHORTCUT_EDIT 1015
#define ID_CMB_SCFN         1044   // 기능 선택 콤보 (ShortcutFn 순서)
#define ID_LBL_CANDFONT      1046   // 한자 후보창 글꼴 표시
#define ID_BTN_CANDFONT_SET  1047   // 한자 후보창 글꼴 선택(ChooseFont)
#define ID_CMB_CANDSIZE      1048   // 한자 후보창 글꼴 크기 (12~72 클램프, 직접 입력 가능)
#define ID_CHK_INLINE        1049   // 문서 인라인 조합 (RFC-0010, 끄면 확정 전용 + 미리보기)

// 기능 콤보 표시 이름 (ShortcutFn 인덱스와 동일 순서)
static const wchar_t *g_scFnNames[SC_FN_COUNT] = {
    L"Layout switch (Korean/English toggle)",
    L"Hanja / special character conversion",
    L"Unicode code point input",
    L"Open IME settings (this window)",
    L"Pass-through (direct input) mode toggle",
};
static int g_curScFn = 0;   // 현재 선택된 기능 (DPI 재구성에도 유지)

static HFONT g_hUiFont = NULL;   // Segoe UI 12pt (DPI 스케일) — 모든 컨트롤에 적용

// 헬퍼: 픽셀 스케일링 (100% = 96 DPI 기준)
static int ScaleX(int x) { return MulDiv(x, g_CurrentDpi, 96); }
static int ScaleY(int y) { return MulDiv(y, g_CurrentDpi, 96); }

// ── 테마(다크/라이트) ─────────────────────────────────────────────────────
//   팔레트를 커스텀 값으로 통일(라이트에서 창=COLOR_BTNFACE·컨트롤=COLOR_WINDOW를 섞어 쓰던
//   미묘한 배경색 차이 제거). g_clrBg=창·라벨·버튼 공통 배경, g_clrCtl=입력 컨트롤(리스트·
//   에디트·콤보) 배경, g_clrLine=구분 테두리.
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
    if (g_dark) {
        g_clrBg   = RGB(32, 32, 32);     // 창·라벨 공통 배경
        g_clrText = RGB(235, 235, 235);
        g_clrCtl  = RGB(45, 45, 45);     // 입력 컨트롤
    } else {
        g_clrBg   = RGB(243, 243, 243);  // Win11 톤 — 창·라벨이 하나의 배경색
        g_clrText = RGB(26, 26, 26);
        g_clrCtl  = RGB(255, 255, 255);  // 입력 컨트롤은 흰색으로 대비
    }
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
    (void)s_reg;
    {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = CaptureProc; wc.hInstance = g_hInst;
        wc.lpszClassName = L"JamotongCaptureClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        Jamo_EnsureClass(&wc);   // W2-05
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

// 자판 목록 한 줄의 글 (화면 읽기 도구가 읽는다 — 그리는 것은 DrawLayoutItem)
static void LayoutItemText(int i, wchar_t *buf, int cap) {
    swprintf(buf, cap, L"%ls: %ls", g_TempConfig.layouts[i].enabled ? L"On" : L"Off",
             g_TempConfig.layouts[i].name ? g_TempConfig.layouts[i].name : L"?");
}

// 자판 목록 한 줄을 그린다 (2026-10-03 오너: 고른 줄이 반전되면 ◼/◻ 글자도 반전되어 켜짐·꺼짐이 헷갈렸다).
//   왼쪽에 체크 상자를 그리되 상자는 줄의 선택 색과 상관없이 늘 같은 모양이다: 켜짐 = 파란 상자에 흰 ✓, 꺼짐 = 빈 상자.
//   꺼진 자판은 이름도 흐리게.
static void DrawLayoutItem(const DRAWITEMSTRUCT *d) {
    if ((int)d->itemID < 0) return;
    HDC hdc = d->hDC;
    RECT rc = d->rcItem;
    bool sel = (d->itemState & ODS_SELECTED) != 0;
    int i = (int)d->itemID;
    bool on = i < g_TempConfig.layoutCount && g_TempConfig.layouts[i].enabled;
    HBRUSH bk = CreateSolidBrush(sel ? GetSysColor(COLOR_HIGHLIGHT) : g_clrCtl);
    FillRect(hdc, &rc, bk);
    DeleteObject(bk);
    int box = ScaleY(14), pad = ScaleX(4);
    RECT b = { rc.left + pad, rc.top + (rc.bottom - rc.top - box) / 2, 0, 0 };
    b.right = b.left + box; b.bottom = b.top + box;
    if (sel) {   // 선택 색 위에서도 상자 테두리가 보이게 흰 테두리 한 겹
        RECT o = { b.left - 1, b.top - 1, b.right + 1, b.bottom + 1 };
        HBRUSH w = CreateSolidBrush(RGB(255, 255, 255)); FrameRect(hdc, &o, w); DeleteObject(w);
    }
    const COLORREF accent = g_dark ? RGB(76, 194, 255) : RGB(0, 95, 184);
    HBRUSH fill = CreateSolidBrush(on ? accent : g_clrCtl);
    FillRect(hdc, &b, fill);
    DeleteObject(fill);
    HBRUSH edge = CreateSolidBrush(on ? accent : (g_dark ? RGB(160, 160, 160) : RGB(110, 110, 110)));
    FrameRect(hdc, &b, edge);
    DeleteObject(edge);
    if (on) {   // ✓
        HPEN pen = CreatePen(PS_SOLID, ScaleX(2) > 1 ? ScaleX(2) : 2, g_dark ? RGB(0, 0, 0) : RGB(255, 255, 255));
        HGDIOBJ op = SelectObject(hdc, pen);
        POINT pt[3] = { { b.left + box * 22 / 100, b.top + box * 52 / 100 },
                        { b.left + box * 42 / 100, b.top + box * 72 / 100 },
                        { b.left + box * 78 / 100, b.top + box * 30 / 100 } };
        Polyline(hdc, pt, 3);
        SelectObject(hdc, op);
        DeleteObject(pen);
    }
    RECT t = rc;
    t.left = b.right + ScaleX(8);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, sel ? GetSysColor(COLOR_HIGHLIGHTTEXT) : (on ? g_clrText : RGB(128, 128, 128)));
    const wchar_t *name = (i < g_TempConfig.layoutCount && g_TempConfig.layouts[i].name) ? g_TempConfig.layouts[i].name : L"?";
    DrawTextW(hdc, name, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (d->itemState & ODS_FOCUS) DrawFocusRect(hdc, &rc);
}

static void RefreshLists(HWND hwnd) {
    HWND hLstLayouts = GetDlgItem(hwnd, ID_LST_LAYOUTS);
    HWND hLstShortcuts = GetDlgItem(hwnd, ID_LST_SHORTCUTS);
    
    int top = (int)SendMessageW(hLstLayouts, LB_GETTOPINDEX, 0, 0);   // 다시 채워도 보던 자리를 지킨다
    SendMessageW(hLstLayouts, WM_SETREDRAW, FALSE, 0);
    SendMessageW(hLstLayouts, LB_RESETCONTENT, 0, 0);
    SendMessageW(hLstShortcuts, LB_RESETCONTENT, 0, 0);
    
    // 켜짐/꺼짐은 목록이 직접 그리는 체크 상자로 보인다(DrawLayoutItem). 글자는 화면 읽기 도구를 위한 것이다.
    for (int i = 0; i < g_TempConfig.layoutCount; i++) {
        wchar_t buf[96];
        LayoutItemText(i, buf, 96);
        SendMessageW(hLstLayouts, LB_ADDSTRING, 0, (LPARAM)buf);
    }
    if (top > 0) SendMessageW(hLstLayouts, LB_SETTOPINDEX, (WPARAM)top, 0);
    SendMessageW(hLstLayouts, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hLstLayouts, NULL, TRUE);
    
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
    // 그 줄만 고친다 — 목록을 통째로 다시 채우면 더블클릭 중에 스크롤 자리를 잃고 다른 줄이 제대로 안 그려졌다 (2026-10-03).
    HWND lst = GetDlgItem(hwnd, ID_LST_LAYOUTS);
    wchar_t buf[96];
    LayoutItemText(sel, buf, 96);
    int top = (int)SendMessageW(lst, LB_GETTOPINDEX, 0, 0);
    SendMessageW(lst, WM_SETREDRAW, FALSE, 0);
    SendMessageW(lst, LB_DELETESTRING, (WPARAM)sel, 0);
    SendMessageW(lst, LB_INSERTSTRING, (WPARAM)sel, (LPARAM)buf);
    SendMessageW(lst, LB_SETCURSEL, (WPARAM)sel, 0);
    SendMessageW(lst, LB_SETTOPINDEX, (WPARAM)top, 0);
    SendMessageW(lst, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lst, NULL, TRUE);
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
// Layout Options 탭을 지금 g_TempConfig 로 채운다 (자판 목록은 Layouts 탭에서 바뀔 수 있으므로 탭을 열 때마다).
static void LoptRefresh(HWND hwnd, bool refill) {
    HWND cmb = GetDlgItem(hwnd, ID_CMB_LOPT_LAYOUT);
    if (!cmb) return;
    if (refill) {
        SendMessageW(cmb, CB_RESETCONTENT, 0, 0);
        for (int i = 0; i < g_TempConfig.layoutCount; i++)
            SendMessageW(cmb, CB_ADDSTRING, 0, (LPARAM)(g_TempConfig.layouts[i].name ? g_TempConfig.layouts[i].name : L"?"));
        if (g_loptSel >= g_TempConfig.layoutCount) g_loptSel = 0;
        SendMessageW(cmb, CB_SETCURSEL, (WPARAM)g_loptSel, 0);
    }
    const LayoutConfig *L = (g_loptSel >= 0 && g_loptSel < g_TempConfig.layoutCount) ? &g_TempConfig.layouts[g_loptSel] : NULL;
    bool seq = L && L->type == LAYOUT_TYPE_SEQUENCE;
    HWND s = GetDlgItem(hwnd, ID_CHK_LOPT_SENTENCE), g = GetDlgItem(hwnd, ID_CHK_LOPT_SUGGEST);
    EnableWindow(s, seq); EnableWindow(g, seq);
    SendMessageW(s, BM_SETCHECK, (seq && !L->optNoSentence) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g, BM_SETCHECK, (seq && !L->optNoSuggest) ? BST_CHECKED : BST_UNCHECKED, 0);
    bool zh = seq && L->pSeqLayout && ((const SeqLayout *)L->pSeqLayout)->zh;
    bool ja = seq && L->pSeqLayout && ((const SeqLayout *)L->pSeqLayout)->ja;   // 일본어 방식 (0.70.0)
    HWND pc = GetDlgItem(hwnd, ID_CHK_LOPT_PUNCT);
    SetWindowTextW(pc, ja ? L"Japanese punctuation (\x3001\x3002\x300C\x300D\x30FB\x301C\xFF01\xFF1F)"
                          : L"Chinese punctuation (\xFF0C\x3002\xFF1F\xFF01\x201C\x201D \x2014 \x300C\x300D for traditional)");
    EnableWindow(pc, zh || ja);
    SendMessageW(pc, BM_SETCHECK, ((zh || ja) && !L->optNoPunct) ? BST_CHECKED : BST_UNCHECKED, 0);
    HWND fz = GetDlgItem(hwnd, ID_CHK_LOPT_FUZZY);
    EnableWindow(fz, zh);
    SendMessageW(fz, BM_SETCHECK, (zh && L->optFuzzy) ? BST_CHECKED : BST_UNCHECKED, 0);
    HWND em = GetDlgItem(hwnd, ID_CHK_LOPT_EMOJI), tn = GetDlgItem(hwnd, ID_CHK_LOPT_TONES);
    EnableWindow(em, zh);
    SendMessageW(em, BM_SETCHECK, (zh && !L->optNoEmoji) ? BST_CHECKED : BST_UNCHECKED, 0);
    bool hasTones = zh && L->pSeqLayout && ((const SeqLayout *)L->pSeqLayout)->toneFile[0];   // 쓸 때 열기 — 이름으로 본다
    EnableWindow(tn, hasTones);
    SendMessageW(tn, BM_SETCHECK, (hasTones && !L->optNoTones) ? BST_CHECKED : BST_UNCHECKED, 0);
    HWND bar = GetDlgItem(hwnd, ID_CHK_LOPT_BAR);
    EnableWindow(bar, zh || ja);
    SendMessageW(bar, BM_SETCHECK, ((zh || ja) && L->optBar) ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(GetDlgItem(hwnd, ID_BTN_LOPT_PHRASES), zh || ja);
    {   // 글쇠: 온 병음 + 이 자판이 가진 쌍병 표
        HWND kc = GetDlgItem(hwnd, ID_CMB_LOPT_KEYS);
        SendMessageW(kc, CB_RESETCONTENT, 0, 0);
        SendMessageW(kc, CB_ADDSTRING, 0, (LPARAM)L"Full pinyin");
        const SeqLayout *sl = zh ? (const SeqLayout *)L->pSeqLayout : NULL;
        int sel = 0;
        for (int i = 0; sl && i < sl->nScheme; i++) {
            const wchar_t *nm = sl->schemeName[i];
            const wchar_t *shown = !wcscmp(nm, L"xiaohe") ? L"Xiaohe (\x5C0F\x9E64\x53CC\x62FC)"
                                 : !wcscmp(nm, L"ziranma") ? L"Ziranma (\x81EA\x7136\x7801)"
                                 : !wcscmp(nm, L"microsoft") ? L"Microsoft (\x5FAE\x8F6F\x53CC\x62FC)" : nm;
            SendMessageW(kc, CB_ADDSTRING, 0, (LPARAM)shown);
            if (!wcscmp(nm, L->optKeys)) sel = i + 1;
        }
        SendMessageW(kc, CB_SETCURSEL, (WPARAM)sel, 0);
        EnableWindow(kc, sl && sl->nScheme > 0);   // 표 이름은 자판 파일에서 읽었다 (열지 않아도 안다)
    }
    SetWindowTextW(GetDlgItem(hwnd, ID_LBL_LOPT_HINT), seq
        ? (zh ? L"For this layout only. Keys: full pinyin or double pinyin (two keys per syllable). Custom phrases: one per line, letters then text (dz \x5317\x4EAC\x5E02)."
           : ja ? L"For this layout only. Space converts, F6-F10 give hiragana, katakana, half-width katakana and romaji. Custom phrases: one per line, reading then text (\x3088\x308D \x3088\x308D\x3057\x304F\x304A\x9858\x3044\x3057\x307E\x3059)."
              : L"Applies to this layout only. The sentence candidate converts all you typed at once; suggestions offer longer words and idioms that start with it, and initials such as wsm for a whole word.")
        : L"This layout has no options of its own. Options appear here for input layouts that convert a reading (Chinese pinyin, Japanese kana).");
}

static void ShowTab(HWND hwnd, int sel) {
    g_curTab = sel;
    if (sel == TAB_LAYOUTOPTS) LoptRefresh(hwnd, true);
    // 탭 머리도 같은 탭을 가리키게 한다 — 창을 다시 만들면 탭 컨트롤은 0(Layouts)으로 시작하는데
    // 내용은 기억한 탭을 보여 줘 머리와 내용이 어긋났다 (BACKLOGS B9). 같은 값이면 알림도 없다.
    HWND tab = GetDlgItem(hwnd, ID_TAB);
    if (tab && (int)SendMessageW(tab, TCM_GETCURSEL, 0, 0) != sel) SendMessageW(tab, TCM_SETCURSEL, (WPARAM)sel, 0);
    for (HWND h = GetWindow(hwnd, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
        LONG_PTR t = GetWindowLongPtrW(h, GWLP_USERDATA);
        ShowWindow(h, (t == sel || t == TAB_ALWAYS) ? SW_SHOW : SW_HIDE);
    }
}

// 창 논리 크기 (세로는 리사이즈로 늘어남)
#define WIN_W 460
#define WIN_H_MIN 466
static int g_winH = WIN_H_MIN;   // 현재 논리 높이

// UI 생성 — 탭 5개(Layouts/Layout Options/Shortcuts/IME Options/General) + 하단 Apply/Cancel(항상 표시)
static void CreateControls(HWND hwnd) {
    const int W = WIN_W, H = g_winH, BB = 40;   // BB=하단 바 높이
    const int listH = H - BB - 96;              // 리스트 높이(세로 리사이즈 반영)

    // 탭 컨트롤 (항상 표시)
    HWND tab = MkCtl(hwnd, L"SysTabControl32", NULL, 0, 0, 6, 6, W - 12, H - BB - 8, ID_TAB, TAB_ALWAYS);
    TCITEMW ti = {0}; ti.mask = TCIF_TEXT;
    ti.pszText = (LPWSTR)L"Layouts";        SendMessageW(tab, TCM_INSERTITEMW, TAB_LAYOUTS, (LPARAM)&ti);
    ti.pszText = (LPWSTR)L"Layout Options"; SendMessageW(tab, TCM_INSERTITEMW, TAB_LAYOUTOPTS, (LPARAM)&ti);
    ti.pszText = (LPWSTR)L"Shortcuts";      SendMessageW(tab, TCM_INSERTITEMW, TAB_SHORTCUTS, (LPARAM)&ti);
    ti.pszText = (LPWSTR)L"IME Options";    SendMessageW(tab, TCM_INSERTITEMW, TAB_OPTIONS, (LPARAM)&ti);
    ti.pszText = (LPWSTR)L"General";        SendMessageW(tab, TCM_INSERTITEMW, TAB_GENERAL, (LPARAM)&ti);

    // ── Tab: Layout Options ── 자판을 고르고 그 자판의 선택을 켜고 끈다
    MkCtl(hwnd, L"STATIC", L"Layout:", 0, 0, 14, 40, (WIN_W - 28), 18, 0, TAB_LAYOUTOPTS);
    MkCtl(hwnd, L"COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_VSCROLL, 0, 14, 60, (WIN_W - 28), 240, ID_CMB_LOPT_LAYOUT, TAB_LAYOUTOPTS);
    MkCtl(hwnd, L"BUTTON", L"Offer the whole sentence first", BS_AUTOCHECKBOX, 0,
          14, 94, (WIN_W - 28), 22, ID_CHK_LOPT_SENTENCE, TAB_LAYOUTOPTS);
    MkCtl(hwnd, L"BUTTON", L"Suggest words and idioms (completions, initials)", BS_AUTOCHECKBOX, 0,
          14, 118, (WIN_W - 28), 22, ID_CHK_LOPT_SUGGEST, TAB_LAYOUTOPTS);
    MkCtl(hwnd, L"BUTTON", L"Chinese punctuation (\xFF0C\x3002\xFF1F\xFF01\x201C\x201D \x2014 \x300C\x300D for traditional)", BS_AUTOCHECKBOX, 0,
          14, 142, (WIN_W - 28), 22, ID_CHK_LOPT_PUNCT, TAB_LAYOUTOPTS);
    MkCtl(hwnd, L"BUTTON", L"Fuzzy pinyin (z=zh, n=l, an=ang, in=ing ...)", BS_AUTOCHECKBOX, 0,
          14, 166, (WIN_W - 28), 22, ID_CHK_LOPT_FUZZY, TAB_LAYOUTOPTS);
    MkCtl(hwnd, L"BUTTON", L"Emoji and symbols among the candidates", BS_AUTOCHECKBOX, 0,
          14, 190, (WIN_W - 28), 22, ID_CHK_LOPT_EMOJI, TAB_LAYOUTOPTS);
    MkCtl(hwnd, L"BUTTON", L"Show pinyin with tones beside the candidates", BS_AUTOCHECKBOX, 0,
          14, 214, (WIN_W - 28), 22, ID_CHK_LOPT_TONES, TAB_LAYOUTOPTS);
    MkCtl(hwnd, L"BUTTON", L"Horizontal candidate bar", BS_AUTOCHECKBOX, 0,
          14, 238, (WIN_W - 28), 22, ID_CHK_LOPT_BAR, TAB_LAYOUTOPTS);
    MkCtl(hwnd, L"STATIC", L"Keys:", SS_CENTERIMAGE, 0, 14, 266, 44, 24, ID_LBL_LOPT_KEYS, TAB_LAYOUTOPTS);
    MkCtl(hwnd, L"COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_VSCROLL, 0, 62, 266, 220, 200, ID_CMB_LOPT_KEYS, TAB_LAYOUTOPTS);
    MkCtl(hwnd, L"BUTTON", L"Edit custom phrases...", BS_PUSHBUTTON, 0, 14, 298, 180, 26, ID_BTN_LOPT_PHRASES, TAB_LAYOUTOPTS);
    MkCtl(hwnd, L"STATIC", L"", 0, 0, 14, 330, (WIN_W - 28), 72, ID_LBL_LOPT_HINT, TAB_LAYOUTOPTS);

    // ── Tab: Layouts ──
    MkCtl(hwnd, L"LISTBOX", NULL, LBS_NOTIFY | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT, WS_EX_CLIENTEDGE,
          14, 40, (WIN_W - 116), listH, ID_LST_LAYOUTS, TAB_LAYOUTS);   // 줄은 DrawLayoutItem 이 그린다 (체크 상자)
    MkCtl(hwnd, L"BUTTON", L"On/Off", BS_PUSHBUTTON, 0, (WIN_W - 96), 40, 82, 26, ID_BTN_LAYOUT_TOGGLE, TAB_LAYOUTS);
    MkCtl(hwnd, L"BUTTON", L"\x25B2", BS_PUSHBUTTON, 0, (WIN_W - 96), 70, 82, 26, ID_BTN_LAYOUT_UP, TAB_LAYOUTS);
    MkCtl(hwnd, L"BUTTON", L"\x25BC", BS_PUSHBUTTON, 0, (WIN_W - 96), 100, 82, 26, ID_BTN_LAYOUT_DOWN, TAB_LAYOUTS);
    MkCtl(hwnd, L"BUTTON", L"Add",    BS_PUSHBUTTON, 0, (WIN_W - 96), 130, 82, 26, ID_BTN_LAYOUT_ADD, TAB_LAYOUTS);
    MkCtl(hwnd, L"BUTTON", L"Del",    BS_PUSHBUTTON, 0, (WIN_W - 96), 160, 82, 26, ID_BTN_LAYOUT_DEL, TAB_LAYOUTS);
    MkCtl(hwnd, L"STATIC", L"Double-click a row to turn it On/Off.", 0, 0,
          14, 44 + listH, (WIN_W - 28), 18, ID_LBL_LAYOUT_HINT, TAB_LAYOUTS);

    // ── Tab: Shortcuts (위 = 기능 콤보, 아래 = 그 기능의 단축키 목록) ──
    MkCtl(hwnd, L"STATIC", L"Function:", 0, 0, 14, 40, (WIN_W - 28), 18, 0, TAB_SHORTCUTS);
    HWND hCmbFn = MkCtl(hwnd, L"COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_VSCROLL, 0,
                        14, 60, (WIN_W - 28), 200, ID_CMB_SCFN, TAB_SHORTCUTS);
    for (int f = 0; f < SC_FN_COUNT; f++)
        SendMessageW(hCmbFn, CB_ADDSTRING, 0, (LPARAM)g_scFnNames[f]);
    SendMessageW(hCmbFn, CB_SETCURSEL, g_curScFn, 0);
    MkCtl(hwnd, L"LISTBOX", NULL, LBS_NOTIFY | WS_VSCROLL, WS_EX_CLIENTEDGE,
          14, 92, (WIN_W - 116), listH - 52, ID_LST_SHORTCUTS, TAB_SHORTCUTS);
    MkCtl(hwnd, L"BUTTON", L"Add",  BS_PUSHBUTTON, 0, (WIN_W - 96), 92, 82, 26, ID_BTN_SHORTCUT_ADD, TAB_SHORTCUTS);
    MkCtl(hwnd, L"BUTTON", L"Edit", BS_PUSHBUTTON, 0, (WIN_W - 96), 122, 82, 26, ID_BTN_SHORTCUT_EDIT, TAB_SHORTCUTS);
    MkCtl(hwnd, L"BUTTON", L"Del",  BS_PUSHBUTTON, 0, (WIN_W - 96), 152, 82, 26, ID_BTN_SHORTCUT_DEL, TAB_SHORTCUTS);
    MkCtl(hwnd, L"STATIC", L"Shortcuts for the selected function. Add captures a new key.", 0, 0,
          14, 44 + listH, (WIN_W - 28), 18, ID_LBL_SHORTCUT_HINT, TAB_SHORTCUTS);

    // ── Tab: IME Options ──
    MkCtl(hwnd, L"BUTTON", L"Full-width (fullwidth Latin/symbols)", BS_AUTOCHECKBOX, 0,
          14, 46, (WIN_W - 28), 22, ID_CHK_FULLWIDTH, TAB_OPTIONS);
    MkCtl(hwnd, L"BUTTON", L"Backspace deletes one jamo at a time", BS_AUTOCHECKBOX, 0,
          14, 74, (WIN_W - 28), 22, ID_CHK_JAMODELETE, TAB_OPTIONS);
    MkCtl(hwnd, L"BUTTON", L"Show composition preview (floating)", BS_AUTOCHECKBOX, 0,
          14, 102, (WIN_W - 28), 22, ID_CHK_PREVIEW, TAB_OPTIONS);
    MkCtl(hwnd, L"BUTTON", L"Inline composition (in the document)", BS_AUTOCHECKBOX, 0,
          14, 130, (WIN_W - 28), 22, ID_CHK_INLINE, TAB_OPTIONS);
    // 라벨은 한 줄 전체를 쓰고 컨트롤은 그 아랫줄 — 좁은 열에서 라벨이 잘리던 문제 방지
    MkCtl(hwnd, L"STATIC", L"Preview font:", 0, 0, 14, 160, (WIN_W - 28), 18, 0, TAB_OPTIONS);
    MkCtl(hwnd, L"STATIC", L"", SS_CENTERIMAGE | SS_SUNKEN, 0, 14, 180, (WIN_W - 118), 22, ID_LBL_PVFONT, TAB_OPTIONS);
    MkCtl(hwnd, L"BUTTON", L"Set...", BS_PUSHBUTTON, 0, (WIN_W - 96), 178, 82, 26, ID_BTN_PVFONT_SET, TAB_OPTIONS);
    MkCtl(hwnd, L"STATIC", L"Preview size (px, Auto = caret height):", 0, 0, 14, 212, (WIN_W - 28), 18, 0, TAB_OPTIONS);
    HWND hCmbPv = MkCtl(hwnd, L"COMBOBOX", NULL, CBS_DROPDOWN | WS_VSCROLL, 0,
                        14, 232, 120, 200, ID_CMB_PVSIZE, TAB_OPTIONS);
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
    // 한자 후보창 글꼴/크기 — 후보·훈음·페이지 표시 전 요소가 이 하나를 쓴다 (candidate_ui.c)
    MkCtl(hwnd, L"STATIC", L"Hanja candidate font:", 0, 0, 14, 264, (WIN_W - 28), 18, 0, TAB_OPTIONS);
    MkCtl(hwnd, L"STATIC", L"", SS_CENTERIMAGE | SS_SUNKEN, 0, 14, 284, (WIN_W - 118), 22, ID_LBL_CANDFONT, TAB_OPTIONS);
    MkCtl(hwnd, L"BUTTON", L"Set...", BS_PUSHBUTTON, 0, (WIN_W - 96), 282, 82, 26, ID_BTN_CANDFONT_SET, TAB_OPTIONS);
    MkCtl(hwnd, L"STATIC", L"Hanja candidate size (px):", 0, 0, 14, 316, (WIN_W - 28), 18, 0, TAB_OPTIONS);
    HWND hCmbCand = MkCtl(hwnd, L"COMBOBOX", NULL, CBS_DROPDOWN | WS_VSCROLL, 0,
                          14, 336, 120, 200, ID_CMB_CANDSIZE, TAB_OPTIONS);
    {   // 흔한 px 크기 (직접 입력도 가능 — 12~72 클램프)
        static const wchar_t *csz[] = { L"16", L"18", L"20", L"24", L"28", L"32", L"40", L"48" };
        for (int i = 0; i < 8; i++) SendMessageW(hCmbCand, CB_ADDSTRING, 0, (LPARAM)csz[i]);
        wchar_t b[8]; swprintf(b, 8, L"%d", g_TempConfig.options.candFontSize);
        SetWindowTextW(hCmbCand, b);
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
    SendMessageW(GetDlgItem(hwnd, ID_CHK_INLINE),     BM_SETCHECK, g_TempConfig.options.inlineComposition ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(GetDlgItem(hwnd, ID_LBL_PVFONT),
                   g_TempConfig.options.previewFont[0] ? g_TempConfig.options.previewFont : L"Malgun Gothic");
    SetWindowTextW(GetDlgItem(hwnd, ID_LBL_CANDFONT),
                   g_TempConfig.options.candFont[0] ? g_TempConfig.options.candFont : L"Malgun Gothic");
    ShowTab(hwnd, g_curTab);
}

// 리사이즈 시 탭·리스트·안내·하단바 재배치 (세로 크기에 맞춰 리스트가 늘어남 → 스크롤 자동)
static void LayoutControls(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom, BB = ScaleY(40), h;
    HWND c;
    if ((c = GetDlgItem(hwnd, ID_TAB)))           MoveWindow(c, ScaleX(6), ScaleY(6), W - ScaleX(12), H - BB - ScaleY(8), TRUE);
    int listH = H - BB - ScaleY(96); if (listH < ScaleY(60)) listH = ScaleY(60);
    if ((c = GetDlgItem(hwnd, ID_LST_LAYOUTS)))   MoveWindow(c, ScaleX(14), ScaleY(40), ScaleX((WIN_W - 116)), listH, TRUE);
    int listH2 = listH - ScaleY(52); if (listH2 < ScaleY(40)) listH2 = ScaleY(40);   // 단축키 리스트(기능 콤보 아래)
    if ((c = GetDlgItem(hwnd, ID_LST_SHORTCUTS))) MoveWindow(c, ScaleX(14), ScaleY(92), ScaleX((WIN_W - 116)), listH2, TRUE);
    h = ScaleY(40) + listH + ScaleY(4);
    if ((c = GetDlgItem(hwnd, ID_LBL_LAYOUT_HINT)))   MoveWindow(c, ScaleX(14), h, ScaleX((WIN_W - 28)), ScaleY(18), TRUE);
    if ((c = GetDlgItem(hwnd, ID_LBL_SHORTCUT_HINT))) MoveWindow(c, ScaleX(14), h, ScaleX((WIN_W - 28)), ScaleY(18), TRUE);
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

        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT *m = (MEASUREITEMSTRUCT *)lParam;
            if (m && m->CtlID == ID_LST_LAYOUTS) { m->itemHeight = (UINT)ScaleY(22); return TRUE; }
            break;
        }
        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT *d = (const DRAWITEMSTRUCT *)lParam;
            if (d && d->CtlID == ID_LST_LAYOUTS) { DrawLayoutItem(d); return TRUE; }
            break;
        }
        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam; RECT rc; GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, g_brBg); return 1;   // 창 배경을 테마색으로 칠함
        }
        case WM_CTLCOLORSTATIC: {
            SetTextColor((HDC)wParam, g_clrText);
            // SS_SUNKEN 표시 라벨(글꼴 이름)은 읽기 전용 입력 필드처럼 보이므로 불투명 컨트롤 배경.
            if (GetDlgCtrlID((HWND)lParam) == ID_LBL_PVFONT ||
                GetDlgCtrlID((HWND)lParam) == ID_LBL_CANDFONT) {
                SetBkColor((HDC)wParam, g_clrCtl);
                return (LRESULT)(INT_PTR)g_brCtl;
            }
            // 나머지 라벨·체크박스: 창 배경색(g_clrBg)과 '정확히 같은' 불투명 브러시를 돌려준다.
            // 투명(NULL_BRUSH) 방식은 테마 체크박스가 자기 배경을 지우지 않아 회색 잔상이
            // 살짝 비쳤다(실기 2026-07-24). 창 전체가 g_brBg 단일색이므로 같은 브러시면 seam이 없다.
            SetBkColor((HDC)wParam, g_clrBg);
            return (LRESULT)(INT_PTR)g_brBg;
        }
        case WM_CTLCOLORBTN:   // 체크박스/버튼 텍스트 배경 = 창 배경색과 동일(위와 같은 이유)
            SetTextColor((HDC)wParam, g_clrText);
            SetBkColor((HDC)wParam, g_clrBg);
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
                case ID_CMB_LOPT_LAYOUT:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int sel = (int)SendMessageW((HWND)lParam, CB_GETCURSEL, 0, 0);
                        if (sel >= 0) { g_loptSel = sel; LoptRefresh(hwnd, false); }
                    }
                    break;
                case ID_CMB_LOPT_KEYS:
                    if (HIWORD(wParam) == CBN_SELCHANGE && g_loptSel >= 0 && g_loptSel < g_TempConfig.layoutCount) {
                        int k = (int)SendMessageW((HWND)lParam, CB_GETCURSEL, 0, 0);
                        LayoutConfig *L = &g_TempConfig.layouts[g_loptSel];
                        const SeqLayout *sl = (const SeqLayout *)L->pSeqLayout;
                        if (k <= 0 || !sl || k > sl->nScheme) L->optKeys[0] = L'\0';
                        else lstrcpynW(L->optKeys, sl->schemeName[k - 1], 16);
                    }
                    break;
                case ID_BTN_LOPT_PHRASES: {   // 사용자 구 파일을 메모장으로 (없으면 보기 줄을 넣어 만든다)
                    wchar_t dir[MAX_PATH], path[MAX_PATH];
                    if (!Config_UserDictDir(dir, MAX_PATH)) break;
                    const LayoutConfig *PL = (g_loptSel >= 0 && g_loptSel < g_TempConfig.layoutCount) ? &g_TempConfig.layouts[g_loptSel] : NULL;
                    bool jaFile = PL && PL->type == LAYOUT_TYPE_SEQUENCE && PL->pSeqLayout && ((const SeqLayout *)PL->pSeqLayout)->ja;
                    _snwprintf(path, MAX_PATH, L"%ls\\%ls", dir, jaFile ? SEQ_JA_PHRASES_FILE : SEQ_PHRASES_FILE);
                    path[MAX_PATH - 1] = L'\0';
                    if (jaFile && GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {   // 일본어 문구 치환 (0.70.0)
                        FILE *f = _wfopen(path, L"wb");
                        if (f) {
                            fputs("\xEF\xBB\xBF# Jamotong custom phrases for the Japanese layout - one per line:\r\n"
                                  "# the reading in kana, then a space or tab, then the text. They come first in the candidates.\r\n"
                                  "# \xE3\x82\x88\xE3\x82\x8D \xE3\x82\x88\xE3\x82\x8D\xE3\x81\x97\xE3\x81\x8F\xE3\x81\x8A\xE9\xA1\x98\xE3\x81\x84\xE3\x81\x97\xE3\x81\xBE\xE3\x81\x99\r\n"
                                  "# \xE3\x82\x81\xE3\x82\x8B me@example.com\r\n", f);
                            fclose(f);
                        }
                    }
                    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
                        FILE *f = _wfopen(path, L"wb");
                        if (f) {
                            fputs("\xEF\xBB\xBF# Jamotong custom phrases for the Chinese layouts - one per line:\r\n"
                                  "# letters, then a space or tab, then the text. They come first in the candidates.\r\n"
                                  "# dz \xE5\x8C\x97\xE4\xBA\xAC\xE5\xB8\x82\r\n"
                                  "# yx me@example.com\r\n", f);
                            fclose(f);
                        }
                    }
                    ShellExecuteW(hwnd, L"open", L"notepad.exe", path, NULL, SW_SHOWNORMAL);
                    break;
                }
                case ID_CHK_LOPT_SENTENCE:
                case ID_CHK_LOPT_SUGGEST:
                case ID_CHK_LOPT_FUZZY:
                case ID_CHK_LOPT_EMOJI:
                case ID_CHK_LOPT_TONES:
                case ID_CHK_LOPT_BAR:
                case ID_CHK_LOPT_PUNCT:
                    if (g_loptSel >= 0 && g_loptSel < g_TempConfig.layoutCount) {
                        bool on = SendMessageW((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED;
                        if (LOWORD(wParam) == ID_CHK_LOPT_SENTENCE) g_TempConfig.layouts[g_loptSel].optNoSentence = !on;
                        else if (LOWORD(wParam) == ID_CHK_LOPT_PUNCT) g_TempConfig.layouts[g_loptSel].optNoPunct = !on;
                        else if (LOWORD(wParam) == ID_CHK_LOPT_FUZZY) g_TempConfig.layouts[g_loptSel].optFuzzy = on;
                        else if (LOWORD(wParam) == ID_CHK_LOPT_EMOJI) g_TempConfig.layouts[g_loptSel].optNoEmoji = !on;
                        else if (LOWORD(wParam) == ID_CHK_LOPT_TONES) g_TempConfig.layouts[g_loptSel].optNoTones = !on;
                        else if (LOWORD(wParam) == ID_CHK_LOPT_BAR) g_TempConfig.layouts[g_loptSel].optBar = on;
                        else g_TempConfig.layouts[g_loptSel].optNoSuggest = !on;
                    }
                    break;
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
                case ID_CHK_INLINE:
                    g_TempConfig.options.inlineComposition =
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
                case ID_CMB_CANDSIZE:
                    if (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_EDITCHANGE) {
                        wchar_t b[16] = {0};
                        GetWindowTextW((HWND)lParam, b, 16);
                        if (HIWORD(wParam) == CBN_SELCHANGE) {
                            int sel = (int)SendMessageW((HWND)lParam, CB_GETCURSEL, 0, 0);
                            if (sel >= 0) SendMessageW((HWND)lParam, CB_GETLBTEXT, sel, (LPARAM)b);
                        }
                        int v = _wtoi(b);
                        g_TempConfig.options.candFontSize = (v < 12) ? 12 : (v > 72 ? 72 : v);
                    }
                    break;
                case ID_BTN_CANDFONT_SET: {   // 한자 후보창 글꼴 선택 (공용 대화상자; face만 채택)
                    LOGFONTW lf = {0};
                    lf.lfHeight = -16; lf.lfCharSet = DEFAULT_CHARSET;
                    wcsncpy(lf.lfFaceName, g_TempConfig.options.candFont, LF_FACESIZE - 1);
                    CHOOSEFONTW cf = {0};
                    cf.lStructSize = sizeof(cf);
                    cf.hwndOwner = hwnd;
                    cf.lpLogFont = &lf;
                    cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_NOSCRIPTSEL;
                    if (ChooseFontW(&cf) && lf.lfFaceName[0]) {
                        wcsncpy(g_TempConfig.options.candFont, lf.lfFaceName, 31);
                        g_TempConfig.options.candFont[31] = L'\0';
                        SetWindowTextW(GetDlgItem(hwnd, ID_LBL_CANDFONT), g_TempConfig.options.candFont);
                    }
                    break;
                }
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
                        // B8: 번들 자판은 스테이징에 — 저장소에는 [Apply & Save] 때 들어간다.
                        wchar_t stg[MAX_PATH];
                        ConfigStagedLayouts got; memset(&got, 0, sizeof(got));
                        bool haveStg = Config_StagingLayoutDir(stg, MAX_PATH);
                        if (Config_LoadFromFileEx(&g_TempConfig, szFile, haveStg ? stg : NULL, haveStg ? &got : NULL)) {
                            for (int i = 0; i < got.count && g_stagedImport.count < CONFIG_STAGED_MAX; i++)
                                wcscpy(g_stagedImport.names[g_stagedImport.count++], got.names[i]);
                            RefreshLists(hwnd);
                            MessageBoxW(hwnd,
                                L"Configuration imported. Press Apply & Save to keep it.\n\n"
                                L"Bundled user layouts are installed when you apply, and are available "
                                L"after the IME restarts (sign out / in).",
                                L"Success", MB_OK);
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
                        if (Config_SaveToFile(&g_TempConfig, szFile, true)) {
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
                    wchar_t szFile[MAX_PATH] = {0};
                    OPENFILENAMEW ofn = {0};
                    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
                    ofn.lpstrFile = szFile; ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrFilter = L"Jamotong Layout (*.jmt)\0*.jmt\0All Files\0*.*\0";
                    ofn.nFilterIndex = 1; ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) {
                        // RFC-0008 W1-06: 저장소에 같은 이름의 다른 자판이 있으면 묻고 덮어쓴다(예전엔 조용히 덮었다).
                        //   '아니요'면 추가 자체를 취소 — 이번 세션만 새 자판을 쓰고 저장소엔 옛 파일이 남으면
                        //   재시작 뒤 자판 내용이 말없이 바뀐다.
                        {
                            wchar_t udir0[MAX_PATH], dst0[MAX_PATH];
                            const wchar_t *base0 = wcsrchr(szFile, L'\\');
                            base0 = base0 ? base0 + 1 : szFile;
                            if (Config_UserLayoutDir(udir0, MAX_PATH)) {
                                _snwprintf(dst0, MAX_PATH, L"%ls\\%ls", udir0, base0);
                                dst0[MAX_PATH - 1] = L'\0';
                                if (_wcsicmp(dst0, szFile) != 0 && GetFileAttributesW(dst0) != INVALID_FILE_ATTRIBUTES
                                    && MessageBoxW(hwnd,
                                        L"A layout file with the same name already exists in the user layout store.\n\n"
                                        L"Replace it with the selected file?",
                                        L"Replace layout?", MB_YESNO | MB_ICONQUESTION) != IDYES)
                                    break;
                            }
                        }
                        // 고른 원본을 관리 앱이 임시로 굽고, 그 산출물을 읽는다.
                        wchar_t tmpDir[MAX_PATH], tmpJmb[MAX_PATH], tmpLog[MAX_PATH], args[MAX_PATH * 2];
                        if (!GetTempPathW(MAX_PATH, tmpDir)) tmpDir[0] = L'\0';
                        _snwprintf(tmpJmb, MAX_PATH, L"%lsjamotong_add.jmb", tmpDir);
                        _snwprintf(tmpLog, MAX_PATH, L"%lsjamotong_add.log", tmpDir);
                        tmpJmb[MAX_PATH - 1] = tmpLog[MAX_PATH - 1] = L'\0';
                        DeleteFileW(tmpJmb);
                        _snwprintf(args, MAX_PATH * 2, L"--build \"%ls\" -o \"%ls\"", szFile, tmpJmb);
                        args[MAX_PATH * 2 - 1] = L'\0';
                        bool builtOk = RunLayoutBuilder(args, tmpLog, 15000);
                        LayoutConfig lc; memset(&lc, 0, sizeof(lc));
                        bool addOk = builtOk && JLay_Load(tmpJmb, &lc, NULL);
                        DeleteFileW(tmpJmb);   // 임시 산출물은 읽은 뒤 치운다 (원본은 Apply 때 저장소로)
                        if (addOk) {
                            lc.enabled = true;
                            if (!Config_AppendLayout(&g_TempConfig, &lc)) {
                                Config_FreeLayoutResources(&lc);
                                MessageBoxW(hwnd, L"Out of memory - the layout was not added.", L"Jamotong", MB_OK | MB_ICONERROR);
                                break;
                            }
                            RefreshLists(hwnd);
                            // 사용자 자판 저장소로의 복사는 [Apply & Save] 때 (B8) — 재시작 후 자동 로드 (RFC-0004 P0-2).
                            if (g_pendingAddCount < g_pendingAddCap || GrowPendingAdds()) {
                                const wchar_t *base = wcsrchr(szFile, L'\\');
                                base = base ? base + 1 : szFile;
                                PendingAdd *pa = &g_pendingAdds[g_pendingAddCount++];
                                wcsncpy(pa->src, szFile, MAX_PATH - 1); pa->src[MAX_PATH - 1] = L'\0';
                                wcsncpy(pa->name, base, 127); pa->name[127] = L'\0';
                                pa->layoutName = lc.name;   // Apply 전에 Del 하면 이것으로 찾아 뺀다
                            }
                        } else {
                            // 컴파일러가 낸 진단을 그대로 보여준다 — 줄:열, 코드, 고치는 법 (RFC-0011 P1).
                            static wchar_t msg[3072], body[2816];
                            ReadLogText(tmpLog, body, 2816);
                            _snwprintf(msg, 3072, L"Failed to build the layout (.jmt) file.\n\n%ls",
                                       body[0] ? body : L"invalid or empty file (is jamotong.exe beside the IME?)");
                            msg[3071] = L'\0';
                            MessageBoxW(hwnd, msg, L"Error", MB_ICONERROR);
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
                    // Apply 전에 Add 했던 자판을 지우면 적어 둔 복사도 뺀다 (B8)
                    for (int i = 0; i < g_pendingAddCount; i++) {
                        if (g_pendingAdds[i].layoutName == g_TempConfig.layouts[sel].name) {
                            g_pendingAdds[i] = g_pendingAdds[--g_pendingAddCount];
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
                        Config_CopyShallow(&g_LastSavedConfig, g_pRealConfig);
                        // 사용자 설정 파일에 저장 → 다음 활성화/다른 프로세스·"옵션" 버튼에 반영.
                        wchar_t cfgPath[MAX_PATH];
                        bool saved = Config_UserPath(cfgPath, MAX_PATH) && Config_SaveToFile(g_pRealConfig, cfgPath, false);
                        LeaveCriticalSection(&g_configLock);
                        if (CommitPendingFileOps() > 0)   // B8: Add/Import 의 파일은 지금 저장소에
                            MessageBoxW(hwnd,
                                L"Some layout files could not be copied to the user layout store.\n\n"
                                L"They are loaded for this session only. Copy the .jmt files manually to "
                                L"%APPDATA%\\Jamotong\\layouts.",
                                L"Warning", MB_ICONWARNING);
                        // RFC-0008 W1-06: 저장 실패를 숨기지 않는다(원자적 저장이라 기존 파일은 그대로 남아 있다).
                        if (!saved)
                            MessageBoxW(hwnd,
                                L"The settings were applied to this session, but saving them failed.\n\n"
                                L"The previous settings file was left unchanged, so the new settings will be lost "
                                L"after sign-out. Check that %APPDATA%\\Jamotong is writable.",
                                L"Settings not saved", MB_ICONWARNING);
                    }
                    DestroyWindow(hwnd);
                    break;
                case ID_BTN_RESET:
                    DiscardPendingFileOps();   // B8
                    Config_DiscardEdited(&g_TempConfig, g_pRealConfig);   // temp 고유 리소스 해제 후 재로드
                    Config_LoadDefault(&g_TempConfig);
                    RefreshLists(hwnd);
                    MessageBoxW(hwnd, L"Reset to factory defaults.", L"Info", MB_OK);
                    break;
                case ID_BTN_REVERT:
                    DiscardPendingFileOps();   // B8
                    Config_DiscardEdited(&g_TempConfig, g_pRealConfig);   // temp 고유 리소스 해제 후 복원
                    Config_CopyShallow(&g_TempConfig, &g_LastSavedConfig);
                    RefreshLists(hwnd);
                    MessageBoxW(hwnd, L"Reverted to last saved state.", L"Info", MB_OK);
                    break;
            }
            return 0;

        case WM_DESTROY:
            // 적용 없이 닫힘(취소/X): live가 소유하지 않는 temp 리소스(예: import·reset 잔여) 정리
            if (g_pRealConfig) Config_DiscardEdited(&g_TempConfig, g_pRealConfig);
            DiscardPendingFileOps();   // B8: 적용 없이 닫혔으면 스테이징·적어 둔 복사를 버린다 (적용 뒤면 이미 비어 있다)
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

    // DPI 인지 컨텍스트는 **아무것도 설정하지 않는다** (사용자 결정 2026-08-24).
    //  - 호스트가 DPI 비인지(AkelPad 류): 스레드도 비인지 상속 → GetDpiForWindow=96, 우리는 100% 로
    //    그리고 Windows 가 비트맵 확대한다. 크기 정상(약간 흐림), 호스트 무접촉.
    //  - 호스트가 DPI 인지: 실제 DPI 를 받아 Auto 스케일(ScaleX/Y)이 선명하게 그린다.
    //  - 흐림이 싫으면 설정창의 수동 배율(g_ManualDpiScale)로 조절.
    // ★과거 사고: 여기서 SetProcessDpiAwarenessContext(프로세스 전역)를 불러 DPI 비인지 호스트
    //   (AkelPad)의 시스템 확대를 꺼 버렸다 — 프로세스 재시작 전 불가역(실기 0.17.90 C-4).
    //   TIP 이 로드된 프로세스에서 프로세스 전역 DPI API 호출 금지(매뉴얼 §10). 스레드 한정
    //   호출도 필요 없어 제거 — 설정 안 함이 가장 단순·안전.

    WNDCLASSW wc = {0};
    wc.lpfnWndProc   = SettingsWndProc;
    wc.hInstance     = g_hInst;
    wc.lpszClassName = L"JamotongSettingsClass";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    Jamo_EnsureClass(&wc);   // W2-05

    // 트랜잭션 버퍼 복사 (입력 스레드의 자판 전환/설정 적용과 직렬화)
    if (g_pRealConfig) {
        EnterCriticalSection(&g_configLock);
        Config_CopyShallow(&g_TempConfig, g_pRealConfig);
        Config_CopyShallow(&g_LastSavedConfig, g_pRealConfig);
        LeaveCriticalSection(&g_configLock);
    }
    g_pendingAddCount = 0;          // B8: 새 창은 적어 둔 파일 작업 없이 시작
    g_stagedImport.count = 0;

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
