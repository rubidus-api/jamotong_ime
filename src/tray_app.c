// jamotong.exe — Jamotong 종합 관리 앱 (외부 실행 관리 도구)
//
// TSF(msctf) 계층 없이 핵심 입력 로직(config/layout/fsm/hangul_layout/chord/klay)만 링크한
// 일반 Win32 창 앱. 트레이 상주가 아니라 작업 표시줄·작업 관리자에 일반 앱으로 나온다. 기능:
//   - .jmt 자판 파일 열기/편집/저장 + 로드 검증(파서 진단 = 줄+사유 표시)
//   - 같은 에디터가 '입력 테스트' 모드도 겸함: 자판/오토마타를 TSF 없이 직접 검증
//   - 설정 창(자판/단축키/옵션) 열기, 정보
// 자판 순환: Right Alt / 한/영 / Shift+Space / 메뉴 [Next layout] (테스트 모드에서).

#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <commdlg.h>   // GetOpenFileName / GetSaveFileName
#include <imm.h>       // (구버전 IMM32 잔재 정리용 /uninstallime)
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"
#include "layout.h"
#include "fsm.h"
#include "hangul_layout.h"
#include "chord.h"
#include "chord_layout.h"
#include "klay.h"      // Klay_Load + KlayDiag (레이아웃 검증)
#include "settings_ui.h"
#include "version.h"

HINSTANCE g_hInst;
CRITICAL_SECTION g_configLock;   // config.c가 참조

static JamotongConfig g_config;
static FsmContext g_fsm;
static ChordContext g_chord;
static HWND g_hMain, g_hEdit, g_hStatus;
static WNDPROC g_editOrigProc;
static int g_compStart = 0, g_compLen = 0;   // 조합(preedit) 구간 [start, start+len)
static int g_dpi = 96;
#define S(x) MulDiv((x), g_dpi, 96)
static bool g_testMode = true;                 // true=입력 테스트(오토마타), false=.jmt 편집(raw)
static wchar_t g_curFile[MAX_PATH] = L"";      // 현재 편집 중인 .jmt 경로 ("" = 없음)

// ── 메뉴 명령 ID ──
#define IDM_OPEN     1101
#define IDM_SAVE     1102
#define IDM_SAVEAS   1103
#define IDM_EXIT     1104
#define IDM_CLEAR    1201
#define IDM_SELALL   1202
#define IDM_NEXT     1301
#define IDM_SETTINGS 1302
#define IDM_VALIDATE 1401
#define IDM_TESTMODE 1402
#define IDM_ABOUT    1501

// ── QWERTY/모디파이어 헬퍼 (오토마타 테스트용) ──
static wchar_t GetQwertyChar(WPARAM vk, bool shift) {
    if (vk >= 'A' && vk <= 'Z') return shift ? (wchar_t)vk : (wchar_t)(vk + 32);
    if (vk >= '0' && vk <= '9') { if (!shift) return (wchar_t)vk; const wchar_t s[] = L")!@#$%^&*("; return s[vk - '0']; }
    switch (vk) {
        case VK_OEM_1: return shift ? L':' : L';';
        case VK_OEM_PLUS: return shift ? L'+' : L'=';
        case VK_OEM_COMMA: return shift ? L'<' : L',';
        case VK_OEM_MINUS: return shift ? L'_' : L'-';
        case VK_OEM_PERIOD: return shift ? L'>' : L'.';
        case VK_OEM_2: return shift ? L'?' : L'/';
        case VK_OEM_3: return shift ? L'~' : L'`';
        case VK_OEM_4: return shift ? L'{' : L'[';
        case VK_OEM_5: return shift ? L'|' : L'\\';
        case VK_OEM_6: return shift ? L'}' : L']';
        case VK_OEM_7: return shift ? L'"' : L'\'';
        case VK_SPACE: return L' ';
    }
    return 0;
}

static bool IsModifierOrLock(UINT vk) {
    switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN: case VK_APPS:
        case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
        case VK_HANGUL: case VK_HANJA:
            return true;
    }
    return false;
}

static void UpdateStatus(void) {
    LayoutConfig *L = Config_GetCurrentLayout(&g_config);
    const wchar_t *fname = g_curFile[0] ? (wcsrchr(g_curFile, L'\\') ? wcsrchr(g_curFile, L'\\') + 1 : g_curFile) : L"(none)";
    wchar_t s[256];
    if (g_testMode)
        wsprintfW(s, L"Mode: INPUT TEST  |  Layout: %ls  (switch: Right Alt / Hangul / Shift+Space)",
                  (L && L->name) ? L->name : L"?");
    else
        wsprintfW(s, L"Mode: EDIT .jmt  |  File: %ls  (Tools ▸ Validate to check, Tools ▸ Test input to type)", fname);
    if (g_hStatus) SetWindowTextW(g_hStatus, s);
    wchar_t t[160];
    wsprintfW(t, L"Jamotong Manager%ls%ls", g_curFile[0] ? L" — " : L"", g_curFile[0] ? fname : L"");
    if (g_hMain) SetWindowTextW(g_hMain, t);
}

static void ResetComp(void) { g_compLen = 0; }

// 자모 결과를 에디트에 반영: 조합 구간을 (commit+preedit)로 치환, 새 조합 구간=preedit(하이라이트).
static void ApplyResult(wchar_t commit, wchar_t preedit) {
    wchar_t rep[4]; int n = 0;
    if (commit) rep[n++] = commit;
    if (preedit) rep[n++] = preedit;
    rep[n] = 0;
    if (g_compLen > 0) {
        SendMessageW(g_hEdit, EM_SETSEL, g_compStart, g_compStart + g_compLen);
    } else {
        DWORD a = 0, b = 0; SendMessageW(g_hEdit, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
        g_compStart = (int)a;
    }
    SendMessageW(g_hEdit, EM_REPLACESEL, TRUE, (LPARAM)rep);
    g_compStart += (commit ? 1 : 0);
    g_compLen = preedit ? 1 : 0;
    if (g_compLen > 0) SendMessageW(g_hEdit, EM_SETSEL, g_compStart, g_compStart + g_compLen);
    else { int c = g_compStart; SendMessageW(g_hEdit, EM_SETSEL, c, c); }
}

// 키다운 처리(테스트 모드). true=소비(에디트 기본처리 생략), false=에디트에 위임.
static bool AutomataKeyDown(UINT vk, LPARAM lParam) {
    UINT rvk = Config_ResolveVK(vk, lParam);
    if (Config_IsShortcut(&g_config, SC_FN_ROTATE, rvk, Config_CurrentMods())) {
        ResetComp(); Fsm_Init(&g_fsm); Chord_Init(&g_chord);
        Config_RotateLayout(&g_config); UpdateStatus();
        return true;
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000) ||
        (GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) {
        if (g_fsm.state != STATE_EMPTY) { ApplyResult(Fsm_Flush(&g_fsm), 0); ResetComp(); }
        return false;
    }
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    LayoutConfig *L = Config_GetCurrentLayout(&g_config);
    if (!L) return false;

    switch (L->type) {
        case LAYOUT_TYPE_PASSTHROUGH:
            ResetComp();
            return false;
        case LAYOUT_TYPE_STATIC_MAP: {
            wchar_t qc = GetQwertyChar(vk, shift);
            if (qc > 0 && qc < 256 && L->charMap[qc]) { ApplyResult(L->charMap[qc], 0); ResetComp(); return true; }
            ResetComp();
            return false;
        }
        case LAYOUT_TYPE_KOREAN_FSM:
        case LAYOUT_TYPE_HANGUL_CUSTOM: {
            const HangulLayout *hl = (const HangulLayout*)L->pHangulLayout;
            if (hl && hl->moachigi) {
                if (vk == VK_BACK && g_compLen > 0) { Chord_Init(&g_chord); ApplyResult(0, 0); return true; }
                wchar_t kc = GetQwertyChar(vk, shift);
                ChordResult cr = Chord_KeyDown(&g_chord, hl, vk, kc);
                if (cr.eaten) { ApplyResult(0, cr.composing); return true; }
                return false;
            }
            if (vk == VK_BACK) {
                if (g_fsm.state != STATE_EMPTY) { wchar_t pe = 0; Fsm_Backspace(&g_fsm, &pe); ApplyResult(0, pe); return true; }
                ResetComp();
                return false;
            }
            wchar_t kc = GetQwertyChar(vk, shift);
            LayoutResult lr = { JAMO_NONE, 0 };
            if (kc > 0 && kc < 128) lr = hl ? hl->keymap[(int)kc] : Layout_MapKeyToJamo(kc, L->kbdVariant);
            if (lr.type != JAMO_NONE) {
                FsmResult r = Fsm_ProcessKey(&g_fsm, kc, L->kbdVariant, hl);
                ApplyResult(r.commitChar, r.preeditChar);
                return true;
            }
            if (!IsModifierOrLock(vk) && g_fsm.state != STATE_EMPTY) {
                ApplyResult(Fsm_Flush(&g_fsm), 0); ResetComp();
            }
            return false;
        }
        case LAYOUT_TYPE_CHORD:
            return false;   // ARTSEY류 SendInput 기반 — 하네스 미지원
        default:
            return false;
    }
}

static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // .jmt 편집 모드에서는 오토마타를 끄고 일반 텍스트 편집(raw)만 한다.
    if (g_testMode && (m == WM_KEYDOWN || m == WM_SYSKEYDOWN)) {
        if (AutomataKeyDown((UINT)w, l)) return 0;
        if ((UINT)w == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
            SendMessageW(h, EM_SETSEL, 0, (LPARAM)-1);
            return 0;
        }
        MSG mm = { h, m, w, l, 0, { 0, 0 } };
        TranslateMessage(&mm);
    } else if (g_testMode && (m == WM_KEYUP || m == WM_SYSKEYUP)) {
        LayoutConfig *L = Config_GetCurrentLayout(&g_config);
        if (L && (L->type == LAYOUT_TYPE_KOREAN_FSM || L->type == LAYOUT_TYPE_HANGUL_CUSTOM)) {
            const HangulLayout *hl = (const HangulLayout*)L->pHangulLayout;
            if (hl && hl->moachigi && (UINT)w < 256 && g_chord.keyDown[(UINT)w]) {
                ChordResult cr = Chord_KeyUp(&g_chord, (UINT)w);
                if (cr.commit) { ApplyResult(cr.commit, 0); ResetComp(); }
                return 0;
            }
        }
    }
    return CallWindowProcW(g_editOrigProc, h, m, w, l);
}

static void Relayout(void) {
    if (!g_hMain) return;
    RECT rc; GetClientRect(g_hMain, &rc);
    MoveWindow(g_hStatus, S(10), S(8), rc.right - S(20), S(22), TRUE);
    MoveWindow(g_hEdit, S(10), S(36), rc.right - S(20), rc.bottom - S(46), TRUE);
}

// ── 파일 IO (UTF-8 ↔ wide) ──
static bool ReadFileUtf8(const wchar_t *path, wchar_t **outText) {
    *outText = NULL;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD sz = GetFileSize(f, NULL);
    if (sz == INVALID_FILE_SIZE || sz > 8 * 1024 * 1024) { CloseHandle(f); return false; }
    char *raw = (char*)malloc(sz + 1); if (!raw) { CloseHandle(f); return false; }
    DWORD rd = 0; ReadFile(f, raw, sz, &rd, NULL); CloseHandle(f); raw[rd] = '\0';
    char *p = raw; int n = (int)rd;
    if (n >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) { p += 3; n -= 3; }
    int wl = MultiByteToWideChar(CP_UTF8, 0, p, n, NULL, 0);
    wchar_t *w = (wchar_t*)malloc(((size_t)wl + 1) * sizeof(wchar_t));
    if (w) { MultiByteToWideChar(CP_UTF8, 0, p, n, w, wl); w[wl] = L'\0'; }
    free(raw);
    if (!w) return false;
    // EDIT 컨트롤은 \n을 줄바꿈으로 안 그림 — \r\n 정규화는 EM_SETTEXT가 알아서 하지만
    // 안전하게 로드 텍스트의 홑 \n을 그대로 두고, 에디트가 표시. (대부분 .jmt는 \n)
    *outText = w;
    return true;
}

static bool WriteFileUtf8FromEdit(const wchar_t *path) {
    int len = GetWindowTextLengthW(g_hEdit);
    wchar_t *w = (wchar_t*)malloc(((size_t)len + 1) * sizeof(wchar_t));
    if (!w) return false;
    GetWindowTextW(g_hEdit, w, len + 1);
    // EDIT은 줄바꿈을 \r\n으로 준다 — 파일엔 \n만 남기도록 정규화(.jmt는 LF 기준).
    int u8 = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *buf = (char*)malloc(u8 > 0 ? u8 : 1);
    if (!buf) { free(w); return false; }
    WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, u8, NULL, NULL);
    free(w);
    // \r\n → \n
    int wn = 0; for (int i = 0; buf[i]; i++) if (buf[i] != '\r') buf[wn++] = buf[i];
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { free(buf); return false; }
    DWORD wr = 0; WriteFile(f, buf, (DWORD)wn, &wr, NULL); CloseHandle(f);
    free(buf);
    return true;
}

static void SetEditText(const wchar_t *text) {
    g_compLen = 0; Fsm_Init(&g_fsm); Chord_Init(&g_chord);
    SetWindowTextW(g_hEdit, text ? text : L"");
}

static bool BrowseFile(bool save, wchar_t *path, int cch) {
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMain;
    ofn.lpstrFile = path;
    ofn.nMaxFile = cch;
    ofn.lpstrFilter = L"Jamotong Layout (*.jmt)\0*.jmt\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = L"jmt";
    ofn.Flags = save ? OFN_OVERWRITEPROMPT : (OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST);
    return save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
}

static void SetTestMode(bool on) {
    g_testMode = on;
    HMENU menu = GetMenu(g_hMain);
    if (menu) CheckMenuItem(menu, IDM_TESTMODE, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    g_compLen = 0; Fsm_Init(&g_fsm); Chord_Init(&g_chord);
    UpdateStatus();
    SetFocus(g_hEdit);
}

// 현재 에디터 내용을 임시 .jmt로 저장 후 Klay_Load 로 검증 — 파서 진단(줄+사유) 표시.
static void ValidateCurrent(void) {
    wchar_t tmpDir[MAX_PATH], tmp[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tmpDir)) { MessageBoxW(g_hMain, L"No temp path.", L"Validate", MB_ICONERROR); return; }
    _snwprintf(tmp, MAX_PATH, L"%lsjamotong_validate.jmt", tmpDir);
    if (!WriteFileUtf8FromEdit(tmp)) { MessageBoxW(g_hMain, L"Could not write a temporary file.", L"Validate", MB_ICONERROR); return; }
    LayoutConfig lc; memset(&lc, 0, sizeof(lc));
    KlayDiag diag = {0};
    bool ok = Klay_Load(tmp, &lc, &diag);
    DeleteFileW(tmp);
    if (ok) {
        const wchar_t *type = lc.type == LAYOUT_TYPE_STATIC_MAP ? L"static"
                            : lc.type == LAYOUT_TYPE_CHORD ? L"chord" : L"hangul";
        wchar_t msg[256];
        _snwprintf(msg, 256, L"OK — loads as a valid %ls layout.\nName: %ls",
                   type, lc.name ? lc.name : L"?");
        Config_FreeLayoutResources(&lc);
        MessageBoxW(g_hMain, msg, L"Validate — OK", MB_ICONINFORMATION);
    } else {
        wchar_t msg[320];
        if (diag.line > 0) _snwprintf(msg, 320, L"Invalid layout.\n\nLine %d: %ls", diag.line, diag.message);
        else _snwprintf(msg, 320, L"Invalid layout.\n\n%ls", diag.message[0] ? diag.message : L"empty or unreadable");
        MessageBoxW(g_hMain, msg, L"Validate — error", MB_ICONERROR);
    }
    SetFocus(g_hEdit);
}

static void ShowAbout(void) {
    wchar_t msg[512];
    _snwprintf(msg, 512,
        L"Jamotong Manager\nVersion %ls\n\n"
        L"Pure C23 + WinAPI Korean IME (TSF).\n"
        L"Author: %ls\n%ls\n\n"
        L"This app manages layouts and settings, edits/validates .jmt files, "
        L"and tests input without TSF.",
        JAMOTONG_VERSION, JAMOTONG_AUTHOR, JAMOTONG_HOMEPAGE);
    MessageBoxW(g_hMain, msg, L"About Jamotong", MB_ICONINFORMATION);
}

static void DoOpen(void) {
    wchar_t path[MAX_PATH] = L"";
    if (!BrowseFile(false, path, MAX_PATH)) return;
    wchar_t *text = NULL;
    if (!ReadFileUtf8(path, &text)) { MessageBoxW(g_hMain, L"Could not read the file.", L"Open", MB_ICONERROR); return; }
    SetEditText(text); free(text);
    wcsncpy(g_curFile, path, MAX_PATH - 1); g_curFile[MAX_PATH - 1] = L'\0';
    SetTestMode(false);   // 파일을 열면 편집 모드로
}

static void DoSave(bool saveAs) {
    wchar_t path[MAX_PATH];
    if (saveAs || !g_curFile[0]) {
        path[0] = L'\0';
        if (g_curFile[0]) wcsncpy(path, g_curFile, MAX_PATH - 1);
        if (!BrowseFile(true, path, MAX_PATH)) return;
    } else {
        wcsncpy(path, g_curFile, MAX_PATH - 1); path[MAX_PATH - 1] = L'\0';
    }
    if (!WriteFileUtf8FromEdit(path)) { MessageBoxW(g_hMain, L"Could not save the file.", L"Save", MB_ICONERROR); return; }
    wcsncpy(g_curFile, path, MAX_PATH - 1); g_curFile[MAX_PATH - 1] = L'\0';
    UpdateStatus();
}

static void OnCommand(int id) {
    switch (id) {
        case IDM_OPEN:     DoOpen(); break;
        case IDM_SAVE:     DoSave(false); break;
        case IDM_SAVEAS:   DoSave(true); break;
        case IDM_EXIT:     DestroyWindow(g_hMain); break;
        case IDM_CLEAR:    SetEditText(L""); g_curFile[0] = L'\0'; UpdateStatus(); break;
        case IDM_SELALL:   SendMessageW(g_hEdit, EM_SETSEL, 0, (LPARAM)-1); break;
        case IDM_NEXT:     ResetComp(); Fsm_Init(&g_fsm); Chord_Init(&g_chord);
                           Config_RotateLayout(&g_config); UpdateStatus(); break;
        case IDM_SETTINGS: SettingsUI_Show(&g_config); break;
        case IDM_VALIDATE: ValidateCurrent(); break;
        case IDM_TESTMODE: SetTestMode(!g_testMode); break;
        case IDM_ABOUT:    ShowAbout(); break;
    }
    if (id != IDM_SETTINGS && id != IDM_ABOUT && id != IDM_VALIDATE) SetFocus(g_hEdit);
}

static HMENU BuildMenu(void) {
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu(), edit = CreatePopupMenu(),
          lay = CreatePopupMenu(), tools = CreatePopupMenu(), help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_OPEN,   L"&Open .jmt...\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDM_SAVE,   L"&Save\tCtrl+S");
    AppendMenuW(file, MF_STRING, IDM_SAVEAS, L"Save &As...");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, IDM_EXIT,   L"E&xit");
    AppendMenuW(edit, MF_STRING, IDM_SELALL, L"Select &All\tCtrl+A");
    AppendMenuW(edit, MF_STRING, IDM_CLEAR,  L"&Clear");
    AppendMenuW(lay,  MF_STRING, IDM_NEXT,     L"&Next layout");
    AppendMenuW(lay,  MF_STRING, IDM_SETTINGS, L"&Settings...");
    AppendMenuW(tools, MF_STRING, IDM_VALIDATE, L"&Validate layout");
    AppendMenuW(tools, MF_STRING | MF_CHECKED, IDM_TESTMODE, L"&Test input (type Hangul)");
    AppendMenuW(help, MF_STRING, IDM_ABOUT,  L"&About...");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file,  L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit,  L"&Edit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)lay,   L"&Layout");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)tools, L"&Tools");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help,  L"&Help");
    return bar;
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_COMMAND:
            if (HIWORD(w) == 0) { OnCommand(LOWORD(w)); return 0; }   // 메뉴/액셀
            break;
        case WM_SIZE:     Relayout(); return 0;
        case WM_SETFOCUS: SetFocus(g_hEdit); return 0;
        case WM_DESTROY:  g_hMain = NULL; PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static int UninstallIme(void) {
    // Keyboard Layouts 에서 IME File==jamotong.ime 인 KLID 제거 + 파일 삭제 (구버전 IMM32 잔재).
    HKEY hRoot;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts",
                      0, KEY_READ, &hRoot) == ERROR_SUCCESS) {
        wchar_t klid[64]; DWORD i = 0, len;
        wchar_t toDelete[16][64]; int nDel = 0;
        while (len = 64, RegEnumKeyExW(hRoot, i++, klid, &len, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            HKEY hk; wchar_t imefile[MAX_PATH]; DWORD sz = sizeof(imefile);
            if (RegOpenKeyExW(hRoot, klid, 0, KEY_READ, &hk) == ERROR_SUCCESS) {
                if (RegQueryValueExW(hk, L"IME File", NULL, NULL, (BYTE*)imefile, &sz) == ERROR_SUCCESS) {
                    if (_wcsicmp(imefile, L"jamotong.ime") == 0 && nDel < 16) wcscpy(toDelete[nDel++], klid);
                }
                RegCloseKey(hk);
            }
        }
        for (int k = 0; k < nDel; k++) RegDeleteKeyW(hRoot, toDelete[k]);
        RegCloseKey(hRoot);
    }
    wchar_t sys[MAX_PATH], p[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH); _snwprintf(p, MAX_PATH, L"%ls\\jamotong.ime", sys); DeleteFileW(p);
    if (GetSystemWow64DirectoryW(sys, MAX_PATH)) { _snwprintf(p, MAX_PATH, L"%ls\\jamotong.ime", sys); DeleteFileW(p); }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hI, HINSTANCE hP, PWSTR cmd, int show) {
    (void)hP;
    g_hInst = hI;
    if (cmd && wcsstr(cmd, L"/uninstallime")) return UninstallIme();   // 구버전 IMM32 잔재 정리 전용

    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    BOOL (WINAPI *pSetCtx)(HANDLE) = (void*)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (pSetCtx) pSetCtx((HANDLE)-4);   // Per-Monitor V2

    InitializeCriticalSection(&g_configLock);
    Config_LoadDefault(&g_config);
    {   // 저장된 사용자 설정 병합 로드 (설정창이 이 실제 설정을 편집·저장)
        wchar_t cfgPath[MAX_PATH];
        if (Config_UserPath(cfgPath, MAX_PATH)) Config_LoadFromFile(&g_config, cfgPath);
    }
    Fsm_Init(&g_fsm);
    Chord_Init(&g_chord);

    UINT (WINAPI *pGetDpi)(HWND) = (void*)GetProcAddress(u32, "GetDpiForWindow");

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hI;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"JamotongManager";
    wc.hIcon = LoadIconW(hI, MAKEINTRESOURCEW(1));   // .rc의 프로파일 아이콘
    RegisterClassW(&wc);

    g_hMain = CreateWindowW(L"JamotongManager", L"Jamotong Manager",
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            820, 560, NULL, BuildMenu(), hI, NULL);
    if (!g_hMain) return 1;
    if (pGetDpi) g_dpi = (int)pGetDpi(g_hMain);
    SetWindowPos(g_hMain, NULL, 0, 0, S(820), S(560), SWP_NOMOVE | SWP_NOZORDER);

    static HFONT s_font = NULL, s_editFont = NULL;
    s_font = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    s_editFont = CreateFontW(-S(18), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             FIXED_PITCH | FF_MODERN, L"Consolas");   // 편집·정렬에 고정폭

    g_hStatus = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                              S(10), S(8), S(780), S(22), g_hMain, NULL, hI, NULL);
    g_hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                              ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN,
                              S(10), S(36), S(790), S(470), g_hMain, NULL, hI, NULL);
    SendMessageW(g_hEdit, EM_LIMITTEXT, 1024 * 1024, 0);   // 큰 .jmt 편집 허용
    SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)s_font, TRUE);
    SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)s_editFont, TRUE);
    g_editOrigProc = (WNDPROC)SetWindowLongPtrW(g_hEdit, GWLP_WNDPROC, (LONG_PTR)EditProc);

    // 명령행에 .jmt 경로가 오면 열어서 편집 모드로 (관리 도구답게)
    if (cmd && cmd[0]) {
        wchar_t path[MAX_PATH] = L""; const wchar_t *q = cmd;
        while (*q == L' ' || *q == L'"') q++;
        if (*q && q[0] != L'/') {
            wcsncpy(path, q, MAX_PATH - 1);
            wchar_t *endq = wcschr(path, L'"'); if (endq) *endq = L'\0';
            wchar_t *text = NULL;
            if (path[0] && ReadFileUtf8(path, &text)) {
                SetEditText(text); free(text);
                wcsncpy(g_curFile, path, MAX_PATH - 1); g_testMode = false;
            }
        }
    }

    UpdateStatus();
    Relayout();
    ShowWindow(g_hMain, show == 0 ? SW_SHOWNORMAL : show);
    SetFocus(g_hEdit);

    // 액셀러레이터: Ctrl+O/S/A
    ACCEL acc[] = {
        { FCONTROL | FVIRTKEY, 'O', IDM_OPEN },
        { FCONTROL | FVIRTKEY, 'S', IDM_SAVE },
    };
    HACCEL hacc = CreateAcceleratorTableW(acc, 2);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (hacc && TranslateAcceleratorW(g_hMain, hacc, &msg)) continue;
        if (IsDialogMessageW(g_hMain, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (hacc) DestroyAcceleratorTable(hacc);
    SettingsUI_Shutdown();
    return 0;
}
