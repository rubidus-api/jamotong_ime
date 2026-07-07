// ime_main.c — IMM32 IME 진입점 + 필수 export.
//   .ime(=DLL)로 빌드되어 키보드 레이아웃(HKL)으로 등록된다. 시스템이 Ime* 를 호출.
//   조합/오토마타는 기존 fsm.c·layout.c·config.c 재사용. 조합 처리 본체는 ime_process.c.
#include "immdev_min.h"
#include "ime_internal.h"
#include "../config.h"
#include <stdio.h>

HINSTANCE g_hImeInst = NULL;
JamotongConfig g_imeConfig;             // 전역(프로세스당) 설정
static bool s_configLoaded = false;
CRITICAL_SECTION g_imeLock;             // 설정/전역 보호
// 공유 소스(config.c·plugin_loader.c)가 참조하는 전역 — TSF DLL은 dllmain.c에서 정의하나
// .ime는 dllmain.c 미포함이므로 여기서 정의한다.
CRITICAL_SECTION g_configLock;          // config.c: 플러그인/레이아웃 직렬화
HINSTANCE g_hInst = NULL;               // plugin_loader.c: 모듈 옆 플러그인 탐색

static const WCHAR kUIClass[] = L"JamotongImeUIClass";

// 전역 설정 로드(1회). jamotong.exe가 저장한 %APPDATA%\Jamotong\config.ini 를 읽는다.
void Ime_EnsureConfig(void) {
    EnterCriticalSection(&g_imeLock);
    if (!s_configLoaded) {
        s_configLoaded = true;
        Config_LoadDefault(&g_imeConfig);
        wchar_t path[MAX_PATH];
        if (Config_UserPath(path, MAX_PATH)) Config_LoadFromFile(&g_imeConfig, path);   // 병합(리소스 유지)
        Ime_HanjaInit();            // 후보창 클래스 등록 + 한자사전 로드(1회)
    }
    LeaveCriticalSection(&g_imeLock);
}

// ── 최소 IME UI 윈도 (인라인 조합만 — 앱이 조합문자열을 직접 렌더하므로 UI는 비워둠) ──
static LRESULT CALLBACK UIWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(hWnd, msg, wp, lp);
}
static void RegisterUIClass(void) {
    WNDCLASSW wc = {0};
    wc.style = CS_IME;
    wc.lpfnWndProc = UIWndProc;
    wc.hInstance = g_hImeInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.cbWndExtra = 2 * sizeof(LONG_PTR);   // IME UI 관례
    wc.lpszClassName = kUIClass;
    RegisterClassW(&wc);
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_hImeInst = hInst;
            g_hInst = hInst;
            DisableThreadLibraryCalls(hInst);
            InitializeCriticalSection(&g_imeLock);
            InitializeCriticalSection(&g_configLock);
            RegisterUIClass();
            break;
        case DLL_PROCESS_DETACH:
            DeleteCriticalSection(&g_imeLock);
            DeleteCriticalSection(&g_configLock);
            break;
    }
    return TRUE;
}

// ── 필수 export ──────────────────────────────────────────────────────────────────

// IME 능력·성질 보고 + private 데이터 크기·UI 클래스.
BOOL WINAPI ImeInquire(LPIMEINFO lpImeInfo, LPWSTR lpszUIClass, DWORD dwSystemInfoFlags) {
    (void)dwSystemInfoFlags;
    if (!lpImeInfo) return FALSE;
    Ime_EnsureConfig();
    lpImeInfo->dwPrivateDataSize = sizeof(ImePrivate);
    // 유니코드·캐럿 인라인 조합. 한글 변환모드. 후보/문장 없음(초기).
    lpImeInfo->fdwProperty       = IME_PROP_UNICODE | IME_PROP_AT_CARET | IME_PROP_KBD_CHAR_FIRST;
    lpImeInfo->fdwConversionCaps = IME_CMODE_NATIVE;
    lpImeInfo->fdwSentenceCaps   = 0;
    lpImeInfo->fdwUICaps         = 0;
    lpImeInfo->fdwSCSCaps        = SCS_CAP_COMPSTR;
    lpImeInfo->fdwSelectCaps     = 0;
    if (lpszUIClass) lstrcpyW(lpszUIClass, kUIClass);
    return TRUE;
}

// 컨텍스트 선택/해제. private(FSM) 초기화.
BOOL WINAPI ImeSelect(HIMC hIMC, BOOL fSelect) {
    if (!hIMC) return FALSE;
    Ime_EnsureConfig();
    LPINPUTCONTEXT pIC = ImmLockIMC(hIMC);
    if (!pIC) return FALSE;
    if (fSelect) {
        pIC->fOpen = TRUE;                        // 선택 시 IME 열림(활성) 상태
        pIC->fdwConversion = IME_CMODE_NATIVE;   // 선택 시 기본 한글모드 (hPrivate 유무와 무관)
        if (pIC->hPrivate) {
            ImePrivate *priv = (ImePrivate*)ImmLockIMCC(pIC->hPrivate);
            if (priv) { Ime_PrivInit(priv); ImmUnlockIMCC(pIC->hPrivate); }
        }
    }
    ImmUnlockIMC(hIMC);
    return TRUE;
}

BOOL WINAPI ImeSetActiveContext(HIMC hIMC, BOOL fFlag) { (void)hIMC; (void)fFlag; return TRUE; }

BOOL WINAPI ImeDestroy(UINT uReserved) { (void)uReserved; return TRUE; }

// 설정: jamotong.exe(독립 설정앱) 실행.
BOOL WINAPI ImeConfigure(HKL hKL, HWND hWnd, DWORD dwMode, LPVOID lpData) {
    (void)hKL; (void)dwMode; (void)lpData;
    wchar_t exe[MAX_PATH];
    if (GetModuleFileNameW(g_hImeInst, exe, MAX_PATH)) {
        wchar_t *slash = wcsrchr(exe, L'\\');
        if (slash) { wcscpy(slash + 1, L"jamotong.exe"); ShellExecuteW(hWnd, L"open", exe, NULL, NULL, SW_SHOW); return TRUE; }
    }
    return FALSE;
}

LRESULT WINAPI ImeEscape(HIMC hIMC, UINT uEscape, LPVOID lpData) {
    (void)hIMC; (void)lpData;
    if (uEscape == IME_ESC_QUERY_SUPPORT) return FALSE;   // 특수 escape 미지원
    return 0;
}

// ── 스텁 export (후속 구현) ──────────────────────────────────────────────────────
DWORD WINAPI ImeConversionList(HIMC hIMC, LPCWSTR lpSrc, LPCANDIDATELIST lpDst, DWORD dwBufLen, UINT uFlag) {
    (void)hIMC; (void)lpSrc; (void)lpDst; (void)dwBufLen; (void)uFlag; return 0;
}
BOOL WINAPI ImeSetCompositionString(HIMC hIMC, DWORD dwIndex, LPVOID lpComp, DWORD dwCompLen, LPVOID lpRead, DWORD dwReadLen) {
    (void)hIMC; (void)dwIndex; (void)lpComp; (void)dwCompLen; (void)lpRead; (void)dwReadLen; return FALSE;
}
BOOL WINAPI ImeRegisterWord(LPCWSTR lpRead, DWORD dwStyle, LPCWSTR lpStr) {
    (void)lpRead; (void)dwStyle; (void)lpStr; return FALSE;
}
BOOL WINAPI ImeUnregisterWord(LPCWSTR lpRead, DWORD dwStyle, LPCWSTR lpStr) {
    (void)lpRead; (void)dwStyle; (void)lpStr; return FALSE;
}
UINT WINAPI ImeGetRegisterWordStyle(UINT nItem, LPSTYLEBUFW lpStyleBuf) {
    (void)nItem; (void)lpStyleBuf; return 0;
}
UINT WINAPI ImeEnumRegisterWord(REGISTERWORDENUMPROCW proc, LPCWSTR lpRead, DWORD dwStyle, LPCWSTR lpStr, LPVOID lpData) {
    (void)proc; (void)lpRead; (void)dwStyle; (void)lpStr; (void)lpData; return 0;
}
DWORD WINAPI ImeGetImeMenuItems(HIMC hIMC, DWORD dwFlags, DWORD dwType, LPIMEMENUITEMINFOW lpImeParentMenu,
                                LPIMEMENUITEMINFOW lpImeMenu, DWORD dwSize) {
    (void)hIMC; (void)dwFlags; (void)dwType; (void)lpImeParentMenu; (void)lpImeMenu; (void)dwSize; return 0;
}
