// jamotest.c — Jamotong 자판/오토마타 독립 테스트 하네스 (TSF 없이 순수 로직 검증)
//
// TSF(msctf) 계층을 빼고 핵심 입력 로직(config/layout/fsm/hangul_layout/chord/klay)만 링크한
// Win32 GUI. 실제 편집 감각을 위해 멀티라인 EDIT 컨트롤에 직접 입력한다. 에디트를 서브클래스해
// KES_OnKeyDown과 같은 자판 디스패치를 돌리고, 결과(확정/조합)를 조합 구간 치환으로 반영한다.
//  - 조합 중 글자는 선택 하이라이트로 표시(=IME의 preedit), 다음 키에서 치환/확정된다.
//  - 백스페이스: 조합 중이면 마지막 자모 제거, 아니면 에디트가 평범히 지움.
//  - 자판 순환: Right Alt / 한/영(VK_HANGUL) / Shift+Space / [다음 자판] 버튼 (좌우 구분 매칭).
//  - 영문 패스스루는 에디트가 직접 입력, 정적맵/한글FSM/모아치기는 오토마타가 처리.
//    (CHORD/ARTSEY는 SendInput 기반 → 실제 앱에서 확인)

#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <msctf.h>     // 트레이 모니터링: 활성 입력기 프로파일 조회
#include <shellapi.h>  // Shell_NotifyIconW (시스템 트레이 아이콘)
#include <imm.h>       // (구버전 IMM32 잔재 정리용 /uninstallime)
#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "layout.h"
#include "fsm.h"
#include "hangul_layout.h"
#include "chord.h"
#include "chord_layout.h"
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
static HFONT g_uiFont = NULL;
// 오너드로 버튼 3개 (자식 BUTTON 대신 부모 창이 직접 그리고 클릭 처리 — WM_COMMAND 라우팅 의존 제거)
static RECT g_btnRect[3];
static const wchar_t *g_btnLabel[3] = { L"Next layout", L"Settings", L"Clear" };

#define ID_NEXT 1001
#define ID_SETTINGS 1002
#define ID_CLEAR 1003

// 물리 키 vk+shift → US-QWERTY 산출 문자
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
    wchar_t s[160];
    wsprintfW(s, L"Layout: %s    (switch: Right Alt / Hangul key / Shift+Space / [Next layout])",
              (L && L->name) ? L->name : L"?");
    SetWindowTextW(g_hStatus, s);
    // 창 제목에도 현재 자판명을 표시 — "다음 자판" 버튼이 동작하면 제목이 바뀌므로 클릭 도달 진단도 된다.
    wchar_t t[128];
    wsprintfW(t, L"Jamotong Input Test - [%s]", (L && L->name) ? L->name : L"?");
    if (g_hMain) SetWindowTextW(g_hMain, t);
}

static void ResetComp(void) { g_compLen = 0; }

// 자모 결과를 에디트에 반영: 조합 구간을 (commit+preedit)로 치환, 새 조합 구간=preedit(선택 하이라이트).
// preedit==0 이면 조합 구간을 지운다(백스페이스로 비움에도 재사용).
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
    if (g_compLen > 0) SendMessageW(g_hEdit, EM_SETSEL, g_compStart, g_compStart + g_compLen); // 조합 하이라이트
    else { int c = g_compStart; SendMessageW(g_hEdit, EM_SETSEL, c, c); }
}

// 키다운 처리. true=우리가 소비(에디트 기본처리 생략), false=에디트에 위임(패스스루·스페이스 등).
static bool AutomataKeyDown(UINT vk, LPARAM lParam) {
    UINT rvk = Config_ResolveVK(vk, lParam);
    if (Config_IsShortcut(&g_config, SC_FN_ROTATE, rvk, Config_CurrentMods())) {
        ResetComp();   // 조합은 이미 텍스트에 있으므로 확정 처리만
        Fsm_Init(&g_fsm); Chord_Init(&g_chord);
        Config_RotateLayout(&g_config); UpdateStatus();
        return true;
    }
    // Ctrl/Alt/Win 조합(Ctrl+A/C/V 등 앱 단축키)은 소비하지 않고 에디트에 위임. 조합 중이면 먼저 확정.
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
            return false;                       // 에디트가 직접 입력(WM_CHAR)
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
                return false;                   // 조합 없음 → 에디트가 지움
            }
            wchar_t kc = GetQwertyChar(vk, shift);
            LayoutResult lr = { JAMO_NONE, 0 };
            if (kc > 0 && kc < 128) lr = hl ? hl->keymap[(int)kc] : Layout_MapKeyToJamo(kc, L->kbdVariant);
            if (lr.type != JAMO_NONE) {
                FsmResult r = Fsm_ProcessKey(&g_fsm, kc, L->kbdVariant, hl);
                ApplyResult(r.commitChar, r.preeditChar);
                return true;
            }
            // 비자모 키: 모디파이어가 아니면 조합 확정(부분 상태도 Fsm_Flush로 올바르게) 후 에디트에 위임
            if (!IsModifierOrLock(vk) && g_fsm.state != STATE_EMPTY) {
                ApplyResult(Fsm_Flush(&g_fsm), 0); ResetComp();
            }
            return false;   // 에디트가 스페이스/기호/방향키 입력·이동 처리
        }
        case LAYOUT_TYPE_CHORD:
            return false;   // ARTSEY류 SendInput 기반 — 하네스 미지원
        default:
            return false;
    }
}

static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN || m == WM_SYSKEYDOWN) {
        // 소비한 키는 WM_CHAR를 아예 만들지 않는다(이중 입력 원천 차단, 플래그 경쟁 없음).
        // 소비하지 않은 키(패스스루·스페이스·기호)만 이 자리에서 WM_CHAR로 번역해 에디트가 입력.
        if (AutomataKeyDown((UINT)w, l)) return 0;
        // 클래식 EDIT은 Ctrl+A(전체선택)를 기본 지원하지 않으므로 직접 처리 (C/V/X/Z는 내장)
        if ((UINT)w == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
            SendMessageW(h, EM_SETSEL, 0, (LPARAM)-1);
            return 0;
        }
        MSG mm = { h, m, w, l, 0, { 0, 0 } };
        TranslateMessage(&mm);
    } else if (m == WM_KEYUP || m == WM_SYSKEYUP) {
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
    RECT rc; GetClientRect(g_hMain, &rc);
    MoveWindow(g_hStatus, S(12), S(44), rc.right - S(24), S(24), TRUE);
    MoveWindow(g_hEdit, S(12), S(74), rc.right - S(24), rc.bottom - S(86), TRUE);
}

static void InitButtons(void) {
    int x = S(12), y = S(8), ht = S(32);
    int w[3] = { S(170), S(150), S(90) };
    for (int i = 0; i < 3; i++) { SetRect(&g_btnRect[i], x, y, x + w[i], y + ht); x += w[i] + S(8); }
}

static void DoCommand(int idx) {
    switch (idx) {
        case 0: ResetComp(); Fsm_Init(&g_fsm); Chord_Init(&g_chord); Config_RotateLayout(&g_config); UpdateStatus(); break;
        case 1: SettingsUI_Show(&g_config); break;
        case 2: SetWindowTextW(g_hEdit, L""); ResetComp(); Fsm_Init(&g_fsm); Chord_Init(&g_chord); break;
    }
    SetFocus(g_hEdit);
    InvalidateRect(g_hMain, NULL, FALSE);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_LBUTTONDOWN: {
            POINT pt = { (short)LOWORD(l), (short)HIWORD(l) };
            for (int i = 0; i < 3; i++) if (PtInRect(&g_btnRect[i], pt)) { DoCommand(i); break; }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
            HFONT of = (HFONT)SelectObject(hdc, g_uiFont ? g_uiFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetBkMode(hdc, TRANSPARENT);
            for (int i = 0; i < 3; i++) {
                DrawFrameControl(hdc, &g_btnRect[i], DFC_BUTTON, DFCS_BUTTONPUSH);   // 네이티브 푸시버튼 모양
                DrawTextW(hdc, g_btnLabel[i], -1, &g_btnRect[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            SelectObject(hdc, of);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_SIZE: Relayout(); return 0;
        case WM_SETFOCUS: SetFocus(g_hEdit); return 0;
        case WM_DESTROY: g_hMain = NULL; return 0;   // 에디터만 닫힘 — 앱 종료는 트레이 메뉴 '종료'
    }
    return DefWindowProcW(h, m, w, l);
}

// ── 구버전 IMM32 IME(.ime) 잔재 정리 (jamotong.exe /uninstallime — uninstall.bat이 호출) ──
//   IMM32 경로는 폐기됨(Win11 봉쇄). 과거 버전이 설치했던 레지스트리/파일만 조용히 제거한다.
static int UninstallIme(void) {
    // Keyboard Layouts 에서 IME File==jamotong.ime 인 KLID 제거 + 파일 삭제.
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
    return 0;   // 조용히 완료 — uninstall.bat의 구버전 잔재 정리 단계에서 호출됨
}

// ─────────────────────────────────────────────────────────────────────────────
// 시스템 트레이 모니터링 (기본 실행 모드)
//   - 자모통 IME 활성 여부(TSF 프로파일 조회) + 현재 자판(HKCU\Software\Jamotong, DLL이 발행)을
//     1.5초 폴링해 자판 축약명을 트레이 아이콘으로 표시.
//   - 좌클릭=설정창, 우클릭=메뉴(설정/입력 테스트/정보/종료).
// ─────────────────────────────────────────────────────────────────────────────
#define WM_TRAYICON   (WM_APP + 1)
#define TRAY_TIMER_ID 1
#define IDM_TRAY_SETTINGS 101
#define IDM_TRAY_EDITOR   102
#define IDM_TRAY_ABOUT    103
#define IDM_TRAY_EXIT     104

static const CLSID kCLSID_Jamotong =   // DLL의 CLSID (활성 프로파일 판별용)
{ 0xc471bcf2, 0x343f, 0x4187, { 0xa1, 0x03, 0x24, 0x15, 0x1c, 0x3e, 0x20, 0xb9 } };

static HWND    g_hTray = NULL;
static NOTIFYICONDATAW g_nid;
static bool    g_trayAdded = false;
static bool    g_imeActive = false;
static wchar_t g_trayAbbrev[8] = L"?";
static int     g_editorShow = SW_SHOW;   // ShowEditorWindow용 (wWinMain의 show 보관)

// 자판 축약명(1~4글자)을 2x2 격자로 렌더한 트레이 아이콘. active=파란 배지, 비활성=회색.
// (langbar.c CreateAbbrevIcon과 같은 기법 — exe엔 langbar 미링크라 독립 구현)
static HICON MakeTrayIcon(const wchar_t *text, bool active) {
    int len = (int)wcslen(text); if (len < 1) { text = L"?"; len = 1; }
    if (len > 4) len = 4;
    int sz = 32, half = 16;   // 셸이 트레이 크기(16/24)로 축소
    RECT cell[4]; int fontH;
    if (len == 1)      { SetRect(&cell[0], 0, 0, sz, sz); fontH = 26; }
    else if (len == 2) { SetRect(&cell[0], 0, 0, half, sz); SetRect(&cell[1], half, 0, sz, sz); fontH = 15; }
    else if (len == 3) { SetRect(&cell[0], 0, 0, half, half); SetRect(&cell[1], half, 0, sz, half);
                         SetRect(&cell[2], 8, half, 8 + half, sz); fontH = 15; }
    else               { SetRect(&cell[0], 0, 0, half, half); SetRect(&cell[1], half, 0, sz, half);
                         SetRect(&cell[2], 0, half, half, sz); SetRect(&cell[3], half, half, sz, sz); fontH = 15; }

    HDC scr = GetDC(NULL);
    if (!scr) return NULL;
    HDC hdc = CreateCompatibleDC(scr);
    HBITMAP bmC = CreateCompatibleBitmap(scr, sz, sz);
    HBITMAP bmM = CreateBitmap(sz, sz, 1, 1, NULL);
    HICON icon = NULL;
    if (hdc && bmC && bmM) {
        HGDIOBJ ob = SelectObject(hdc, bmC);
        RECT full = { 0, 0, sz, sz };
        HBRUSH bg = CreateSolidBrush(active ? RGB(0, 92, 200) : RGB(120, 120, 120));
        FillRect(hdc, &full, bg); DeleteObject(bg);
        SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(255, 255, 255));
        HFONT hf = CreateFontW(fontH, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                               DEFAULT_PITCH, L"\xB3CB\xC6C0" /* 돋움 */);
        HGDIOBJ of = SelectObject(hdc, hf);
        for (int i = 0; i < len; i++)
            DrawTextW(hdc, &text[i], 1, &cell[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        SelectObject(hdc, of); DeleteObject(hf);
        SelectObject(hdc, ob);
        HDC hdcM = CreateCompatibleDC(scr);
        if (hdcM) {
            HGDIOBJ om = SelectObject(hdcM, bmM);
            PatBlt(hdcM, 0, 0, sz, sz, BLACKNESS);
            SelectObject(hdcM, om); DeleteDC(hdcM);
            ICONINFO ii = { 0 }; ii.fIcon = TRUE; ii.hbmColor = bmC; ii.hbmMask = bmM;
            icon = CreateIconIndirect(&ii);
        }
    }
    if (bmC) DeleteObject(bmC);
    if (bmM) DeleteObject(bmM);
    if (hdc) DeleteDC(hdc);
    ReleaseDC(NULL, scr);
    return icon;
}

// 자모통이 현재 활성 입력기인가 (TSF 활성 프로파일 조회)
static bool QueryImeActive(void) {
    bool active = false;
    ITfInputProcessorProfiles *pp = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_ITfInputProcessorProfiles, (void**)&pp)) && pp) {
        LANGID lid = 0; GUID prof;
        if (pp->lpVtbl->GetActiveLanguageProfile(pp, &kCLSID_Jamotong, &lid, &prof) == S_OK)
            active = true;
        pp->lpVtbl->Release(pp);
    }
    return active;
}

// DLL이 발행한 현재 자판 축약명 읽기 (HKCU\Software\Jamotong\CurrentAbbrev)
static void ReadPublishedAbbrev(wchar_t *out, int cch) {
    lstrcpynW(out, L"?", cch);
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Jamotong", 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        wchar_t buf[16]; DWORD sz = sizeof(buf), type = 0;
        if (RegQueryValueExW(hk, L"CurrentAbbrev", NULL, &type, (BYTE*)buf, &sz) == ERROR_SUCCESS
            && type == REG_SZ && sz >= sizeof(wchar_t)) {
            buf[15] = L'\0';
            lstrcpynW(out, buf, cch);
        }
        RegCloseKey(hk);
    }
}

// 상태 폴링 → 변화 시 아이콘/툴팁 갱신
static void TrayPoll(bool force) {
    bool act = QueryImeActive();
    wchar_t ab[8]; ReadPublishedAbbrev(ab, 8);
    if (!force && act == g_imeActive && wcscmp(ab, g_trayAbbrev) == 0) return;
    g_imeActive = act;
    lstrcpynW(g_trayAbbrev, ab, 8);

    HICON icon = MakeTrayIcon(g_trayAbbrev, g_imeActive);
    if (!icon) return;
    HICON old = g_nid.hIcon;
    g_nid.hIcon = icon;
    swprintf(g_nid.szTip, 128, L"Jamotong: %ls (%ls)",
             g_imeActive ? L"active" : L"inactive", g_trayAbbrev);
    Shell_NotifyIconW(g_trayAdded ? NIM_MODIFY : NIM_ADD, &g_nid);
    g_trayAdded = true;
    if (old) DestroyIcon(old);
}

static void ShowEditorWindow(void);   // 아래 정의

static void ShowAbout(HWND owner) {
    wchar_t msg[512];
    swprintf(msg, 512,
        L"Jamotong IME  %ls\n\n"
        L"Korean IME for Windows in pure C23 + WinAPI (TSF).\n"
        L"Author: %ls\n"
        L"Homepage: %ls\n\n"
        L"Tray icon = current layout (blue = active, gray = inactive)",
        JAMOTONG_VERSION, JAMOTONG_AUTHOR, JAMOTONG_HOMEPAGE);
    MessageBoxW(owner, msg, L"About Jamotong IME", MB_OK | MB_ICONINFORMATION);
}

static LRESULT CALLBACK TrayWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_TRAYICON:
            if (LOWORD(l) == WM_LBUTTONUP) {          // 좌클릭 = 설정창
                SettingsUI_Show(&g_config);
            } else if (LOWORD(l) == WM_RBUTTONUP) {   // 우클릭 = 메뉴
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, IDM_TRAY_SETTINGS, L"&Settings...");
                AppendMenuW(menu, MF_STRING, IDM_TRAY_EDITOR,   L"Input &Test");
                AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(menu, MF_STRING, IDM_TRAY_ABOUT,    L"&About...");
                AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT,     L"E&xit");
                POINT pt; GetCursorPos(&pt);
                SetForegroundWindow(h);   // 메뉴 밖 클릭 시 자동 닫힘 보장 (MSDN 관례)
                int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                         pt.x, pt.y, 0, h, NULL);
                DestroyMenu(menu);
                switch (cmd) {
                    case IDM_TRAY_SETTINGS: SettingsUI_Show(&g_config); break;
                    case IDM_TRAY_EDITOR:   ShowEditorWindow(); break;
                    case IDM_TRAY_ABOUT:    ShowAbout(h); break;
                    case IDM_TRAY_EXIT:     DestroyWindow(h); break;
                }
            }
            return 0;
        case WM_TIMER:
            if (w == TRAY_TIMER_ID) TrayPoll(false);
            return 0;
        case WM_DESTROY:
            KillTimer(h, TRAY_TIMER_ID);
            if (g_trayAdded) { Shell_NotifyIconW(NIM_DELETE, &g_nid); g_trayAdded = false; }
            if (g_nid.hIcon) { DestroyIcon(g_nid.hIcon); g_nid.hIcon = NULL; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// 입력 테스트(자판 검증) 에디터 창 — 트레이 메뉴에서 연다. 닫아도 앱은 트레이에 상주.
static void ShowEditorWindow(void) {
    if (g_hMain) { ShowWindow(g_hMain, SW_SHOW); SetForegroundWindow(g_hMain); return; }

    UINT (WINAPI *pGetDpi)(HWND) = (void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");

    static bool s_reg = false;
    if (!s_reg) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"JamotestMain";
        RegisterClassW(&wc); s_reg = true;
    }

    g_hMain = CreateWindowW(L"JamotestMain", L"Jamotong Input Test",
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            760, 520, NULL, NULL, g_hInst, NULL);
    if (!g_hMain) return;
    if (pGetDpi) g_dpi = (int)pGetDpi(g_hMain);
    SetWindowPos(g_hMain, NULL, 0, 0, S(760), S(520), SWP_NOMOVE | SWP_NOZORDER);

    static HFONT s_font = NULL, s_editFont = NULL;   // 재오픈 시 재사용 (핸들 누수 방지)
    if (!s_font)
        s_font = CreateFontW(-S(16), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!s_editFont)
        s_editFont = CreateFontW(-S(24), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Malgun Gothic");
    g_uiFont = s_font;
    InitButtons();

    g_hStatus = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                              S(12), S(44), S(700), S(24), g_hMain, NULL, g_hInst, NULL);
    g_hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                              S(12), S(74), S(720), S(400), g_hMain, NULL, g_hInst, NULL);
    SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)s_font, TRUE);
    SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)s_editFont, TRUE);
    g_editOrigProc = (WNDPROC)SetWindowLongPtrW(g_hEdit, GWLP_WNDPROC, (LONG_PTR)EditProc);

    UpdateStatus();
    Relayout();
    SetFocus(g_hEdit);
    ShowWindow(g_hMain, g_editorShow);
}

int WINAPI wWinMain(HINSTANCE hI, HINSTANCE hP, PWSTR cmd, int show) {
    (void)hP;
    g_hInst = hI;
    if (cmd && wcsstr(cmd, L"/uninstallime")) return UninstallIme();   // 구버전 IMM32 잔재 정리 전용

    // DPI 인식 (Per-Monitor V2)
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    BOOL (WINAPI *pSetCtx)(HANDLE) = (void*)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (pSetCtx) pSetCtx((HANDLE)-4);

    CoInitialize(NULL);   // 활성 프로파일 조회(TSF COM)용
    InitializeCriticalSection(&g_configLock);
    Config_LoadDefault(&g_config);
    {   // 저장된 사용자 설정 병합 로드 (설정창이 이 실제 설정을 편집·저장)
        wchar_t cfgPath[MAX_PATH];
        if (Config_UserPath(cfgPath, MAX_PATH)) Config_LoadFromFile(&g_config, cfgPath);
    }
    Fsm_Init(&g_fsm);
    Chord_Init(&g_chord);
    g_editorShow = show;

    // 트레이 메시지 창 + 아이콘 + 폴링 타이머
    WNDCLASSW twc = {0};
    twc.lpfnWndProc = TrayWndProc;
    twc.hInstance = hI;
    twc.lpszClassName = L"JamotongTrayWnd";
    RegisterClassW(&twc);
    g_hTray = CreateWindowW(L"JamotongTrayWnd", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hI, NULL);
    if (!g_hTray) { CoUninitialize(); return 1; }

    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hTray;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    TrayPoll(true);                          // 초기 아이콘 등록
    SetTimer(g_hTray, TRAY_TIMER_ID, 1500, NULL);

    if (cmd && wcsstr(cmd, L"/editor")) ShowEditorWindow();   // 바로 에디터도 열기 (선택)

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        DispatchMessageW(&msg);
    }
    SettingsUI_Shutdown();   // 설정창 스레드 정리
    CoUninitialize();
    return 0;
}
