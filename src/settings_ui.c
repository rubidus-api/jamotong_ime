#include "settings_ui.h"
#include "jamo_class.h"   // RFC-0008 W2-05
#include "jlay.h"      // 구운 자판 읽기 (Add 는 관리 앱을 불러 굽는다)
#include "seq_layout.h"   // 중국어 병음 방식인가 (Layout Options 의 문장부호 칸)
#include "settings_uia.h" // 화면 읽기 도구에 포커스 블록을 알린다
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

// 블록 번호 (옵션 칸·단추·행)
#define ID_CHK_FULLWIDTH     1031
#define ID_CHK_JAMODELETE    1032
#define ID_LBL_LAYOUT_HINT   1035
#define ID_LBL_SHORTCUT_HINT 1036
#define ID_CHK_PREVIEW       1040   // 조합 미리보기 오버레이 (RFC-0002)
#define ID_LBL_PVFONT        1041   // 미리보기 글꼴 표시
#define ID_BTN_PVFONT_SET    1042   // 글꼴 선택(ChooseFont)
#define ID_CMB_PVSIZE        1043   // 미리보기 글꼴 크기 (Auto / 고정 px, 직접 입력 가능)

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
#define ID_LBL_LOPT_FOR      1062

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
//   팔레트 하나. 화면은 창 하나에 사각 블록을 직접 그린다 — 브러시는 DC 브러시 하나를 색만 바꿔 쓰고,
//   만드는 GDI 개체는 글꼴 하나와 ✓ 용 펜 하나뿐이다.
static bool     g_dark = false;
static COLORREF g_clrBg, g_clrText, g_clrBlock, g_clrHot, g_clrAccent, g_clrOnAccent, g_clrDim, g_clrLine;
static HPEN     g_penCheck = NULL;

static void InitTheme(void) {
    DWORD v = 1, sz = sizeof(v); HKEY hk;   // AppsUseLightTheme: 1=light, 0=dark
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &hk) == ERROR_SUCCESS) {
        RegQueryValueExW(hk, L"AppsUseLightTheme", NULL, NULL, (BYTE*)&v, &sz);
        RegCloseKey(hk);
    }
    g_dark = (v == 0);
    if (g_dark) {
        g_clrBg = RGB(32, 32, 32);   g_clrText = RGB(235, 235, 235);
        g_clrBlock = RGB(54, 54, 54); g_clrHot = RGB(72, 72, 72);
        g_clrAccent = RGB(76, 194, 255); g_clrOnAccent = RGB(0, 0, 0);
        g_clrDim = RGB(140, 140, 140); g_clrLine = RGB(80, 80, 80);
    } else {
        g_clrBg = RGB(243, 243, 243); g_clrText = RGB(26, 26, 26);
        g_clrBlock = RGB(228, 228, 228); g_clrHot = RGB(208, 208, 208);
        g_clrAccent = RGB(0, 95, 184); g_clrOnAccent = RGB(255, 255, 255);
        g_clrDim = RGB(120, 120, 120); g_clrLine = RGB(190, 190, 190);
    }
    if (g_penCheck) DeleteObject(g_penCheck);
    g_penCheck = CreatePen(PS_SOLID, ScaleX(2) > 1 ? ScaleX(2) : 2, g_clrOnAccent);
}

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

// 창 논리 크기 (세로는 리사이즈로 늘어남)
#define WIN_W 460
#define WIN_H_MIN 466
static int g_winH = 560;   // 현재 논리 높이

// 바깥 높이를 화면의 작업 영역 안으로 (배율이 크면 창이 화면보다 커져 아래 Apply/Cancel 줄이 밖으로 나갔다)
static int FitOuterH(int h) {
    RECT wa;
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) {
        int mx = wa.bottom - wa.top;
        if (h > mx) h = mx;
    }
    return h;
}
static void KeepOnScreen(HWND hwnd) {   // 아래가 작업 영역을 넘으면 위로 올린다
    RECT wa, wr;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0) || !GetWindowRect(hwnd, &wr)) return;
    int h = wr.bottom - wr.top, y = wr.top;
    if (y + h > wa.bottom) y = wa.bottom - h;
    if (y < wa.top) y = wa.top;
    if (y != wr.top) SetWindowPos(hwnd, NULL, wr.left, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// ── 한 장짜리 스크롤 설정 페이지 ─────────────────────────────────────────────
//   탭도 자식 컨트롤도 없다. 창 하나가 섹션(머리)과 사각 블록(켜고 끄기·단추·행·좌우 고르기)을 위에서 아래로
//   직접 그리고, 넘치면 스크롤한다. 글꼴은 하나(섹션 머리는 강조색 막대와 간격으로 구분). Apply/Cancel 은 아래에 고정.
//   모든 블록은 Item 하나다 — 그리기·마우스·키보드·화면 읽기가 같은 표를 쓴다.
enum { K_HEAD, K_NOTE, K_TOGGLE, K_BUTTON, K_ROW, K_SPIN };   // K_TOGGLE 부터 포커스를 받는다
typedef struct { int kind, id, idx; bool fixed; RECT r; } Item;

static Item *g_items = NULL;
static int g_nItems = 0, g_capItems = 0;
static int g_scroll = 0, g_contentH = 0, g_viewH = 0;   // 물리 픽셀
static int g_focus = -1, g_hover = -1;                  // g_items 번호
static bool g_hasFocus = false, g_dragThumb = false;
static int g_dragY = 0, g_dragScroll = 0;
static int g_layoutSel = 0, g_shortcutSel = -1;         // 고른 자판 / 고른 단축키 행
static int g_dpiSel = 0;                                // 0 = Auto, 1..4 = 100/125/150/200

static const int kDpiScales[] = { 0, 100, 125, 150, 200 };
static const int kPvSizes[]   = { 0, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 64, 96 };   // 0 = Auto, 8~96
static const int kCandSizes[] = { 12, 16, 18, 20, 24, 28, 32, 40, 48, 56, 72 };           // 12~72
#define COUNT_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

static void Relayout(HWND hwnd);

static const LayoutConfig *SelLayout(void) {
    return (g_layoutSel >= 0 && g_layoutSel < g_TempConfig.layoutCount) ? &g_TempConfig.layouts[g_layoutSel] : NULL;
}

// 자판별 옵션이 이 자판에 쓰이는가 (고른 자판에 따라 켜지고 꺼진다)
typedef struct { bool seq, zh, ja, tones, keys; const SeqLayout *sl; } Caps;
static Caps LoptCaps(void) {
    Caps c; memset(&c, 0, sizeof c);
    const LayoutConfig *L = SelLayout();
    if (!L) return c;
    c.seq = L->type == LAYOUT_TYPE_SEQUENCE;
    c.sl = (c.seq && L->pSeqLayout) ? (const SeqLayout *)L->pSeqLayout : NULL;
    c.zh = c.sl && c.sl->zh;
    c.ja = c.sl && c.sl->ja;   // 일본어 방식 (0.70.0)
    c.tones = c.zh && c.sl->toneFile[0];   // 쓸 때 열기 — 이름으로 본다
    c.keys = c.zh && c.sl->nScheme > 0;    // 표 이름은 자판 파일에서 읽었다 (열지 않아도 안다)
    return c;
}
// 자판별 옵션 칸의 (켜짐 여부, 쓸 수 있음)
static bool LoptState(int id, bool *on) {
    Caps c = LoptCaps(); const LayoutConfig *L = SelLayout();
    bool en = false, v = false;
    switch (id) {
        case ID_CHK_LOPT_SENTENCE: en = c.seq;          v = en && !L->optNoSentence; break;
        case ID_CHK_LOPT_SUGGEST:  en = c.seq;          v = en && !L->optNoSuggest;  break;
        case ID_CHK_LOPT_PUNCT:    en = c.zh || c.ja;   v = en && !L->optNoPunct;    break;
        case ID_CHK_LOPT_FUZZY:    en = c.zh;           v = en && L->optFuzzy;       break;
        case ID_CHK_LOPT_EMOJI:    en = c.zh;           v = en && !L->optNoEmoji;    break;
        case ID_CHK_LOPT_TONES:    en = c.tones;        v = en && !L->optNoTones;    break;
        case ID_CHK_LOPT_BAR:      en = c.zh || c.ja;   v = en && L->optBar;         break;
        case ID_BTN_LOPT_PHRASES:  en = c.zh || c.ja;   break;
        case ID_CMB_LOPT_KEYS:     en = c.keys;         break;
    }
    if (on) *on = v;
    return en;
}
static bool *OptFlag(int id) {
    switch (id) {
        case ID_CHK_FULLWIDTH:  return &g_TempConfig.options.fullWidth;
        case ID_CHK_JAMODELETE: return &g_TempConfig.options.jamoDelete;
        case ID_CHK_PREVIEW:    return &g_TempConfig.options.showPreview;
        case ID_CHK_INLINE:     return &g_TempConfig.options.inlineComposition;
    }
    return NULL;
}

static bool ItemOn(const Item *it) {   // 체크 모양을 칠할 상태
    bool on = false;
    if (it->kind == K_ROW && it->id == ID_LST_LAYOUTS)
        return it->idx < g_TempConfig.layoutCount && g_TempConfig.layouts[it->idx].enabled;
    bool *f = OptFlag(it->id);
    if (f) return *f;
    LoptState(it->id, &on);
    return on;
}
static bool ItemEnabled(const Item *it) {
    switch (it->id) {
        case ID_CHK_LOPT_SENTENCE: case ID_CHK_LOPT_SUGGEST: case ID_CHK_LOPT_PUNCT: case ID_CHK_LOPT_FUZZY:
        case ID_CHK_LOPT_EMOJI: case ID_CHK_LOPT_TONES: case ID_CHK_LOPT_BAR: case ID_BTN_LOPT_PHRASES:
        case ID_CMB_LOPT_KEYS:
            return LoptState(it->id, NULL);
        case ID_BTN_LAYOUT_TOGGLE: case ID_BTN_LAYOUT_DEL: return SelLayout() != NULL;
        case ID_BTN_LAYOUT_UP:   return g_layoutSel > 0;
        case ID_BTN_LAYOUT_DOWN: return g_layoutSel >= 0 && g_layoutSel < g_TempConfig.layoutCount - 1;
        case ID_BTN_SHORTCUT_EDIT: case ID_BTN_SHORTCUT_DEL:
            return g_shortcutSel >= 0 && g_shortcutSel < g_TempConfig.shortcuts[g_curScFn].count;
    }
    return true;
}

// 블록의 글. 화면 읽기 도구가 읽는 이름이기도 하다.
static void ItemText(const Item *it, wchar_t *b, int cap) {
    b[0] = L'\0';
    switch (it->id) {
        case ID_BTN_LAYOUT_TOGGLE: lstrcpynW(b, L"On/Off", cap); return;
        case ID_BTN_LAYOUT_UP:     lstrcpynW(b, L"\x25B2", cap); return;
        case ID_BTN_LAYOUT_DOWN:   lstrcpynW(b, L"\x25BC", cap); return;
        case ID_BTN_LAYOUT_ADD: case ID_BTN_SHORTCUT_ADD: lstrcpynW(b, L"Add", cap); return;
        case ID_BTN_LAYOUT_DEL: case ID_BTN_SHORTCUT_DEL: lstrcpynW(b, L"Del", cap); return;
        case ID_BTN_SHORTCUT_EDIT: lstrcpynW(b, L"Edit", cap); return;
        case ID_CHK_LOPT_SENTENCE: lstrcpynW(b, L"Offer the whole sentence first", cap); return;
        case ID_CHK_LOPT_SUGGEST:  lstrcpynW(b, L"Suggest words and idioms (completions, initials)", cap); return;
        case ID_CHK_LOPT_PUNCT:
            lstrcpynW(b, LoptCaps().ja ? L"Japanese punctuation (\x3001\x3002\x300C\x300D\x30FB\x301C\xFF01\xFF1F)"
                                       : L"Chinese punctuation (\xFF0C\x3002\xFF1F\xFF01\x201C\x201D \x2014 \x300C\x300D for traditional)", cap);
            return;
        case ID_CHK_LOPT_FUZZY:    lstrcpynW(b, L"Fuzzy pinyin (z=zh, n=l, an=ang, in=ing ...)", cap); return;
        case ID_CHK_LOPT_EMOJI:    lstrcpynW(b, L"Emoji and symbols among the candidates", cap); return;
        case ID_CHK_LOPT_TONES:    lstrcpynW(b, L"Show pinyin with tones beside the candidates", cap); return;
        case ID_CHK_LOPT_BAR:      lstrcpynW(b, L"Horizontal candidate bar", cap); return;
        case ID_BTN_LOPT_PHRASES:  lstrcpynW(b, L"Edit custom phrases...", cap); return;
        case ID_CMB_LOPT_KEYS: {
            const Caps c = LoptCaps();
            const wchar_t *shown = L"Full pinyin";
            const LayoutConfig *L = SelLayout();
            for (int i = 0; c.keys && L && i < c.sl->nScheme; i++) {
                const wchar_t *nm = c.sl->schemeName[i];
                if (wcscmp(nm, L->optKeys)) continue;
                shown = !wcscmp(nm, L"xiaohe") ? L"Xiaohe (\x5C0F\x9E64\x53CC\x62FC)"
                      : !wcscmp(nm, L"ziranma") ? L"Ziranma (\x81EA\x7136\x7801)"
                      : !wcscmp(nm, L"microsoft") ? L"Microsoft (\x5FAE\x8F6F\x53CC\x62FC)" : nm;
            }
            _snwprintf(b, cap, L"Keys: %ls", shown); b[cap - 1] = L'\0';
            return;
        }
        case ID_CMB_SCFN:
            _snwprintf(b, cap, L"Function: %ls", g_scFnNames[g_curScFn]); b[cap - 1] = L'\0'; return;
        case ID_CHK_FULLWIDTH:  lstrcpynW(b, L"Full-width (fullwidth Latin/symbols)", cap); return;
        case ID_CHK_JAMODELETE: lstrcpynW(b, L"Backspace deletes one jamo at a time", cap); return;
        case ID_CHK_PREVIEW:    lstrcpynW(b, L"Show composition preview (floating)", cap); return;
        case ID_CHK_INLINE:     lstrcpynW(b, L"Inline composition (in the document)", cap); return;
        case ID_BTN_PVFONT_SET:
            _snwprintf(b, cap, L"Preview font: %ls",
                       g_TempConfig.options.previewFont[0] ? g_TempConfig.options.previewFont : L"Malgun Gothic");
            b[cap - 1] = L'\0'; return;
        case ID_CMB_PVSIZE:
            if (g_TempConfig.options.previewFontSize <= 0) lstrcpynW(b, L"Preview size (px): Auto (caret height)", cap);
            else _snwprintf(b, cap, L"Preview size (px): %d", g_TempConfig.options.previewFontSize);
            b[cap - 1] = L'\0'; return;
        case ID_BTN_CANDFONT_SET:
            _snwprintf(b, cap, L"Hanja candidate font: %ls",
                       g_TempConfig.options.candFont[0] ? g_TempConfig.options.candFont : L"Malgun Gothic");
            b[cap - 1] = L'\0'; return;
        case ID_CMB_CANDSIZE:
            _snwprintf(b, cap, L"Hanja candidate size (px): %d", g_TempConfig.options.candFontSize); b[cap - 1] = L'\0'; return;
        case ID_CMB_DPI:
            if (g_dpiSel == 0) lstrcpynW(b, L"Display scale: Auto", cap);
            else _snwprintf(b, cap, L"Display scale: %d%%", kDpiScales[g_dpiSel]);
            b[cap - 1] = L'\0'; return;
        case ID_BTN_IMPORT: lstrcpynW(b, L"Import", cap); return;
        case ID_BTN_EXPORT: lstrcpynW(b, L"Export", cap); return;
        case ID_BTN_RESET:  lstrcpynW(b, L"Reset", cap); return;
        case ID_BTN_REVERT: lstrcpynW(b, L"Revert", cap); return;
        case ID_BTN_APPLY:  lstrcpynW(b, L"Apply & Save", cap); return;
        case ID_BTN_CANCEL: lstrcpynW(b, L"Cancel", cap); return;
    }
    if (it->kind == K_NOTE) {   // 안내 글 — 높이를 재는 것도 그리는 것도 이 글이다
        switch (it->id) {
            case ID_LBL_LAYOUT_HINT:
                lstrcpynW(b, L"Click a row to select it. Click its box (or press Space) to turn it On/Off.", cap); return;
            case ID_LBL_SHORTCUT_HINT:
                lstrcpynW(b, L"Shortcuts for the selected function. Add captures a new key; click a row, then Edit or Del.", cap); return;
            case ID_LBL_LOPT_FOR: {
                const LayoutConfig *S = SelLayout();
                _snwprintf(b, cap, L"For the layout selected above: %ls", (S && S->name) ? S->name : L"-"); b[cap - 1] = L'\0'; return;
            }
            case ID_LBL_LOPT_HINT: {
                Caps c = LoptCaps();
                lstrcpynW(b, c.seq
                  ? (c.zh ? L"For this layout only. Keys: full pinyin or double pinyin (two keys per syllable). Custom phrases: one per line, letters then text (dz \x5317\x4EAC\x5E02)."
                     : c.ja ? L"For this layout only. Space converts, F6-F10 give hiragana, katakana, half-width katakana and romaji. Custom phrases: one per line, reading then text (\x3088\x308D \x3088\x308D\x3057\x304F\x304A\x9858\x3044\x3057\x307E\x3059)."
                        : L"Applies to this layout only. The sentence candidate converts all you typed at once; suggestions offer longer words and idioms that start with it, and initials such as wsm for a whole word.")
                  : L"This layout has no options of its own. Options appear here for input layouts that convert a reading (Chinese pinyin, Japanese kana).", cap);
                return;
            }
        }
        return;
    }
    if (it->kind == K_ROW && it->id == ID_LST_LAYOUTS) {
        if (it->idx < g_TempConfig.layoutCount)
            lstrcpynW(b, g_TempConfig.layouts[it->idx].name ? g_TempConfig.layouts[it->idx].name : L"?", cap);
    } else if (it->kind == K_ROW && it->id == ID_LST_SHORTCUTS) {
        ShortcutList *sl = &g_TempConfig.shortcuts[g_curScFn];
        if (it->idx < sl->count) FormatShortcutStr(&sl->keys[it->idx], b, cap);
    }
}

// ── 놓기 ──
typedef struct { HDC dc; int m, cw, y, rh, gap, pad; } Lay;

static int AddItem(int kind, int id, int idx, bool fixed, int x, int y, int w, int h) {
    if (g_nItems == g_capItems) {
        int cap = g_capItems ? g_capItems * 2 : 64;
        Item *p = (Item *)realloc(g_items, (size_t)cap * sizeof(Item));
        if (!p) return -1;   // 메모리가 없으면 그 블록은 그리지 않는다
        g_items = p; g_capItems = cap;
    }
    Item *it = &g_items[g_nItems];
    it->kind = kind; it->id = id; it->idx = idx; it->fixed = fixed;
    it->r.left = x; it->r.top = y; it->r.right = x + w; it->r.bottom = y + h;
    return g_nItems++;
}
static int TextH(const Lay *L, const wchar_t *t, int w) {
    RECT r = { 0, 0, w > 8 ? w : 8, 0 };
    DrawTextW(L->dc, t, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    return r.bottom;
}
static void LHead(Lay *L, int n) {   // 섹션 머리 — n 번째 이름(kHeads)
    L->y += L->rh / 2;
    AddItem(K_HEAD, 0, n, false, L->m, L->y, L->cw, L->rh);
    L->y += L->rh + L->gap;
}
static void LNote(Lay *L, int id) {
    wchar_t b[512]; Item tmp; tmp.kind = K_NOTE; tmp.id = id; tmp.idx = 0;
    ItemText(&tmp, b, 512);
    int h = TextH(L, b, L->cw - 2 * L->pad) + L->pad;
    AddItem(K_NOTE, id, 0, false, L->m, L->y, L->cw, h);
    L->y += h + L->gap;
}
static void LBlock(Lay *L, int kind, int id, int idx) {   // 한 줄 전체 블록. 글이 길면 줄바꿈해 높이를 늘린다
    wchar_t b[256]; Item tmp; tmp.kind = kind; tmp.id = id; tmp.idx = idx;
    ItemText(&tmp, b, 256);
    int h = L->rh;
    if (kind == K_TOGGLE) {
        int tw = L->cw - ScaleX(16) - 3 * L->pad;
        int th = TextH(L, b, tw) + 2 * ScaleY(8);
        if (th > h) h = th;
    }
    AddItem(kind, id, idx, false, L->m, L->y, L->cw, h);
    L->y += h + L->gap;
}
static void LButtons(Lay *L, const int *ids, int n) {   // 단추 n 개를 한 줄에 나란히
    int w = (L->cw - (n - 1) * L->gap) / n;
    for (int i = 0; i < n; i++)
        AddItem(K_BUTTON, ids[i], 0, false, L->m + i * (w + L->gap), L->y, w, L->rh);
    L->y += L->rh + L->gap;
}

static void ClampSel(void) {
    if (g_layoutSel >= g_TempConfig.layoutCount) g_layoutSel = g_TempConfig.layoutCount - 1;
    if (g_layoutSel < 0) g_layoutSel = g_TempConfig.layoutCount > 0 ? 0 : -1;
    int sc = g_TempConfig.shortcuts[g_curScFn].count;
    if (g_shortcutSel >= sc) g_shortcutSel = sc - 1;
}

// 섹션 머리 글 (그림은 DrawItem 이 K_HEAD 의 자리 번호로 이 표에서 읽는다)
static const wchar_t *kHeads[] = { L"Layouts", L"Layout Options", L"Shortcuts", L"IME Options", L"General" };

static void Relayout(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    if (W <= 0 || H <= 0) return;
    ClampSel();
    HDC dc = GetDC(hwnd);
    HGDIOBJ of = (dc && g_hUiFont) ? SelectObject(dc, g_hUiFont) : NULL;
    Lay L; L.dc = dc; L.m = ScaleX(14); L.cw = W - 2 * L.m - ScaleX(6); L.y = ScaleY(8);
    L.rh = ScaleY(34); L.gap = ScaleY(3); L.pad = ScaleX(10);
    int prevFocusId = (g_focus >= 0 && g_focus < g_nItems) ? g_items[g_focus].id : -1;
    int prevFocusIdx = (g_focus >= 0 && g_focus < g_nItems) ? g_items[g_focus].idx : 0;
    g_nItems = 0;
    if (dc) {
        // 자판
        LHead(&L, 0);
        for (int i = 0; i < g_TempConfig.layoutCount; i++) LBlock(&L, K_ROW, ID_LST_LAYOUTS, i);
        { static const int ids[] = { ID_BTN_LAYOUT_TOGGLE, ID_BTN_LAYOUT_UP, ID_BTN_LAYOUT_DOWN, ID_BTN_LAYOUT_ADD, ID_BTN_LAYOUT_DEL };
          LButtons(&L, ids, 5); }
        LNote(&L, ID_LBL_LAYOUT_HINT);
        // 자판 옵션 — 위에서 고른 자판의 것
        LHead(&L, 1);
        LNote(&L, ID_LBL_LOPT_FOR);
        { static const int ids[] = { ID_CHK_LOPT_SENTENCE, ID_CHK_LOPT_SUGGEST, ID_CHK_LOPT_PUNCT, ID_CHK_LOPT_FUZZY,
                                     ID_CHK_LOPT_EMOJI, ID_CHK_LOPT_TONES, ID_CHK_LOPT_BAR };
          for (int i = 0; i < COUNT_OF(ids); i++) LBlock(&L, K_TOGGLE, ids[i], 0); }
        LBlock(&L, K_SPIN, ID_CMB_LOPT_KEYS, 0);
        { static const int ids[] = { ID_BTN_LOPT_PHRASES }; LButtons(&L, ids, 1); }
        LNote(&L, ID_LBL_LOPT_HINT);
        // 단축키
        LHead(&L, 2);
        LBlock(&L, K_SPIN, ID_CMB_SCFN, 0);
        for (int i = 0; i < g_TempConfig.shortcuts[g_curScFn].count; i++) LBlock(&L, K_ROW, ID_LST_SHORTCUTS, i);
        { static const int ids[] = { ID_BTN_SHORTCUT_ADD, ID_BTN_SHORTCUT_EDIT, ID_BTN_SHORTCUT_DEL }; LButtons(&L, ids, 3); }
        LNote(&L, ID_LBL_SHORTCUT_HINT);
        // IME 옵션
        LHead(&L, 3);
        { static const int ids[] = { ID_CHK_FULLWIDTH, ID_CHK_JAMODELETE, ID_CHK_PREVIEW, ID_CHK_INLINE };
          for (int i = 0; i < COUNT_OF(ids); i++) LBlock(&L, K_TOGGLE, ids[i], 0); }
        LBlock(&L, K_BUTTON, ID_BTN_PVFONT_SET, 0);
        LBlock(&L, K_SPIN, ID_CMB_PVSIZE, 0);
        LBlock(&L, K_BUTTON, ID_BTN_CANDFONT_SET, 0);
        LBlock(&L, K_SPIN, ID_CMB_CANDSIZE, 0);
        // 일반
        LHead(&L, 4);
        LBlock(&L, K_SPIN, ID_CMB_DPI, 0);
        { static const int ids[] = { ID_BTN_IMPORT, ID_BTN_EXPORT, ID_BTN_RESET, ID_BTN_REVERT }; LButtons(&L, ids, 4); }
    }
    g_contentH = L.y + ScaleY(10);
    int bar = ScaleY(48), bh = ScaleY(32), bw = ScaleX(120), cw = ScaleX(84);
    g_viewH = H - bar; if (g_viewH < 1) g_viewH = 1;
    int by = g_viewH + (bar - bh) / 2;
    AddItem(K_BUTTON, ID_BTN_APPLY, 0, true, W - L.m - cw - L.gap - bw, by, bw, bh);
    AddItem(K_BUTTON, ID_BTN_CANCEL, 0, true, W - L.m - cw, by, cw, bh);
    int maxScroll = g_contentH - g_viewH; if (maxScroll < 0) maxScroll = 0;
    if (g_scroll > maxScroll) g_scroll = maxScroll;
    if (g_scroll < 0) g_scroll = 0;
    // 포커스는 같은 블록에 둔다 (번호가 바뀌어도 id·행으로 찾는다)
    g_focus = -1; g_hover = -1;
    for (int i = 0; i < g_nItems; i++)
        if (g_items[i].id == prevFocusId && g_items[i].idx == prevFocusIdx && g_items[i].kind >= K_TOGGLE) { g_focus = i; break; }
    if (of) SelectObject(dc, of);
    if (dc) ReleaseDC(hwnd, dc);
    InvalidateRect(hwnd, NULL, FALSE);
}

static void Refresh(HWND hwnd) { Relayout(hwnd); }

// ── 그리기 ──
static void FillC(HDC dc, const RECT *r, COLORREF c) { SetDCBrushColor(dc, c); FillRect(dc, r, (HBRUSH)GetStockObject(DC_BRUSH)); }
static void FrameC(HDC dc, const RECT *r, COLORREF c) { SetDCBrushColor(dc, c); FrameRect(dc, r, (HBRUSH)GetStockObject(DC_BRUSH)); }

static void DrawBox(HDC dc, const RECT *blk, bool on, bool enabled) {   // 켜짐 = 강조색 상자 + ✓, 꺼짐 = 빈 상자
    int bs = ScaleY(16);
    RECT b = { blk->left + ScaleX(10), blk->top + (blk->bottom - blk->top - bs) / 2, 0, 0 };
    b.right = b.left + bs; b.bottom = b.top + bs;
    COLORREF edge = enabled ? (on ? g_clrAccent : g_clrDim) : g_clrDim;
    if (on && enabled) FillC(dc, &b, g_clrAccent);
    FrameC(dc, &b, edge);
    if (on) {
        HGDIOBJ op = SelectObject(dc, enabled ? (HGDIOBJ)g_penCheck : GetStockObject(BLACK_PEN));
        POINT pt[3] = { { b.left + bs * 22 / 100, b.top + bs * 52 / 100 },
                        { b.left + bs * 42 / 100, b.top + bs * 72 / 100 },
                        { b.left + bs * 78 / 100, b.top + bs * 30 / 100 } };
        Polyline(dc, pt, 3);
        SelectObject(dc, op);
    }
}

static void Frame2(HDC dc, const RECT *r, COLORREF c) {   // 굵기 2 의 포커스 테두리
    RECT in = { r->left + 1, r->top + 1, r->right - 1, r->bottom - 1 };
    FrameC(dc, r, c); FrameC(dc, &in, c);
}

static void DrawItem(HDC dc, const Item *it, int i) {
    RECT r = it->r;
    if (!it->fixed) OffsetRect(&r, 0, -g_scroll);
    wchar_t b[512]; ItemText(it, b, 512);
    bool en = ItemEnabled(it), hot = (i == g_hover) && en, foc = (i == g_focus) && g_hasFocus;
    UINT fmt = DT_NOPREFIX | DT_VCENTER | DT_END_ELLIPSIS | DT_SINGLELINE;
    RECT t = r;
    switch (it->kind) {
    case K_HEAD: {   // 강조색 막대 + 같은 글꼴의 섹션 이름 (그 섹션의 번호는 머리의 차례)
        int n = it->idx;
        RECT bar = { r.left, r.top + ScaleY(4), r.left + ScaleX(4), r.bottom - ScaleY(4) };
        FillC(dc, &bar, g_clrAccent);
        t.left += ScaleX(14);
        SetTextColor(dc, g_clrAccent);
        DrawTextW(dc, kHeads[n < COUNT_OF(kHeads) ? n : 0], -1, &t, fmt);
        return;
    }
    case K_NOTE: {
        t.left += ScaleX(10); t.right -= ScaleX(10);
        SetTextColor(dc, g_clrDim);
        DrawTextW(dc, b, -1, &t, DT_NOPREFIX | DT_WORDBREAK);
        return;
    }
    case K_ROW: {
        bool sel = (it->id == ID_LST_LAYOUTS) ? (it->idx == g_layoutSel) : (it->idx == g_shortcutSel);
        FillC(dc, &r, sel || hot ? g_clrHot : g_clrBlock);
        if (sel) { RECT bar = { r.left, r.top, r.left + ScaleX(4), r.bottom }; FillC(dc, &bar, g_clrAccent); }
        bool box = it->id == ID_LST_LAYOUTS, on = box && ItemOn(it);
        if (box) { DrawBox(dc, &r, on, true); t.left += ScaleX(10) + ScaleY(16) + ScaleX(10); }
        else t.left += ScaleX(14);
        t.right -= ScaleX(8);
        SetTextColor(dc, (box && !on) ? g_clrDim : g_clrText);   // 꺼진 자판은 이름도 흐리게
        DrawTextW(dc, b, -1, &t, fmt);
        break;
    }
    case K_TOGGLE: {
        FillC(dc, &r, hot ? g_clrHot : g_clrBlock);
        DrawBox(dc, &r, ItemOn(it), en);
        t.left += ScaleX(10) + ScaleY(16) + ScaleX(10); t.right -= ScaleX(8);
        SetTextColor(dc, en ? g_clrText : g_clrDim);
        RECT m = t; DrawTextW(dc, b, -1, &m, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);   // 가운데 맞춘 여러 줄
        int th = m.bottom - m.top;
        t.top += ((t.bottom - t.top) - th) / 2; t.bottom = t.top + th;
        DrawTextW(dc, b, -1, &t, DT_NOPREFIX | DT_WORDBREAK);
        break;
    }
    case K_BUTTON: {
        bool apply = it->id == ID_BTN_APPLY;
        FillC(dc, &r, apply ? (hot ? g_clrText : g_clrAccent) : (hot ? g_clrHot : g_clrBlock));
        SetTextColor(dc, apply ? g_clrOnAccent : (en ? g_clrText : g_clrDim));
        if (apply && hot) SetTextColor(dc, g_clrBg);
        // 글꼴 이름·크기를 담은 단추는 왼쪽 맞춤, 그 밖은 가운데
        bool left = (it->id == ID_BTN_PVFONT_SET || it->id == ID_BTN_CANDFONT_SET);
        t.left += ScaleX(10); t.right -= ScaleX(10);
        DrawTextW(dc, b, -1, &t, fmt | (left ? DT_LEFT : DT_CENTER));
        break;
    }
    case K_SPIN: {   // ◀ 값 ▶ — 양 끝 블록으로 이전·다음
        FillC(dc, &r, g_clrBlock);
        int w = r.bottom - r.top;
        RECT a = { r.left, r.top, r.left + w, r.bottom }, z = { r.right - w, r.top, r.right, r.bottom };
        FillC(dc, &a, g_clrHot); FillC(dc, &z, g_clrHot);
        SetTextColor(dc, en ? g_clrText : g_clrDim);
        DrawTextW(dc, L"\x25C0", -1, &a, DT_NOPREFIX | DT_VCENTER | DT_CENTER | DT_SINGLELINE);
        DrawTextW(dc, L"\x25B6", -1, &z, DT_NOPREFIX | DT_VCENTER | DT_CENTER | DT_SINGLELINE);
        t.left = a.right + ScaleX(10); t.right = z.left - ScaleX(10);
        DrawTextW(dc, b, -1, &t, fmt);
        break;
    }
    }
    if (foc) Frame2(dc, &r, g_clrAccent);
}

static void DrawThumbs(HDC dc, int W) {   // 가는 사각 스크롤 막대 (넘칠 때만)
    if (g_contentH <= g_viewH) return;
    int tw = ScaleX(5), x = W - tw - ScaleX(2);
    int th = (int)((long long)g_viewH * g_viewH / g_contentH); if (th < ScaleY(24)) th = ScaleY(24);
    int ty = (int)((long long)(g_viewH - th) * g_scroll / (g_contentH - g_viewH));
    RECT t = { x, ty, x + tw, ty + th };
    FillC(dc, &t, g_dragThumb ? g_clrAccent : g_clrDim);
}

static void DrawAll(HDC dc, int W, int H, const RECT *clip) {
    FillC(dc, clip, g_clrBg);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ of = g_hUiFont ? SelectObject(dc, g_hUiFont) : NULL;
    SaveDC(dc);
    IntersectClipRect(dc, 0, 0, W, g_viewH);
    for (int i = 0; i < g_nItems; i++) {
        const Item *it = &g_items[i];
        if (it->fixed) continue;
        int top = it->r.top - g_scroll, bot = it->r.bottom - g_scroll;
        if (bot < clip->top || top > clip->bottom || bot < 0 || top > g_viewH) continue;
        DrawItem(dc, it, i);
    }
    DrawThumbs(dc, W);
    RestoreDC(dc, -1);
    RECT bar = { 0, g_viewH, W, H };
    FillC(dc, &bar, g_clrBg);
    RECT line = { 0, g_viewH, W, g_viewH + 1 };
    FillC(dc, &line, g_clrLine);
    for (int i = 0; i < g_nItems; i++) if (g_items[i].fixed) DrawItem(dc, &g_items[i], i);
    if (of) SelectObject(dc, of);
}

static void InvalidateItem(HWND hwnd, int i) {
    if (i < 0 || i >= g_nItems) return;
    RECT r = g_items[i].r;
    if (!g_items[i].fixed) OffsetRect(&r, 0, -g_scroll);
    InflateRect(&r, 2, 2);
    InvalidateRect(hwnd, &r, FALSE);
}

static void PaintPage(HWND hwnd) {
    PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
    RECT cr; GetClientRect(hwnd, &cr);
    RECT pr = ps.rcPaint;
    int pw = pr.right - pr.left, ph = pr.bottom - pr.top;
    if (pw > 0 && ph > 0) {
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bm = mem ? CreateCompatibleBitmap(hdc, pw, ph) : NULL;
        if (bm) {   // 칠할 자리만큼의 그림판에 그려 한 번에 옮긴다 (깜박임 없음)
            HGDIOBJ ob = SelectObject(mem, bm);
            SetViewportOrgEx(mem, -pr.left, -pr.top, NULL);
            DrawAll(mem, cr.right, cr.bottom, &pr);
            SetViewportOrgEx(mem, 0, 0, NULL);
            BitBlt(hdc, pr.left, pr.top, pw, ph, mem, 0, 0, SRCCOPY);
            SelectObject(mem, ob);
            DeleteObject(bm);
        } else {
            DrawAll(hdc, cr.right, cr.bottom, &pr);
        }
        if (mem) DeleteDC(mem);
    }
    EndPaint(hwnd, &ps);
}

// ── 스크롤 ──
static void SetScroll(HWND hwnd, int pos) {
    int mx = g_contentH - g_viewH; if (mx < 0) mx = 0;
    if (pos > mx) pos = mx;
    if (pos < 0) pos = 0;
    if (pos == g_scroll) return;
    g_scroll = pos;
    g_hover = -1;
    RECT v = { 0, 0, 100000, g_viewH };
    InvalidateRect(hwnd, &v, FALSE);
}
static void EnsureVisible(HWND hwnd, int i) {
    if (i < 0 || i >= g_nItems || g_items[i].fixed) return;
    int pad = ScaleY(8), top = g_items[i].r.top, bot = g_items[i].r.bottom;
    if (top - pad < g_scroll) SetScroll(hwnd, top - pad);
    else if (bot + pad > g_scroll + g_viewH) SetScroll(hwnd, bot + pad - g_viewH);
}

static int HitTest(int x, int y) {
    for (int i = g_nItems - 1; i >= 0; i--) {
        const Item *it = &g_items[i];
        if (it->kind < K_TOGGLE) continue;
        int yy = it->fixed ? y : y + g_scroll;
        if (!it->fixed && y >= g_viewH) continue;
        if (x >= it->r.left && x < it->r.right && yy >= it->r.top && yy < it->r.bottom) return i;
    }
    return -1;
}
// ── 동작 ─────────────────────────────────────────────────────────────────────
static int FindItem(int id, int idx) {
    for (int i = 0; i < g_nItems; i++) if (g_items[i].id == id && g_items[i].idx == idx) return i;
    return -1;
}

// 창 글꼴(하나)과 ✓ 펜을 지금 배율로 다시 만든다
static void RecreateFont(void) {
    int fontH = -MulDiv(12, g_CurrentDpi, 72);   // 12pt → 픽셀 (음수=문자 높이)
    HFONT nf = CreateFontW(fontH, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (nf) { if (g_hUiFont) DeleteObject(g_hUiFont); g_hUiFont = nf; }
    HPEN np = CreatePen(PS_SOLID, ScaleX(2) > 1 ? ScaleX(2) : 2, g_clrOnAccent);
    if (np) { if (g_penCheck) DeleteObject(g_penCheck); g_penCheck = np; }
}

// 자판 사용(켜짐) 토글. 최소 1개는 켜진 상태를 유지한다.
//   그 줄만 다시 그린다 — 전체를 다시 놓으면 스크롤 자리를 잃고 다른 줄이 깜박였다 (2026-10-03 오너).
static void ToggleLayoutEnabled(HWND hwnd, int sel) {
    if (sel < 0 || sel >= g_TempConfig.layoutCount) return;
    if (g_TempConfig.layouts[sel].enabled) {
        int on = 0;
        for (int i = 0; i < g_TempConfig.layoutCount; i++) if (g_TempConfig.layouts[i].enabled) on++;
        if (on <= 1) { MessageBoxW(hwnd, L"At least one layout must stay On.", L"Info", MB_OK); return; }
    }
    g_TempConfig.layouts[sel].enabled = !g_TempConfig.layouts[sel].enabled;
    InvalidateItem(hwnd, FindItem(ID_LST_LAYOUTS, sel));
}

// 목록에서 v 의 다음(dir>0)·이전(dir<0) 값. 목록 밖의 값(설정 파일에서 온 것)도 가까운 쪽으로 붙는다.
static int StepList(const int *list, int n, int v, int dir) {
    if (dir > 0) { for (int i = 0; i < n; i++) if (list[i] > v) return list[i]; return list[n - 1]; }
    for (int i = n - 1; i >= 0; i--) if (list[i] < v) return list[i];
    return list[0];
}

// 좌우로 고르는 블록 한 칸 (dir = +1 / -1)
static void ChoiceStep(HWND hwnd, int id, int dir) {
    switch (id) {
        case ID_CMB_SCFN:
            g_curScFn = (g_curScFn + dir + SC_FN_COUNT) % SC_FN_COUNT;
            g_shortcutSel = -1;
            Refresh(hwnd);
            return;
        case ID_CMB_LOPT_KEYS: {
            Caps c = LoptCaps(); LayoutConfig *L = (g_layoutSel >= 0 && g_layoutSel < g_TempConfig.layoutCount) ? &g_TempConfig.layouts[g_layoutSel] : NULL;
            if (!c.keys || !L) return;
            int n = 1 + c.sl->nScheme, cur = 0;
            for (int i = 0; i < c.sl->nScheme; i++) if (!wcscmp(c.sl->schemeName[i], L->optKeys)) cur = i + 1;
            cur = (cur + dir + n) % n;
            if (cur == 0) L->optKeys[0] = L'\0';
            else lstrcpynW(L->optKeys, c.sl->schemeName[cur - 1], 16);
            break;
        }
        case ID_CMB_PVSIZE: {
            int v = g_TempConfig.options.previewFontSize;
            v = StepList(kPvSizes, COUNT_OF(kPvSizes), v < 0 ? 0 : v, dir);
            g_TempConfig.options.previewFontSize = v;   // 0 = Auto, 그 밖은 8~96 (목록이 지킨다)
            break;
        }
        case ID_CMB_CANDSIZE: {
            int v = StepList(kCandSizes, COUNT_OF(kCandSizes), g_TempConfig.options.candFontSize, dir);
            g_TempConfig.options.candFontSize = v < 12 ? 12 : (v > 72 ? 72 : v);
            break;
        }
        case ID_CMB_DPI: {
            g_dpiSel = (g_dpiSel + dir + COUNT_OF(kDpiScales)) % COUNT_OF(kDpiScales);
            g_ManualDpiScale = kDpiScales[g_dpiSel];
            if (g_ManualDpiScale == 0) {
                UINT (WINAPI *pGetDpi)(HWND) = (void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
                g_CurrentDpi = pGetDpi ? (int)pGetDpi(hwnd) : 96;
            } else {
                g_CurrentDpi = MulDiv(96, g_ManualDpiScale, 100);
            }
            RecreateFont();
            RECT rc = { 0, 0, ScaleX(WIN_W), ScaleY(g_winH) };
            AdjustWindowRectEx(&rc, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE,
                               (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
            SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, FitOuterH(rc.bottom - rc.top),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);   // WM_SIZE 가 다시 놓는다
            KeepOnScreen(hwnd);
            Refresh(hwnd);
            return;
        }
        default: return;
    }
    InvalidateItem(hwnd, FindItem(id, 0));
}

static void DoCommand(HWND hwnd, int id);

// 자판별 옵션 칸을 뒤집는다
static void ToggleItem(HWND hwnd, const Item *it) {
    if (!ItemEnabled(it)) return;
    bool *f = OptFlag(it->id);
    if (f) { *f = !*f; }
    else {
        LayoutConfig *L = (g_layoutSel >= 0 && g_layoutSel < g_TempConfig.layoutCount) ? &g_TempConfig.layouts[g_layoutSel] : NULL;
        if (!L) return;
        switch (it->id) {
            case ID_CHK_LOPT_SENTENCE: L->optNoSentence = !L->optNoSentence; break;
            case ID_CHK_LOPT_SUGGEST:  L->optNoSuggest = !L->optNoSuggest; break;
            case ID_CHK_LOPT_PUNCT:    L->optNoPunct = !L->optNoPunct; break;
            case ID_CHK_LOPT_FUZZY:    L->optFuzzy = !L->optFuzzy; break;
            case ID_CHK_LOPT_EMOJI:    L->optNoEmoji = !L->optNoEmoji; break;
            case ID_CHK_LOPT_TONES:    L->optNoTones = !L->optNoTones; break;
            case ID_CHK_LOPT_BAR:      L->optBar = !L->optBar; break;
            default: return;
        }
    }
    InvalidateItem(hwnd, (int)(it - g_items));
}

// 화면 읽기 도구에 지금 포커스 블록의 이름을 알린다 (예: "Fuzzy pinyin, checked")
static void Announce(HWND hwnd) {
    if (g_focus < 0 || g_focus >= g_nItems || !IsWindow(hwnd)) return;
    const Item *it = &g_items[g_focus];
    wchar_t b[256], n[300];
    ItemText(it, b, 256);
    bool box = it->kind == K_TOGGLE || (it->kind == K_ROW && it->id == ID_LST_LAYOUTS);
    bool sel = it->kind == K_ROW && ((it->id == ID_LST_LAYOUTS) ? it->idx == g_layoutSel : it->idx == g_shortcutSel);
    _snwprintf(n, 300, L"%ls%ls%ls%ls", b, box ? (ItemOn(it) ? L", checked" : L", not checked") : L"",
               sel ? L", selected" : L"", ItemEnabled(it) ? L"" : L", unavailable");
    n[299] = L'\0';
    SettingsUia_SetName(hwnd, n);
}

// 블록을 눌렀다. key = 0(마우스) / VK_SPACE / VK_RETURN, mx = 마우스 x(클라이언트), dbl = 더블클릭
static void ActivateInner(HWND hwnd, int i, int key, int mx, bool dbl) {
    if (i < 0 || i >= g_nItems) return;
    const Item it = g_items[i];   // 복사 — 아래 동작이 표를 다시 만든다
    switch (it.kind) {
    case K_TOGGLE:
        ToggleItem(hwnd, &g_items[i]);
        break;
    case K_BUTTON:
        if (ItemEnabled(&it)) DoCommand(hwnd, it.id);
        break;
    case K_SPIN: {
        if (!ItemEnabled(&it)) break;
        int h = it.r.bottom - it.r.top, dir = 1;
        if (!key && mx < it.r.left + h) dir = -1;
        ChoiceStep(hwnd, it.id, dir);
        break;
    }
    case K_ROW:
        if (it.id == ID_LST_LAYOUTS) {
            bool onBox = !key && mx < it.r.left + ScaleX(10) + ScaleY(16) + ScaleX(6);
            if (it.idx != g_layoutSel) {   // 고른 자판이 바뀌면 자판 옵션 구역도 바뀐다
                g_layoutSel = it.idx;
                Refresh(hwnd);
                int n = FindItem(ID_LST_LAYOUTS, it.idx);
                if (n >= 0) g_focus = n;
            }
            if (key == VK_SPACE || onBox || (dbl && !onBox)) ToggleLayoutEnabled(hwnd, it.idx);   // 그 줄만 다시 그린다
        } else {
            g_shortcutSel = it.idx;
            InvalidateRect(hwnd, NULL, FALSE);
            if (dbl || key == VK_RETURN) DoCommand(hwnd, ID_BTN_SHORTCUT_EDIT);
        }
        break;
    }
}

static void Activate(HWND hwnd, int i, int key, int mx, bool dbl) {
    ActivateInner(hwnd, i, key, mx, dbl);
    Announce(hwnd);   // 켜짐·꺼짐·고른 값이 바뀌었을 수 있다
}

static void MoveFocus(HWND hwnd, int dir) {
    int i = g_focus, found = -1;
    if (i < 0 && dir < 0) i = g_nItems;
    for (int k = 0; k < g_nItems; k++) {
        i += dir;
        if (i >= g_nItems) i = 0;
        if (i < 0) i = g_nItems - 1;
        if (g_items[i].kind >= K_TOGGLE && ItemEnabled(&g_items[i])) { found = i; break; }
    }
    if (found < 0) return;
    int old = g_focus;
    g_focus = found;
    InvalidateItem(hwnd, old); InvalidateItem(hwnd, g_focus);
    EnsureVisible(hwnd, g_focus);
    Announce(hwnd);
}

static void DoCommand(HWND hwnd, int id) {
    switch (id) {
        case ID_BTN_LAYOUT_TOGGLE:
            ToggleLayoutEnabled(hwnd, g_layoutSel);
            break;
        case ID_BTN_LOPT_PHRASES: {   // 사용자 구 파일을 메모장으로 (없으면 보기 줄을 넣어 만든다)
            wchar_t dir[MAX_PATH], path[MAX_PATH];
            if (!Config_UserDictDir(dir, MAX_PATH)) break;
            const LayoutConfig *PL = (g_layoutSel >= 0 && g_layoutSel < g_TempConfig.layoutCount) ? &g_TempConfig.layouts[g_layoutSel] : NULL;
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
        InvalidateRect(hwnd, NULL, FALSE);
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
        InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }
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
                    Refresh(hwnd);
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
            ofn.lpstrDefExt = L"ini";   // 확장자는 대화상자가 붙인다 (버퍼를 넘기지 않는다)
            ofn.Flags = OFN_OVERWRITEPROMPT;
            if (GetSaveFileNameW(&ofn)) {
                if (Config_SaveToFile(&g_TempConfig, szFile, true)) {
                    MessageBoxW(hwnd, L"Configuration exported successfully.", L"Success", MB_OK);
                } else {
                    MessageBoxW(hwnd, L"Failed to export configuration.", L"Error", MB_ICONERROR);
                }
            }
            break;
        }
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
                    g_layoutSel = g_TempConfig.layoutCount - 1;   // 방금 더한 자판을 고른다
                    Refresh(hwnd);
                    EnsureVisible(hwnd, FindItem(ID_LST_LAYOUTS, g_layoutSel));
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
            int sel = g_layoutSel;
            if (sel > 0 && sel < g_TempConfig.layoutCount) {
                LayoutConfig tmp = g_TempConfig.layouts[sel - 1];
                g_TempConfig.layouts[sel - 1] = g_TempConfig.layouts[sel];
                g_TempConfig.layouts[sel] = tmp;
                g_layoutSel = sel - 1;
                Refresh(hwnd);
                EnsureVisible(hwnd, FindItem(ID_LST_LAYOUTS, g_layoutSel));
            }
            break;
        }
        case ID_BTN_LAYOUT_DOWN: {
            int sel = g_layoutSel;
            if (sel >= 0 && sel < g_TempConfig.layoutCount - 1) {
                LayoutConfig tmp = g_TempConfig.layouts[sel + 1];
                g_TempConfig.layouts[sel + 1] = g_TempConfig.layouts[sel];
                g_TempConfig.layouts[sel] = tmp;
                g_layoutSel = sel + 1;
                Refresh(hwnd);
                EnsureVisible(hwnd, FindItem(ID_LST_LAYOUTS, g_layoutSel));
            }
            break;
        }
        case ID_BTN_LAYOUT_DEL: {
            int sel = g_layoutSel;
            if (sel < 0 || sel >= g_TempConfig.layoutCount || g_TempConfig.layoutCount <= 1) break;   // 최소 1개는 유지
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
            Refresh(hwnd);
            break;
        }
        case ID_BTN_SHORTCUT_ADD: {
            ShortcutList *sl = &g_TempConfig.shortcuts[g_curScFn];
            if (sl->count < SHORTCUTS_MAX) {
                ShortcutKey sk;
                if (CaptureShortcut(hwnd, &sk)) {
                    sl->keys[sl->count++] = sk;
                    g_shortcutSel = sl->count - 1;
                    Refresh(hwnd);
                }
            } else {
                MessageBoxW(hwnd, L"Maximum of 8 shortcuts reached.", L"Info", MB_OK);
            }
            break;
        }
        case ID_BTN_SHORTCUT_EDIT: {
            ShortcutList *sl = &g_TempConfig.shortcuts[g_curScFn];
            int sel = g_shortcutSel;
            if (sel >= 0 && sel < sl->count) {
                ShortcutKey sk;
                if (CaptureShortcut(hwnd, &sk)) {
                    sl->keys[sel] = sk;
                    Refresh(hwnd);
                }
            }
            break;
        }
        case ID_BTN_SHORTCUT_DEL: {
            ShortcutList *sl = &g_TempConfig.shortcuts[g_curScFn];
            int sel = g_shortcutSel;
            if (sel < 0 || sel >= sl->count) break;
            if (g_curScFn == SC_FN_ROTATE && sl->count <= 1) {   // 자판 전환은 최소 1개 유지
                MessageBoxW(hwnd, L"At least one layout-switch shortcut must remain.", L"Info", MB_OK);
                break;
            }
            for (int i = sel; i < sl->count - 1; i++) sl->keys[i] = sl->keys[i + 1];
            sl->count--;
            Refresh(hwnd);
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
            Refresh(hwnd);
            MessageBoxW(hwnd, L"Reset to factory defaults.", L"Info", MB_OK);
            break;
        case ID_BTN_REVERT:
            DiscardPendingFileOps();   // B8
            Config_DiscardEdited(&g_TempConfig, g_pRealConfig);   // temp 고유 리소스 해제 후 복원
            Config_CopyShallow(&g_TempConfig, &g_LastSavedConfig);
            Refresh(hwnd);
            MessageBoxW(hwnd, L"Reverted to last saved state.", L"Info", MB_OK);
            break;
    }
}

// 윈도우 프로시저
static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            g_scroll = 0; g_focus = g_hover = -1; g_hasFocus = false; g_dragThumb = false;
            g_layoutSel = 0; g_shortcutSel = -1;
            // DPI 초기화 (Windows 10 1607+)
            {
                UINT (WINAPI *pGetDpiForWindow)(HWND) = (void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
                if (pGetDpiForWindow && g_ManualDpiScale == 0) {
                    g_CurrentDpi = pGetDpiForWindow(hwnd);
                } else if (g_ManualDpiScale > 0) {
                    g_CurrentDpi = MulDiv(96, g_ManualDpiScale, 100);
                }
                g_dpiSel = 0;
                for (int i = 0; i < COUNT_OF(kDpiScales); i++) if (kDpiScales[i] == g_ManualDpiScale) g_dpiSel = i;
            }
            InitTheme();                 // 다크/라이트 색 준비
            ApplyDarkTitleBar(hwnd);     // 제목표시줄 다크
            RecreateFont();              // 창 글꼴 하나 (Segoe UI 12pt, DPI 스케일)
            // 창 크기를 DPI 스케일된 컨텐츠에 맞춤 (WM_SIZE 가 놓는다).
            {
                RECT rc = { 0, 0, ScaleX(WIN_W), ScaleY(g_winH) };
                DWORD dwStyle   = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
                DWORD dwExStyle = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
                AdjustWindowRectEx(&rc, dwStyle, FALSE, dwExStyle);
                SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, FitOuterH(rc.bottom - rc.top),
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                KeepOnScreen(hwnd);
            }
            Relayout(hwnd);
            return 0;
        }
        case WM_GETOBJECT:
            return SettingsUia_OnGetObject(hwnd, wParam, lParam);
        case WM_ERASEBKGND:
            return 1;   // 그림판에 그려 한 번에 옮긴다 (깜박임 없음)
        case WM_PAINT:
            PaintPage(hwnd);
            return 0;
        case WM_SIZE:
            if (HIWORD(lParam) > 0) g_winH = MulDiv(HIWORD(lParam), 96, g_CurrentDpi);   // 논리높이 기억(DPI 재구성 대비)
            Relayout(hwnd);
            return 0;
        case WM_GETMINMAXINFO: {    // 최소 크기 제한
            MINMAXINFO *mmi = (MINMAXINFO*)lParam;
            mmi->ptMinTrackSize.x = ScaleX(WIN_W) + GetSystemMetrics(SM_CXSIZEFRAME) * 2;
            mmi->ptMinTrackSize.y = FitOuterH(ScaleY(WIN_H_MIN));
            return 0;
        }
        case WM_SETFOCUS:  g_hasFocus = true;  InvalidateItem(hwnd, g_focus); Announce(hwnd); return 0;
        case WM_KILLFOCUS: g_hasFocus = false; InvalidateItem(hwnd, g_focus); return 0;
        case WM_MOUSEWHEEL:
            SetScroll(hwnd, g_scroll - (int)GET_WHEEL_DELTA_WPARAM(wParam) * ScaleY(34) * 3 / WHEEL_DELTA);
            return 0;
        case WM_MOUSEMOVE: {
            int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam);
            if (g_dragThumb) {   // 스크롤 막대 끌기
                int th = (int)((long long)g_viewH * g_viewH / g_contentH); if (th < ScaleY(24)) th = ScaleY(24);
                int track = g_viewH - th;
                if (track > 0) SetScroll(hwnd, g_dragScroll + (int)((long long)(y - g_dragY) * (g_contentH - g_viewH) / track));
                return 0;
            }
            int h = HitTest(x, y);
            if (h != g_hover) {
                int old = g_hover; g_hover = h;
                InvalidateItem(hwnd, old); InvalidateItem(hwnd, h);
                TRACKMOUSEEVENT te = { sizeof te, TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&te);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (g_hover >= 0) { int old = g_hover; g_hover = -1; InvalidateItem(hwnd, old); }
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam);
            RECT cr; GetClientRect(hwnd, &cr);
            SetFocus(hwnd);
            if (y < g_viewH && g_contentH > g_viewH && x >= cr.right - ScaleX(14)) {   // 스크롤 막대 자리
                int th = (int)((long long)g_viewH * g_viewH / g_contentH); if (th < ScaleY(24)) th = ScaleY(24);
                int ty = (int)((long long)(g_viewH - th) * g_scroll / (g_contentH - g_viewH));
                if (y >= ty && y < ty + th) { g_dragThumb = true; g_dragY = y; g_dragScroll = g_scroll; SetCapture(hwnd); InvalidateRect(hwnd, NULL, FALSE); }
                else SetScroll(hwnd, g_scroll + (y < ty ? -g_viewH : g_viewH) * 9 / 10);
                return 0;
            }
            int i = HitTest(x, y);
            if (i >= 0) {
                int old = g_focus; g_focus = i;
                InvalidateItem(hwnd, old); InvalidateItem(hwnd, i);
                Activate(hwnd, i, 0, x, uMsg == WM_LBUTTONDBLCLK);
            }
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            if (g_dragThumb) { g_dragThumb = false; if (uMsg == WM_LBUTTONUP) ReleaseCapture(); InvalidateRect(hwnd, NULL, FALSE); }
            return 0;
        case WM_KEYDOWN: {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            switch (wParam) {
                case VK_DOWN: MoveFocus(hwnd, 1); return 0;
                case VK_UP:   MoveFocus(hwnd, -1); return 0;
                case VK_TAB:  MoveFocus(hwnd, shift ? -1 : 1); return 0;
                case VK_SPACE: case VK_RETURN:
                    if (g_focus >= 0) Activate(hwnd, g_focus, (int)wParam, 0, false);
                    return 0;
                case VK_LEFT: case VK_RIGHT:
                    if (g_focus >= 0 && g_items[g_focus].kind == K_SPIN && ItemEnabled(&g_items[g_focus]))
                    { ChoiceStep(hwnd, g_items[g_focus].id, wParam == VK_LEFT ? -1 : 1); Announce(hwnd); }
                    return 0;
                case VK_PRIOR: SetScroll(hwnd, g_scroll - g_viewH * 9 / 10); return 0;
                case VK_NEXT:  SetScroll(hwnd, g_scroll + g_viewH * 9 / 10); return 0;
                case VK_HOME:  SetScroll(hwnd, 0); return 0;
                case VK_END:   SetScroll(hwnd, g_contentH); return 0;
            }
            break;
        }
        case WM_DESTROY:
            // 적용 없이 닫힘(취소/X): live가 소유하지 않는 temp 리소스(예: import·reset 잔여) 정리
            if (g_pRealConfig) Config_DiscardEdited(&g_TempConfig, g_pRealConfig);
            DiscardPendingFileOps();   // B8: 적용 없이 닫혔으면 스테이징·적어 둔 복사를 버린다 (적용 뒤면 이미 비어 있다)
            if (g_hUiFont) { DeleteObject(g_hUiFont); g_hUiFont = NULL; }
            if (g_penCheck) { DeleteObject(g_penCheck); g_penCheck = NULL; }
            free(g_items); g_items = NULL; g_nItems = g_capItems = 0;
            SettingsUia_Release();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// 스레드 시작점
static DWORD WINAPI SettingsThreadProc(LPVOID lpParam) {
    (void)lpParam;

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
    wc.style         = CS_DBLCLKS;   // 자판 행 더블클릭 = 켜고 끄기
    wc.hbrBackground = NULL;         // 배경은 직접 그린다

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

