// setup_cmd.c — `jamotong.exe --register` / `--unregister` (RFC-0019).
//   MSI 가 파일을 놓은 뒤 이것을 지연·비가장으로 부른다. 설치기 없는 zip 에서는 관리자가 직접 부른다.
//   등록 전에 설치 폴더를 검사하고 잠근다(RFC-0019 §7): 폴더는 사용자가 고르므로, 관리자 아닌 누가
//   그 폴더나 위 폴더를 바꿔치기할 수 있으면 거절한다 — 입력기 DLL 은 관리자 앱에도 실린다.
//   그다음 64비트 DLL 자기등록, 32비트 DLL 을 SysWOW64 regsvr32 로 등록, 자판 굽기. 로그는 설치 로그에 이어 쓴다.
#include <windows.h>
#include <objbase.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdbool.h>
#include <stdio.h>
#include <wctype.h>
#include "setup_cmd.h"
#include "jlay_build.h"
#include "config.h"
#include "install_acl.h"

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

// ── 설치 폴더 검사와 잠금 (RFC-0019 §7) ─────────────────────────────────────────────
// 한 폴더의 주인과 DACL 을 읽어 판정한다. 무엇을 봤는지는 로그에 남긴다 — 실패는 MSI 에서 일반 오류로만 보인다.
static bool FolderIsSafe(const wchar_t *path, InstallAclRole role) {
    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        LogLine(L"setup: cannot read %ls (error %lu)", path, GetLastError());
        return false;
    }
    // 연결 지점·심볼릭 링크는 가리키는 곳이 바뀔 수 있다 — 따라가서 검사하지 않고 거절한다.
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
        LogLine(L"setup: refused - %ls is a link (reparse point)", path);
        return false;
    }
    PSID owner = NULL;
    PACL dacl = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    DWORD rc = GetNamedSecurityInfoW(path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                     &owner, NULL, &dacl, NULL, &sd);
    if (rc != ERROR_SUCCESS) {
        LogLine(L"setup: cannot read the security of %ls (error %lu)", path, rc);
        return false;
    }
    enum { MAX_ACES = 64 };
    InstallAce aces[MAX_ACES];
    wchar_t *sids[MAX_ACES];
    size_t n = 0;
    bool ok = true;
    if (dacl && dacl->AceCount > MAX_ACES) {
        LogLine(L"setup: refused - %ls has %u ACEs (more than %d)", path, dacl->AceCount, MAX_ACES);
        ok = false;
    }
    for (WORD i = 0; ok && dacl && i < dacl->AceCount; i++) {
        ACE_HEADER *h = NULL;
        if (!GetAce(dacl, i, (void **)&h)) { ok = false; break; }
        // 허용·거부 ACE 만 SID 가 같은 자리에 있다. 그 밖의 꼴(객체 ACE 등)은 폴더에 쓰이지 않지만,
        // 만나면 읽지 못한 것으로 보고 거절한다.
        if (h->AceType != ACCESS_ALLOWED_ACE_TYPE && h->AceType != ACCESS_DENIED_ACE_TYPE) {
            if (h->AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE) continue;
            LogLine(L"setup: refused - %ls has an ACE of type %u", path, h->AceType);
            ok = false;
            break;
        }
        ACCESS_ALLOWED_ACE *a = (ACCESS_ALLOWED_ACE *)h;
        wchar_t *s = NULL;
        if (!ConvertSidToStringSidW((PSID)&a->SidStart, &s)) { ok = false; break; }
        sids[n] = s;
        aces[n] = (InstallAce){ s, a->Mask, h->AceFlags, h->AceType == ACCESS_ALLOWED_ACE_TYPE };
        n++;
    }
    wchar_t *ownerStr = NULL;
    if (owner) ConvertSidToStringSidW(owner, &ownerStr);
    if (ok) {
        InstallAclVerdict v = InstallAcl_Check(ownerStr, aces, dacl ? n : INSTALL_ACL_NULL_DACL, role);
        if (v == INSTALL_ACL_UNSAFE_OWNER) {
            LogLine(L"setup: refused - %ls is owned by %ls, not by administrators", path, ownerStr ? ownerStr : L"(unknown)");
            ok = false;
        } else if (v == INSTALL_ACL_UNSAFE_ACE) {
            long k = InstallAcl_FirstUnsafe(aces, dacl ? n : INSTALL_ACL_NULL_DACL, role);
            if (!dacl) LogLine(L"setup: refused - %ls has no DACL (everyone may change it)", path);
            else LogLine(L"setup: refused - %ls lets %ls change it (access 0x%08lX)", path,
                         aces[k].sid, (unsigned long)aces[k].mask);
            ok = false;
        }
    }
    if (ownerStr) LocalFree(ownerStr);
    for (size_t i = 0; i < n; i++) LocalFree(sids[i]);
    LocalFree(sd);
    return ok;
}

// 설치 폴더의 위 폴더를 드라이브 루트까지 모두 본다. 로컬 고정 디스크만 받는다.
static bool AncestorsAreSafe(const wchar_t *dir) {
    if (!(iswalpha(dir[0]) && dir[1] == L':' && dir[2] == L'\\')) {
        LogLine(L"setup: refused - %ls is not on a local drive", dir);
        return false;
    }
    wchar_t root[4] = { dir[0], L':', L'\\', 0 };
    if (GetDriveTypeW(root) != DRIVE_FIXED) {
        LogLine(L"setup: refused - %ls is not a fixed local disk", root);
        return false;
    }
    wchar_t path[MAX_PATH];
    wcscpy_s(path, MAX_PATH, dir);
    for (;;) {
        wchar_t *slash = wcsrchr(path, L'\\');
        if (!slash) return true;
        if (slash == path + 2) {                 // "C:\" — 루트 자신을 보고 끝낸다
            slash[1] = L'\0';
            return FolderIsSafe(path, INSTALL_ACL_ROOT);
        }
        *slash = L'\0';
        if (!FolderIsSafe(path, INSTALL_ACL_ANCESTOR)) return false;
    }
}

// 설치 폴더를 관리자만 쓰게 잠근다: 주인 Administrators, 상속을 끊은 DACL — SYSTEM·Administrators 는 모두,
// 사용자와 두 앱 컨테이너 묶음(UWP 호스트가 DLL 을 읽는다)은 읽기·실행. 안의 파일은 이 폴더를 상속하게 되돌린다.
static bool LockInstallDir(const wchar_t *dir) {
    static const wchar_t sddl[] =
        L"O:BAD:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)(A;OICI;0x1200a9;;;AC)(A;OICI;0x1200a9;;;S-1-15-2-2)";
    PSECURITY_DESCRIPTOR sd = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, NULL)) {
        LogLine(L"setup: cannot build the install folder DACL (error %lu)", GetLastError());
        return false;
    }
    PSID owner = NULL;
    PACL dacl = NULL;
    BOOL def = FALSE, present = FALSE;
    GetSecurityDescriptorOwner(sd, &owner, &def);
    GetSecurityDescriptorDacl(sd, &present, &dacl, &def);
    DWORD rc = TreeSetNamedSecurityInfoW((LPWSTR)dir, SE_FILE_OBJECT,
                                         OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                         owner, NULL, dacl, NULL, TREE_SEC_INFO_RESET, NULL, ProgressInvokeNever, NULL);
    LocalFree(sd);
    if (rc != ERROR_SUCCESS) {
        LogLine(L"setup: cannot lock %ls (error %lu)", dir, rc);
        return false;
    }
    return FolderIsSafe(dir, INSTALL_ACL_TARGET);    // 잠근 뒤 다시 읽어 확인한다
}

// 같은 폴더의 64비트 DLL 을 불러 자기등록 함수를 부른다.
static int Register64(const wchar_t *dir, bool unregister) {
    // TSF 등록은 COM 으로 한다 — regsvr32 가 해 주던 초기화를 여기서 직접 한다.
    HRESULT co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool couninit = SUCCEEDED(co);
    wchar_t dll[MAX_PATH];
    swprintf(dll, MAX_PATH, L"%ls\\jamotong.dll", dir);
    // DLL 은 절대 경로로, 그 DLL 이 부르는 것은 System32 에서만 찾는다 — 설치 폴더에 누가 심어 둔
    // UIAutomationCore.dll 같은 것이 SYSTEM 으로 실리지 않게 한다(RFC-0019 §7).
    HMODULE h = LoadLibraryExW(dll, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
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

    // 등록하기 전에: 위 폴더가 안전한가, 그리고 설치 폴더를 잠근다. 해제는 검사하지 않는다 — 지우는 길은 막지 않는다.
    if (!unregister) {
        if (!AncestorsAreSafe(dir) || !LockInstallDir(dir)) {
            LogLine(L"setup: --register refused - choose a folder only administrators can change");
            return 8;
        }
        LogLine(L"setup: install folder checked and locked");
    }

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
