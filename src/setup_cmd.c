// setup_cmd.c — `jamotong.exe --register` / `--unregister` (RFC-0019).
//   MSI 가 파일을 놓은 뒤 이것을 지연·비가장으로 부른다. `install.bat` 도 같은 경로를 쓴다.
//   여기서 하는 일은 셋뿐이다: 64비트 DLL 자기등록, 32비트 DLL 을 SysWOW64 regsvr32 로 등록,
//   그리고 자판 굽기. 로그는 설치 로그에 이어 쓴다.
#include <windows.h>
#include <objbase.h>
#include <stdbool.h>
#include <stdio.h>
#include "setup_cmd.h"
#include "jlay_build.h"
#include "config.h"

typedef HRESULT (STDAPICALLTYPE *RegProc)(void);

// 설치기는 서비스(SYSTEM) 아래에서도 돌기 때문에 %TEMP% 는 찾기 어려운 자리에 생긴다.
// 로그는 언제나 같은 곳에 남긴다: %ProgramData%\Jamotong\install.log
static void LogLine(const wchar_t *fmt, ...) {
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ProgramData", path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    wcscat_s(path, MAX_PATH, L"\\Jamotong");
    CreateDirectoryW(path, NULL);
    wcscat_s(path, MAX_PATH, L"\\install.log");
    FILE *fp = NULL;
    if (_wfopen_s(&fp, path, L"a, ccs=UTF-8") != 0 || !fp) return;
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(fp, fmt, ap);
    va_end(ap);
    fputwc(L'\n', fp);
    fclose(fp);
}

// 이 프로세스가 승격되어 있는가 — HKLM 에 못 쓰면 등록은 뜻이 없다.
static bool IsElevated(void) {
    HANDLE tok = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION el;
    DWORD cb = sizeof el;
    bool ok = GetTokenInformation(tok, TokenElevation, &el, cb, &cb) && el.TokenIsElevated;
    CloseHandle(tok);
    return ok;
}

// 이 exe 가 있는 폴더 — 설치 폴더다.
static bool OwnDir(wchar_t *out, size_t cch) {
    DWORD n = GetModuleFileNameW(NULL, out, (DWORD)cch);
    if (!n || n >= cch) return false;
    wchar_t *slash = wcsrchr(out, L'\\');
    if (!slash) return false;
    *slash = L'\0';
    return true;
}

// 같은 폴더의 64비트 DLL 을 불러 자기등록 함수를 부른다.
static int Register64(const wchar_t *dir, bool unregister) {
    // TSF 등록은 COM 으로 한다 — regsvr32 가 해 주던 초기화를 여기서 직접 한다.
    HRESULT co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool couninit = SUCCEEDED(co);
    wchar_t dll[MAX_PATH];
    swprintf(dll, MAX_PATH, L"%ls\\jamotong.dll", dir);
    HMODULE h = LoadLibraryExW(dll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) {
        LogLine(L"setup: cannot load %ls (error %lu)", dll, GetLastError());
        if (couninit) CoUninitialize();
        return 2;
    }
    RegProc p = (RegProc)(void*)GetProcAddress(h, unregister ? "DllUnregisterServer" : "DllRegisterServer");
    HRESULT hr = p ? p() : E_NOINTERFACE;
    FreeLibrary(h);
    if (couninit) CoUninitialize();
    LogLine(L"setup: x64 %ls hr=0x%08lX", unregister ? L"unregister" : L"register", (unsigned long)hr);
    return SUCCEEDED(hr) ? 0 : 3;
}

// 32비트 DLL 은 이 프로세스에서 못 부른다 — SysWOW64 의 regsvr32 에 맡긴다.
static int Register32(const wchar_t *dir, bool unregister) {
    wchar_t sys[MAX_PATH];
    if (!GetSystemWindowsDirectoryW(sys, MAX_PATH)) return 4;
    wchar_t cmd[MAX_PATH * 2];
    swprintf(cmd, MAX_PATH * 2, L"\"%ls\\SysWOW64\\regsvr32.exe\" /s %ls\"%ls\\jamotong32.dll\"",
             sys, unregister ? L"/u " : L"", dir);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        LogLine(L"setup: cannot start regsvr32 (error %lu)", GetLastError());
        return 5;
    }
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD rc = 1;
    GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    LogLine(L"setup: x86 %ls exit=%lu", unregister ? L"unregister" : L"register", rc);
    return rc == 0 ? 0 : 6;
}

// 설치 폴더와 두 자판 폴더의 원본을 굽는다 — 입력기는 구운 자판만 읽는다.
static void BuildLayouts(const wchar_t *dir) {
    int built = 0, failed = 0;
    JLay_BuildDir(dir, &built, &failed);
    LogLine(L"setup: layouts in install folder built=%d failed=%d", built, failed);
    wchar_t user[MAX_PATH];
    if (Config_UserLayoutDir(user, MAX_PATH)) {
        built = failed = 0;
        JLay_BuildDir(user, &built, &failed);
        LogLine(L"setup: layouts in user folder built=%d failed=%d", built, failed);
    }
}

int Setup_Register(bool unregister) {
    wchar_t dir[MAX_PATH];
    if (!OwnDir(dir, MAX_PATH)) {
        LogLine(L"setup: cannot find my own folder");
        return 1;
    }
    if (!IsElevated()) {
        // 여기서 멈추는 편이 낫다 — 반쯤 등록된 상태가 가장 고치기 어렵다.
        LogLine(L"setup: not elevated - registration writes HKLM and would fail");
        return 7;
    }
    LogLine(L"setup: %ls in %ls", unregister ? L"--unregister" : L"--register", dir);

    int rc64 = Register64(dir, unregister);
    int rc32 = Register32(dir, unregister);
    if (unregister) {
        // 제거는 하나가 실패해도 나머지를 마저 한다 — 남은 등록이 더 나쁘다.
        int rc = rc64 ? rc64 : rc32;
        LogLine(L"setup: --unregister done rc=%d", rc);
        return rc;
    }
    if (rc64 || rc32) {
        Register64(dir, true);   // 반쯤 등록된 채로 두지 않는다
        Register32(dir, true);
        LogLine(L"setup: --register failed (x64=%d x86=%d) - rolled back", rc64, rc32);
        return rc64 ? rc64 : rc32;
    }
    BuildLayouts(dir);
    LogLine(L"setup: --register done");
    return 0;
}
