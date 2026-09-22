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
#include "ui_server.h"   // RFC-0015 UI 헬퍼 모드
#include "klay_cli.h"    // RFC-0011 P3 .jmt 저작 도구 명령
#include <shellapi.h>    // CommandLineToArgvW
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
// ── RFC-0011 P5: 저작 도구 — 시험칸과 "이 파일로 시험" ──
static HWND g_hTry, g_hTryLabel;            // 아래쪽 한 줄 시험칸 (편집 중인 .jmt 를 건드리지 않고 쳐 본다)
static HWND g_hTarget;                      // 오토마타 출력이 갈 에디트 (g_hEdit 또는 g_hTry)
static LayoutConfig g_fileLayout;           // Tools ▸ Try this file 로 읽은 자판 (없으면 type=0 / name=NULL)
static bool g_tryFile = false;              // 시험칸이 편집 중인 파일 자판을 쓰는가
static WNDPROC g_tryOrigProc;
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
#define IDM_TRYFILE  1403   // RFC-0011 P5
#define IDM_EXPAND   1106
#define IDM_INSTALL  1107
#define IDM_NEW_3BUL 1110
#define IDM_NEW_DV   1111
#define IDM_NEW_QW   1112
#define IDM_DER_3BUL 1113
#define IDM_DER_DV   1114

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
    // RFC-0008 W0-05: 자판 이름과 파일 이름은 사용자 데이터다 — 길이를 우리가 정하지 않는다.
    // `wsprintfW` 는 대상 크기를 모르고 1024자에서 자른다 → 256/160 버퍼에 그대로 쓰면 넘친다.
    // `_snwprintf` 로 경계를 넘기고, 잘릴 때를 대비해 널 종료를 손으로 보장한다.
    wchar_t s[256];
    if (g_testMode)
        _snwprintf(s, 256, L"Mode: INPUT TEST  |  Layout: %ls  (switch: Right Alt / Hangul / Shift+Space)",
                   (L && L->name) ? L->name : L"?");
    else
        _snwprintf(s, 256, L"Mode: EDIT .jmt  |  File: %ls  (Tools ▸ Validate to check, Tools ▸ Test input to type)", fname);
    s[255] = L'\0';
    if (g_hStatus) SetWindowTextW(g_hStatus, s);
    wchar_t t[160];
    _snwprintf(t, 160, L"Jamotong Manager%ls%ls", g_curFile[0] ? L" — " : L"", g_curFile[0] ? fname : L"");
    t[159] = L'\0';
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
        SendMessageW(g_hTarget, EM_SETSEL, g_compStart, g_compStart + g_compLen);
    } else {
        DWORD a = 0, b = 0; SendMessageW(g_hTarget, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
        g_compStart = (int)a;
    }
    SendMessageW(g_hTarget, EM_REPLACESEL, TRUE, (LPARAM)rep);
    g_compStart += (commit ? 1 : 0);
    g_compLen = preedit ? 1 : 0;
    if (g_compLen > 0) SendMessageW(g_hTarget, EM_SETSEL, g_compStart, g_compStart + g_compLen);
    else { int c = g_compStart; SendMessageW(g_hTarget, EM_SETSEL, c, c); }
}

// 키다운 처리(테스트 모드). true=소비(에디트 기본처리 생략), false=에디트에 위임.
// 오토마타가 쓸 자판: 시험칸에서 "이 파일로 시험" 중이면 그 파일 자판, 아니면 IME 의 현재 자판.
static LayoutConfig *ActiveLayout(void) {
    if (g_hTarget == g_hTry && g_tryFile && g_fileLayout.name) return &g_fileLayout;
    return Config_GetCurrentLayout(&g_config);
}

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
    LayoutConfig *L = ActiveLayout();
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
    const bool isTry = (h == g_hTry);
    const bool automata = isTry || g_testMode;   // 시험칸은 언제나 오토마타
    WNDPROC orig = isTry ? g_tryOrigProc : g_editOrigProc;
    if (m == WM_SETFOCUS && g_hTarget != h) { g_hTarget = h; g_compLen = 0; Fsm_Init(&g_fsm); Chord_Init(&g_chord); }
    if (automata && (m == WM_KEYDOWN || m == WM_SYSKEYDOWN)) {
        g_hTarget = h;
        if (AutomataKeyDown((UINT)w, l)) return 0;
        if ((UINT)w == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
            SendMessageW(h, EM_SETSEL, 0, (LPARAM)-1);
            return 0;
        }
        MSG mm = { h, m, w, l, 0, { 0, 0 } };
        TranslateMessage(&mm);
    } else if (automata && (m == WM_KEYUP || m == WM_SYSKEYUP)) {
        g_hTarget = h;
        LayoutConfig *L = ActiveLayout();
        if (L && (L->type == LAYOUT_TYPE_KOREAN_FSM || L->type == LAYOUT_TYPE_HANGUL_CUSTOM)) {
            const HangulLayout *hl = (const HangulLayout*)L->pHangulLayout;
            if (hl && hl->moachigi && (UINT)w < 256 && g_chord.keyDown[(UINT)w]) {
                ChordResult cr = Chord_KeyUp(&g_chord, (UINT)w);
                if (cr.commit) { ApplyResult(cr.commit, 0); ResetComp(); }
                return 0;
            }
        }
    }
    return CallWindowProcW(orig, h, m, w, l);
}

static void Relayout(void) {
    if (!g_hMain) return;
    RECT rc; GetClientRect(g_hMain, &rc);
    MoveWindow(g_hStatus, S(10), S(8), rc.right - S(20), S(22), TRUE);
    MoveWindow(g_hEdit, S(10), S(36), rc.right - S(20), rc.bottom - S(46) - S(40), TRUE);
    MoveWindow(g_hTryLabel, S(10), rc.bottom - S(40) + S(6), S(150), S(22), TRUE);
    MoveWindow(g_hTry, S(160), rc.bottom - S(40), rc.right - S(170), S(30), TRUE);
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

// 현재 에디터 내용을 임시 .jmt 로 저장해 검증한다 (RFC-0011 P1·P5). 임시 파일은 가능하면 **편집 중인 파일과
// 같은 폴더**에 둔다 — 그래야 `Extends = ./base.jmt`·`Include` 의 상대 경로가 실제와 같게 풀린다.
// keep != NULL 이면 성공한 자판을 넘겨준다(시험칸용). 보고 창은 show 일 때만.
static bool CheckEditor(LayoutConfig *keep, bool show, const wchar_t *title) {
    wchar_t tmp[MAX_PATH] = L"";
    bool wrote = false;
    if (g_curFile[0]) {
        wchar_t dir[MAX_PATH]; wcsncpy(dir, g_curFile, MAX_PATH - 1); dir[MAX_PATH - 1] = L'\0';
        wchar_t *slash = wcsrchr(dir, L'\\');
        if (slash) { *slash = L'\0'; _snwprintf(tmp, MAX_PATH, L"%ls\\~jamotong-check.jmt", dir); tmp[MAX_PATH - 1] = L'\0'; wrote = WriteFileUtf8FromEdit(tmp); }
    }
    if (!wrote) {
        wchar_t tmpDir[MAX_PATH];
        if (!GetTempPathW(MAX_PATH, tmpDir)) { MessageBoxW(g_hMain, L"No temp path.", title, MB_ICONERROR); return false; }
        _snwprintf(tmp, MAX_PATH, L"%lsjamotong_validate.jmt", tmpDir); tmp[MAX_PATH - 1] = L'\0';
        if (!WriteFileUtf8FromEdit(tmp)) { MessageBoxW(g_hMain, L"Could not write a temporary file.", title, MB_ICONERROR); return false; }
    }
    LayoutConfig lc; memset(&lc, 0, sizeof(lc));
    static KlayDiag diag;
    KlayMeta meta;
    bool ok = Klay_LoadEx(tmp, &lc, &diag, &meta);
    DeleteFileW(tmp);
    const wchar_t *fname = g_curFile[0] ? (wcsrchr(g_curFile, L'\\') ? wcsrchr(g_curFile, L'\\') + 1 : g_curFile) : L"(editor)";
    static wchar_t body[6144], msg[6400];
    Klay_DiagFormat(&diag, fname, body, 6144);
    if (ok) {
        const wchar_t *type = lc.type == LAYOUT_TYPE_STATIC_MAP ? L"static" : lc.type == LAYOUT_TYPE_CHORD ? L"chord" : L"hangul";
        _snwprintf(msg, 6400, L"OK - loads as a valid %ls layout.\nName: %ls%ls%ls\n\n%ls",
                   type, lc.name ? lc.name : L"?", meta.version[0] ? L"   Version: " : L"", meta.version, body);
        msg[6399] = L'\0';
        if (show) MessageBoxW(g_hMain, msg, title, diag.warnings ? MB_ICONWARNING : MB_ICONINFORMATION);
        if (keep) *keep = lc; else Config_FreeLayoutResources(&lc);
    } else {
        _snwprintf(msg, 6400, L"Invalid layout.\n\n%ls", body[0] ? body : L"empty or unreadable");
        msg[6399] = L'\0';
        MessageBoxW(g_hMain, msg, title, MB_ICONERROR);
    }
    SetFocus(g_hEdit);
    return ok;
}

static void ValidateCurrent(void) { CheckEditor(NULL, true, L"Validate"); }

static void CliCollect(const wchar_t *text, void *ctx) {   // 명령 출력을 버퍼에 모은다 (메시지 창용)
    wchar_t *b = (wchar_t*)ctx;
    size_t n = wcslen(b);
    if (n + 1 < 4096) { wcsncpy(b + n, text, 4096 - n - 1); b[4095] = L'\0'; }
}

// Tools ▸ Try this file: 편집 중인 내용을 자판으로 읽어 아래 시험칸에서 쳐 본다 (설치 전 확인).
static void TryThisFile(void) {
    LayoutConfig lc;
    if (!CheckEditor(&lc, false, L"Try this file")) return;
    if (lc.type == LAYOUT_TYPE_CHORD) {
        Config_FreeLayoutResources(&lc);
        MessageBoxW(g_hMain, L"Chord layouts send real key events and cannot be tried in this box.\n"
                             L"Install the layout and try it in any application.", L"Try this file", MB_ICONINFORMATION);
        return;
    }
    if (g_fileLayout.name) Config_FreeLayoutResources(&g_fileLayout);
    g_fileLayout = lc;
    g_tryFile = true;
    wchar_t lbl[80];
    _snwprintf(lbl, 80, L"Try [%ls]:", lc.name ? lc.name : L"file"); lbl[79] = L'\0';
    SetWindowTextW(g_hTryLabel, lbl);
    SetWindowTextW(g_hTry, L"");
    g_hTarget = g_hTry; g_compLen = 0; Fsm_Init(&g_fsm); Chord_Init(&g_chord);
    SetFocus(g_hTry);
}

// File ▸ New from built-in / New derived layout: 내장 자판을 편집기로 (RFC-0011 P5·P4).
static void NewFromBuiltin(const wchar_t *name, bool derive) {
    static wchar_t text[16384];
    wchar_t why[200];
    if (!Klay_BuiltinText(name, text, 16384, why, 200)) { MessageBoxW(g_hMain, why, L"New layout", MB_ICONERROR); return; }
    if (derive) {
        const wchar_t *type = (!wcscmp(name, L"ko_3bul")) ? L"hangul" : L"static";
        _snwprintf(text, 16384,
            L"# A layout derived from the built-in %ls - write only what changes.\n"
            L"#   Key x = C0     redefine a key (the later line wins)\n"
            L"#   Key x = -      remove an inherited key\n"
            L"FormatVersion = 2\nType = %ls\nExtends = @%ls\nName = my_%ls\nAbbrev = MY\n",
            name, type, name, name);
        text[16383] = L'\0';
    }
    // 편집기는 \r\n 을 줄바꿈으로 그린다
    static wchar_t crlf[20000];
    size_t o = 0;
    for (size_t i = 0; text[i] && o + 2 < 20000; i++) { if (text[i] == L'\n') crlf[o++] = L'\r'; crlf[o++] = text[i]; }
    crlf[o] = L'\0';
    SetEditText(crlf);
    g_curFile[0] = L'\0';
    SetTestMode(false);
}

// File ▸ Export expanded: Extends/Include 를 푼 자립 파일로 저장 (--expand 와 같은 코드).
static void ExportExpanded(void) {
    if (!g_curFile[0]) { MessageBoxW(g_hMain, L"Save the layout first - relative Extends/Include paths need its folder.", L"Export expanded", MB_ICONINFORMATION); return; }
    wchar_t out[MAX_PATH] = L"";
    if (!BrowseFile(true, out, MAX_PATH)) return;
    WriteFileUtf8FromEdit(g_curFile);   // 편집 중인 내용을 원본에 먼저 저장
    const wchar_t *argv[] = { L"jamotong", L"--expand", g_curFile, L"-o", out };
    static wchar_t log[4096]; log[0] = L'\0';
    int rc = KlayCli_Run(5, argv, CliCollect, log);
    MessageBoxW(g_hMain, log[0] ? log : (rc == 0 ? L"Done." : L"Failed."), L"Export expanded", rc == 0 ? MB_ICONINFORMATION : MB_ICONERROR);
}

// File ▸ Install to my layouts: 검증한 뒤 %APPDATA%\Jamotong\layouts 에 넣는다 (원자적 복사, 덮어쓰기 확인).
static void InstallToMyLayouts(void) {
    if (!CheckEditor(NULL, false, L"Install")) return;
    if (!g_curFile[0]) { MessageBoxW(g_hMain, L"Save the layout to a file first.", L"Install", MB_ICONINFORMATION); return; }
    if (!WriteFileUtf8FromEdit(g_curFile)) { MessageBoxW(g_hMain, L"Could not save the file.", L"Install", MB_ICONERROR); return; }
    wchar_t dir[MAX_PATH], dst[MAX_PATH];
    if (!Config_UserLayoutDir(dir, MAX_PATH)) { MessageBoxW(g_hMain, L"No user layout folder.", L"Install", MB_ICONERROR); return; }
    const wchar_t *base = wcsrchr(g_curFile, L'\\'); base = base ? base + 1 : g_curFile;
    _snwprintf(dst, MAX_PATH, L"%ls\\%ls", dir, base); dst[MAX_PATH - 1] = L'\0';
    if (!_wcsicmp(dst, g_curFile)) { MessageBoxW(g_hMain, L"This file is already in your layout folder.", L"Install", MB_ICONINFORMATION); return; }
    if (GetFileAttributesW(dst) != INVALID_FILE_ATTRIBUTES &&
        MessageBoxW(g_hMain, L"A layout with this file name is already installed. Replace it?", L"Install", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    if (!Config_CopyFileAtomic(g_curFile, dst)) { MessageBoxW(g_hMain, L"Could not copy the file.", L"Install", MB_ICONERROR); return; }
    MessageBoxW(g_hMain, L"Installed to %APPDATA%\\Jamotong\\layouts.\n\n"
                         L"Newly started applications list it (turned off - enable it in Settings > Layouts).",
                L"Install", MB_ICONINFORMATION);
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
        case IDM_TRYFILE:  TryThisFile(); break;
        case IDM_EXPAND:   ExportExpanded(); break;
        case IDM_INSTALL:  InstallToMyLayouts(); break;
        case IDM_NEW_3BUL: NewFromBuiltin(L"ko_3bul", false); break;
        case IDM_NEW_DV:   NewFromBuiltin(L"en_dvorak", false); break;
        case IDM_NEW_QW:   NewFromBuiltin(L"en_qwerty", false); break;
        case IDM_DER_3BUL: NewFromBuiltin(L"ko_3bul", true); break;
        case IDM_DER_DV:   NewFromBuiltin(L"en_dvorak", true); break;
        case IDM_TESTMODE: SetTestMode(!g_testMode); break;
        case IDM_ABOUT:    ShowAbout(); break;
    }
    if (id != IDM_SETTINGS && id != IDM_ABOUT && id != IDM_VALIDATE && id != IDM_TRYFILE) SetFocus(g_hEdit);
}

static HMENU BuildMenu(void) {
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu(), edit = CreatePopupMenu(),
          lay = CreatePopupMenu(), tools = CreatePopupMenu(), help = CreatePopupMenu();
    HMENU nb = CreatePopupMenu(), nd = CreatePopupMenu();
    AppendMenuW(nb, MF_STRING, IDM_NEW_3BUL, L"Sebeolsik final (ko_3bul)");
    AppendMenuW(nb, MF_STRING, IDM_NEW_DV,   L"Dvorak (en_dvorak)");
    AppendMenuW(nb, MF_STRING, IDM_NEW_QW,   L"QWERTY (en_qwerty)");
    AppendMenuW(nd, MF_STRING, IDM_DER_3BUL, L"from Sebeolsik final (Extends = @ko_3bul)");
    AppendMenuW(nd, MF_STRING, IDM_DER_DV,   L"from Dvorak (Extends = @en_dvorak)");
    AppendMenuW(file, MF_POPUP, (UINT_PTR)nb, L"New &copy of a built-in layout");
    AppendMenuW(file, MF_POPUP, (UINT_PTR)nd, L"New &derived layout");
    AppendMenuW(file, MF_STRING, IDM_OPEN,   L"&Open .jmt...\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDM_SAVE,   L"&Save\tCtrl+S");
    AppendMenuW(file, MF_STRING, IDM_SAVEAS, L"Save &As...");
    AppendMenuW(file, MF_STRING, IDM_EXPAND, L"&Export expanded (no Extends)...");
    AppendMenuW(file, MF_STRING, IDM_INSTALL, L"&Install to my layouts");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, IDM_EXIT,   L"E&xit");
    AppendMenuW(edit, MF_STRING, IDM_SELALL, L"Select &All\tCtrl+A");
    AppendMenuW(edit, MF_STRING, IDM_CLEAR,  L"&Clear");
    AppendMenuW(lay,  MF_STRING, IDM_NEXT,     L"&Next layout");
    AppendMenuW(lay,  MF_STRING, IDM_SETTINGS, L"&Settings...");
    AppendMenuW(tools, MF_STRING, IDM_VALIDATE, L"&Validate layout");
    AppendMenuW(tools, MF_STRING, IDM_TRYFILE,  L"T&ry this file (in the box below)");
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

// ── RFC-0011 P3: `jamotong.exe --check/--export/--expand` ─────────────────────────────
// GUI 서브시스템 exe 라 콘솔이 없다 — 부모(cmd/PowerShell) 콘솔에 붙어 쓴다. 출력이 파일·파이프로
// 돌려져 있으면 그 핸들에 UTF-8 로 쓴다(콘솔이면 WriteConsoleW 로 한글이 깨지지 않게).
static HANDLE g_cliOut = NULL;
static void CliOut(const wchar_t *text, void *ctx) {
    (void)ctx;
    if (!g_cliOut || g_cliOut == INVALID_HANDLE_VALUE) return;
    DWORD mode, n;
    if (GetConsoleMode(g_cliOut, &mode)) { WriteConsoleW(g_cliOut, text, (DWORD)wcslen(text), &n, NULL); return; }
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (bytes <= 1) return;
    char *buf = (char*)malloc((size_t)bytes);
    if (!buf) return;
    WideCharToMultiByte(CP_UTF8, 0, text, -1, buf, bytes, NULL, NULL);
    WriteFile(g_cliOut, buf, (DWORD)(bytes - 1), &n, NULL);
    free(buf);
}
static int RunCli(void) {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 2;
    if (!KlayCli_IsCommand(argc, (const wchar_t *const *)argv)) { LocalFree(argv); return -1; }
    g_cliOut = GetStdHandle(STD_OUTPUT_HANDLE);   // 돌려진 출력이 있으면 그것을 쓴다
    if (!g_cliOut || g_cliOut == INVALID_HANDLE_VALUE || GetFileType(g_cliOut) == FILE_TYPE_UNKNOWN) {
        if (AttachConsole(ATTACH_PARENT_PROCESS))
            g_cliOut = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    }
    int rc = KlayCli_Run(argc, (const wchar_t *const *)argv, CliOut, NULL);
    LocalFree(argv);
    return rc;
}

int WINAPI wWinMain(HINSTANCE hI, HINSTANCE hP, PWSTR cmd, int show) {
    (void)hP;
    g_hInst = hI;
    if (cmd && wcsstr(cmd, L"/uninstallime")) return UninstallIme();   // 구버전 IMM32 잔재 정리 전용
    // RFC-0015: UWP 호스트 안의 TIP 은 창을 못 띄운다 → 이 프로세스가 대신 그려 주는 모드.
    // 창도 트레이 아이콘도 없이 파이프만 듣는다. 세션당 하나(뮤텍스)."
    if (cmd && wcsstr(cmd, L"--ui-server")) return UiServer_Run(hI);
    if (cmd && (wcsstr(cmd, L"--check") || wcsstr(cmd, L"--export") || wcsstr(cmd, L"--expand"))) {
        int rc = RunCli();   // 창·트레이 없이 명령만 하고 끝난다
        if (rc >= 0) return rc;
    }

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
    wc.hIcon = LoadIconW(hI, MAKEINTRESOURCEW(100));   // jamotong_app.rc 의 아이콘 (W2-08: 예전엔 없는 ID 1)
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
    g_hTryLabel = CreateWindowW(L"STATIC", L"Try (current layout):", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                                S(10), S(516), S(150), S(22), g_hMain, NULL, hI, NULL);
    g_hTry = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                             S(160), S(510), S(640), S(30), g_hMain, NULL, hI, NULL);
    SendMessageW(g_hTryLabel, WM_SETFONT, (WPARAM)s_font, TRUE);
    SendMessageW(g_hTry, WM_SETFONT, (WPARAM)s_editFont, TRUE);
    g_tryOrigProc = (WNDPROC)SetWindowLongPtrW(g_hTry, GWLP_WNDPROC, (LONG_PTR)EditProc);
    g_hTarget = g_hEdit;

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
