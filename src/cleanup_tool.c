// cleanup_tool.c — jamotong-cleanup.exe: removes every version of Jamotong from this PC (owner request 2026-10-04:
//   "릴리즈할 때 지금까지의 모든 버젼을 다 지울 수 있는 클린업 유틸리티도 항상 같이 제공 … 재부팅이 필요하면 사용자에게 재부팅
//   후 다시 실행시켜 달라고 안내 … gui 프로그램으로"). A program of its own (no Jamotong source is linked in), shipped
//   beside the MSI and the zip of every release.
//
// What it finds and removes:
//   - installed products: Jamotong (any version, MSI) and the separate language pack MSIs of 0.62-0.69;
//   - an input method registered without an installer (the zip and bat installs before 0.61): the TIP is unregistered
//     through the DLL itself (64-bit) and SysWOW64's regsvr32 (32-bit), and its keys go if they remain;
//   - the IMM32 input method of the very first versions (jamotong.ime, its Keyboard Layouts entry);
//   - what is left in the install folders, HKLM\SOFTWARE\Jamotong, %ProgramData%\Jamotong, HKCU\Software\Jamotong and
//     the Jamotong entries of the user's input method list;
//   - on request (a check box, off by default) the user's own settings, layouts and dictionaries (%APPDATA%\Jamotong).
// A file a running program still holds is deleted at the next restart; then the tool asks for a restart and to be run
// again, which finishes what is left. It asks for administrator rights at start (manifest).
#include <windows.h>
#include <commctrl.h>
#include <msi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "version.h"

#define TIP_CLSID L"{C471BCF2-343F-4187-A103-24151C3E20B9}"
static const wchar_t *const kUpgradeCodes[] = {
    L"{6F3A1D52-9C84-5F7B-9A21-0F0F2F0D7A10}",   // Jamotong
    L"{20EB5A9D-485A-4473-91E7-702AD0A2161B}",   // Chinese pack 0.62 / Chinese Simplified 0.63-0.69
    L"{BD50664A-1671-4CDB-9D38-B8B5B3378B56}",   // Chinese Traditional 0.63-0.69
};

enum { K_PROC, K_PACK, K_PRODUCT, K_TIP64, K_TIP32, K_TIPKEY, K_IMM, K_DIR, K_MREG, K_PDATA, K_UREG, K_ULIST, K_UDATA };
typedef struct { int kind; REGSAM view; wchar_t label[600]; wchar_t data[MAX_PATH * 2]; } Item;
#define MAX_ITEMS 256
static Item g_items[MAX_ITEMS];
static int g_count;
static bool g_ko, g_busy, g_reboot, g_pending;
static HWND g_wnd, g_list, g_status, g_chk, g_btnRemove, g_btnScan, g_btnClose;
static HFONT g_font;
static wchar_t g_log[MAX_PATH];

#define T(en, ko) (g_ko ? (ko) : (en))
#define WM_STATUS (WM_APP + 1)
#define WM_DONE   (WM_APP + 2)

static void Log(const wchar_t *fmt, ...) {
    wchar_t line[1200];
    va_list ap; va_start(ap, fmt);
    int n = _vsnwprintf(line, 1196, fmt, ap);
    va_end(ap);
    if (n < 0) n = 1195;
    line[n++] = L'\r'; line[n++] = L'\n'; line[n] = 0;
    HANDLE f = CreateFileW(g_log, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    char u[3600];
    int b = WideCharToMultiByte(CP_UTF8, 0, line, n, u, (int)sizeof u, NULL, NULL);
    DWORD put;
    if (b > 0) WriteFile(f, u, (DWORD)b, &put, NULL);
    CloseHandle(f);
}
static void Status(const wchar_t *text) {   // from the worker: the window copies it
    wchar_t *c = _wcsdup(text);
    if (c) PostMessageW(g_wnd, WM_STATUS, 0, (LPARAM)c);
    Log(L"%ls", text);
}

static void Add(int kind, REGSAM view, const wchar_t *label, const wchar_t *data) {
    if (g_count >= MAX_ITEMS) return;
    for (int i = 0; i < g_count; i++)
        if (g_items[i].kind == kind && !_wcsicmp(g_items[i].data, data ? data : L"")) return;
    Item *it = &g_items[g_count++];
    it->kind = kind; it->view = view;
    lstrcpynW(it->label, label, 600);
    lstrcpynW(it->data, data ? data : L"", MAX_PATH * 2);
}
static bool IsGuid(const wchar_t *s) { return wcslen(s) == 38 && s[0] == L'{' && s[37] == L'}'; }
static bool RegStr(HKEY root, const wchar_t *key, const wchar_t *name, REGSAM view, wchar_t *out, DWORD cch) {
    DWORD sz = cch * sizeof(wchar_t);
    out[0] = 0;
    return RegGetValueW(root, key, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | (view == KEY_WOW64_32KEY ? RRF_SUBKEY_WOW6432KEY : RRF_SUBKEY_WOW6464KEY),
                        NULL, out, &sz) == ERROR_SUCCESS && out[0];
}
static bool KeyExists(HKEY root, const wchar_t *key, REGSAM view) {
    HKEY h;
    if (RegOpenKeyExW(root, key, 0, KEY_READ | view, &h) != ERROR_SUCCESS) return false;
    RegCloseKey(h);
    return true;
}
static bool DirExists(const wchar_t *p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static void DirOf(const wchar_t *path, wchar_t *out) {
    lstrcpynW(out, path, MAX_PATH * 2);
    wchar_t *s = wcsrchr(out, L'\\');
    if (s) *s = 0;
}

// ── finding ──────────────────────────────────────────────────────────────────────────
static void ScanProducts(void) {
    static const REGSAM views[] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    for (int v = 0; v < 2; v++) {
        HKEY h;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | views[v], &h) != ERROR_SUCCESS) continue;
        wchar_t name[256];
        for (DWORD i = 0;; i++) {
            DWORD n = 256;
            if (RegEnumKeyExW(h, i, name, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            if (!IsGuid(name)) continue;
            wchar_t key[400], dn[256], pub[128], ver[64];
            _snwprintf(key, 400, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%ls", name);
            if (!RegStr(HKEY_LOCAL_MACHINE, key, L"DisplayName", views[v], dn, 256) || wcsncmp(dn, L"Jamotong", 8)) continue;
            if (!RegStr(HKEY_LOCAL_MACHINE, key, L"Publisher", views[v], pub, 128) || wcscmp(pub, L"Jamotong")) continue;
            RegStr(HKEY_LOCAL_MACHINE, key, L"DisplayVersion", views[v], ver, 64);
            wchar_t label[600];
            _snwprintf(label, 600, T(L"Installed: %ls %ls", L"설치된 제품: %ls %ls"), dn, ver);
            Add(wcscmp(dn, L"Jamotong") ? K_PACK : K_PRODUCT, 0, label, name);
        }
        RegCloseKey(h);
    }
    for (size_t u = 0; u < sizeof kUpgradeCodes / sizeof kUpgradeCodes[0]; u++) {   // products the list above missed
        wchar_t pc[39];
        for (DWORD i = 0; MsiEnumRelatedProductsW(kUpgradeCodes[u], 0, i, pc) == ERROR_SUCCESS; i++) {
            bool have = false;
            for (int k = 0; k < g_count; k++) if (!_wcsicmp(g_items[k].data, pc)) have = true;
            if (have) continue;
            wchar_t dn[256] = L"Jamotong", label[600];
            DWORD n = 256;
            MsiGetProductInfoW(pc, INSTALLPROPERTY_PRODUCTNAME, dn, &n);
            _snwprintf(label, 600, T(L"Installed: %ls", L"설치된 제품: %ls"), dn);
            Add(u == 0 ? K_PRODUCT : K_PACK, 0, label, pc);
        }
    }
}
static void ScanRegistration(void) {
    static const REGSAM views[] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    for (int v = 0; v < 2; v++) {
        wchar_t path[MAX_PATH * 2], label[600];
        if (RegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" TIP_CLSID L"\\InprocServer32", NULL, views[v], path, MAX_PATH * 2)) {
            _snwprintf(label, 600, T(L"Registered input method (%ls-bit): %ls", L"등록된 입력기 (%ls비트): %ls"), v ? L"32" : L"64", path);
            Add(v ? K_TIP32 : K_TIP64, views[v], label, path);
            wchar_t dir[MAX_PATH * 2];
            DirOf(path, dir);
            _snwprintf(label, 600, T(L"Folder: %ls", L"폴더: %ls"), dir);
            if (DirExists(dir)) Add(K_DIR, 0, label, dir);
        }
        if (KeyExists(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\CTF\\TIP\\" TIP_CLSID, views[v]))
            Add(K_TIPKEY, views[v], T(L"Input method profile in the registry", L"레지스트리의 입력기 프로필"), v ? L"32" : L"64");
    }
    // the IMM32 input method of the first versions
    HKEY h;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts", 0, KEY_READ, &h) == ERROR_SUCCESS) {
        wchar_t klid[64];
        for (DWORD i = 0;; i++) {
            DWORD n = 64;
            if (RegEnumKeyExW(h, i, klid, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            wchar_t key[160], ime[MAX_PATH];
            _snwprintf(key, 160, L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts\\%ls", klid);
            if (RegStr(HKEY_LOCAL_MACHINE, key, L"IME File", KEY_WOW64_64KEY, ime, MAX_PATH) && !_wcsicmp(ime, L"jamotong.ime"))
                Add(K_IMM, 0, T(L"Old IMM32 input method (jamotong.ime)", L"옛 IMM32 입력기 (jamotong.ime)"), klid);
        }
        RegCloseKey(h);
    }
    wchar_t sys[MAX_PATH], p[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    _snwprintf(p, MAX_PATH, L"%ls\\jamotong.ime", sys);
    if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) Add(K_IMM, 0, T(L"Old IMM32 input method (jamotong.ime)", L"옛 IMM32 입력기 (jamotong.ime)"), L"file");
}
static void ScanFolders(void) {
    wchar_t dir[MAX_PATH * 2], label[600];
    static const REGSAM views[] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    for (int v = 0; v < 2; v++) {
        if (RegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Jamotong", L"InstallDir", views[v], dir, MAX_PATH * 2)) {
            size_t n = wcslen(dir);
            while (n > 3 && dir[n - 1] == L'\\') dir[--n] = 0;
            _snwprintf(label, 600, T(L"Folder: %ls", L"폴더: %ls"), dir);
            if (DirExists(dir)) Add(K_DIR, 0, label, dir);
        }
        if (KeyExists(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Jamotong", views[v]))
            Add(K_MREG, views[v], T(L"HKLM\\SOFTWARE\\Jamotong", L"HKLM\\SOFTWARE\\Jamotong"), v ? L"32" : L"64");
    }
    static const int known[] = { CSIDL_PROGRAM_FILES, CSIDL_PROGRAM_FILESX86 };
    for (int k = 0; k < 2; k++) {
        wchar_t base[MAX_PATH];
        if (SHGetFolderPathW(NULL, known[k], NULL, 0, base) != S_OK) continue;
        _snwprintf(dir, MAX_PATH * 2, L"%ls\\Jamotong", base);
        _snwprintf(label, 600, T(L"Folder: %ls", L"폴더: %ls"), dir);
        if (DirExists(dir)) Add(K_DIR, 0, label, dir);
    }
    wchar_t pd[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, pd) == S_OK) {
        _snwprintf(dir, MAX_PATH * 2, L"%ls\\Jamotong", pd);
        _snwprintf(label, 600, T(L"Folder: %ls", L"폴더: %ls"), dir);
        if (DirExists(dir)) Add(K_PDATA, 0, label, dir);
    }
}
static void ScanUser(void) {
    if (KeyExists(HKEY_CURRENT_USER, L"Software\\Jamotong", 0)) Add(K_UREG, 0, L"HKCU\\Software\\Jamotong", L"ureg");
    HKEY h;   // the user's input method list (Settings > Language): entries that point at Jamotong
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\International\\User Profile", 0, KEY_READ, &h) == ERROR_SUCCESS) {
        wchar_t lang[128];
        for (DWORD i = 0;; i++) {
            DWORD n = 128;
            if (RegEnumKeyExW(h, i, lang, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            HKEY hl;
            if (RegOpenKeyExW(h, lang, 0, KEY_READ, &hl) != ERROR_SUCCESS) continue;
            wchar_t val[256];
            for (DWORD j = 0;; j++) {
                DWORD vn = 256;
                if (RegEnumValueW(hl, j, val, &vn, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
                if (!wcsstr(val, TIP_CLSID)) continue;
                wchar_t label[600], data[MAX_PATH * 2];
                _snwprintf(label, 600, T(L"Input method list entry (%ls)", L"입력기 목록의 항목 (%ls)"), lang);
                _snwprintf(data, MAX_PATH * 2, L"%ls|%ls", lang, val);
                Add(K_ULIST, 0, label, data);
            }
            RegCloseKey(hl);
        }
        RegCloseKey(h);
    }
    wchar_t ad[MAX_PATH], dir[MAX_PATH * 2], label[600];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, ad) == S_OK) {
        _snwprintf(dir, MAX_PATH * 2, L"%ls\\Jamotong", ad);
        _snwprintf(label, 600, T(L"Your settings, layouts and dictionaries: %ls (only with the box ticked)",
                                 L"내 설정·자판·사전: %ls (아래 칸을 체크했을 때만)"), dir);
        if (DirExists(dir)) Add(K_UDATA, 0, label, dir);
    }
}
static void ScanProcesses(void) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe = { .dwSize = sizeof pe };
    for (BOOL ok = Process32FirstW(s, &pe); ok; ok = Process32NextW(s, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"jamotong.exe")) continue;
        wchar_t label[600], data[32];
        _snwprintf(label, 600, T(L"Running: jamotong.exe (process %lu) - it will be closed", L"실행 중: jamotong.exe (프로세스 %lu) - 닫습니다"), pe.th32ProcessID);
        _snwprintf(data, 32, L"%lu", pe.th32ProcessID);
        Add(K_PROC, 0, label, data);
    }
    CloseHandle(s);
}
// A restart is still pending for files of an earlier removal
static bool PendingRestart(void) {
    HKEY h;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager", 0, KEY_READ, &h) != ERROR_SUCCESS) return false;
    DWORD sz = 0;
    bool hit = false;
    if (RegQueryValueExW(h, L"PendingFileRenameOperations", NULL, NULL, NULL, &sz) == ERROR_SUCCESS && sz) {
        wchar_t *buf = (wchar_t *)calloc(sz / 2 + 2, sizeof(wchar_t));
        if (buf && RegQueryValueExW(h, L"PendingFileRenameOperations", NULL, NULL, (BYTE *)buf, &sz) == ERROR_SUCCESS) {
            // pairs of (source, target); a deletion's target is an empty string - walk the whole list, not to the first ""
            size_t n = sz / sizeof(wchar_t);
            for (size_t i = 0; i < n && !hit;) {
                size_t len = wcsnlen(buf + i, n - i);
                if (len) {
                    wchar_t low[MAX_PATH * 2];
                    lstrcpynW(low, buf + i, MAX_PATH * 2);
                    CharLowerW(low);
                    if (wcsstr(low, L"\\jamotong\\") || wcsstr(low, L"\\jamotong") || wcsstr(low, L"jamotong.ime")) hit = true;
                }
                i += len + 1;
            }
        }
        free(buf);
    }
    RegCloseKey(h);
    return hit;
}
static void Scan(void) {
    g_count = 0;
    ScanProcesses();
    ScanProducts();
    ScanRegistration();
    ScanFolders();
    ScanUser();
    g_pending = PendingRestart();
}

// ── removing ─────────────────────────────────────────────────────────────────────────
static void Later(const wchar_t *p) {   // deleted at the next restart
    if (MoveFileExW(p, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)) { g_reboot = true; Log(L"  at restart: %ls", p); }
}
static void DeleteTree(const wchar_t *dir) {
    wchar_t pat[MAX_PATH * 2 + 4];
    _snwprintf(pat, MAX_PATH * 2 + 4, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            wchar_t p[MAX_PATH * 2];
            _snwprintf(p, MAX_PATH * 2, L"%ls\\%ls", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { RemoveDirectoryW(p); continue; }   // never follow a link
                DeleteTree(p);
            } else {
                SetFileAttributesW(p, FILE_ATTRIBUTE_NORMAL);
                if (!DeleteFileW(p)) Later(p);
            }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    if (!RemoveDirectoryW(dir)) Later(dir);
}
// A folder not named Jamotong (a folder the user picked) loses only Jamotong's own files
static bool OurFile(const wchar_t *n) {
    static const wchar_t *const pre[] = { L"jamotong", L"layout-ko-", L"example", L"hanja", L"chinese", L"japanese", L"romaji-kana",
                                          L"pinyin-keys", L"README", L"LICENSE", L"COPYRIGHT", L"jmt-format", L"UNICODE-LICENSE",
                                          L"install.bat", L"uninstall.bat", NULL };
    for (int i = 0; pre[i]; i++) if (!_wcsnicmp(n, pre[i], wcslen(pre[i]))) return true;
    const wchar_t *e = wcsrchr(n, L'.');
    return e && (!_wcsicmp(e, L".jmb") || !_wcsicmp(e, L".jmt") || !_wcsicmp(e, L".jdb") || wcsstr(n, L".old."));
}
static void CleanFolder(const wchar_t *dir) {
    const wchar_t *last = wcsrchr(dir, L'\\');
    if (last && !_wcsicmp(last + 1, L"Jamotong")) { DeleteTree(dir); return; }
    wchar_t pat[MAX_PATH * 2 + 4];
    _snwprintf(pat, MAX_PATH * 2 + 4, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        wchar_t p[MAX_PATH * 2];
        _snwprintf(p, MAX_PATH * 2, L"%ls\\%ls", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!_wcsicmp(fd.cFileName, L"licenses") && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) DeleteTree(p);
        } else if (OurFile(fd.cFileName)) {
            SetFileAttributesW(p, FILE_ATTRIBUTE_NORMAL);
            if (!DeleteFileW(p)) Later(p);
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    RemoveDirectoryW(dir);   // only if now empty
}
typedef HRESULT (WINAPI *RegProc)(void);
static void UnregisterTip(int kind, const wchar_t *path) {
    if (kind == K_TIP64) {
        HMODULE m = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (m) {
            RegProc p = (RegProc)(void *)GetProcAddress(m, "DllUnregisterServer");
            HRESULT hr = p ? p() : E_FAIL;
            Log(L"  DllUnregisterServer %ls -> 0x%08lx", path, (unsigned long)hr);
            FreeLibrary(m);
        }
    } else {
        wchar_t win[MAX_PATH], cmd[MAX_PATH * 3];
        GetWindowsDirectoryW(win, MAX_PATH);
        _snwprintf(cmd, MAX_PATH * 3, L"\"%ls\\SysWOW64\\regsvr32.exe\" /u /s \"%ls\"", win, path);
        STARTUPINFOW si = { .cb = sizeof si };
        PROCESS_INFORMATION pi;
        if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 60000);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        }
        Log(L"  regsvr32 /u %ls", path);
    }
}
static DWORD WINAPI Worker(LPVOID arg) {
    bool userData = arg != NULL;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    g_reboot = false;
    int failed = 0;
    for (int i = 0; i < g_count; i++) if (g_items[i].kind == K_PROC) {   // the manager and the UI helper hold jamotong.exe
        HANDLE p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, wcstoul(g_items[i].data, NULL, 10));
        if (p) { TerminateProcess(p, 0); WaitForSingleObject(p, 5000); CloseHandle(p); }
    }
    MsiSetInternalUI(INSTALLUILEVEL_NONE, NULL);
    for (int pass = 0; pass < 2; pass++)   // packs first, then Jamotong
        for (int i = 0; i < g_count; i++) {
            Item *it = &g_items[i];
            if (it->kind != (pass ? K_PRODUCT : K_PACK)) continue;
            wchar_t s[700];
            _snwprintf(s, 700, T(L"Removing %ls ...", L"%ls 지우는 중 ..."), it->label);
            Status(s);
            UINT r = MsiConfigureProductExW(it->data, INSTALLLEVEL_DEFAULT, INSTALLSTATE_ABSENT, L"REBOOT=ReallySuppress MSIRESTARTMANAGERCONTROL=Disable");
            Log(L"  msi remove %ls -> %u", it->data, r);
            if (r == ERROR_SUCCESS_REBOOT_REQUIRED || r == ERROR_SUCCESS_REBOOT_INITIATED) g_reboot = true;
            else if (r != ERROR_SUCCESS && r != ERROR_UNKNOWN_PRODUCT) failed++;
        }
    // What the installers did not take with them: look again
    Status(T(L"Removing what is left ...", L"남은 것을 지우는 중 ..."));
    Scan();
    for (int i = 0; i < g_count; i++) {
        Item *it = &g_items[i];
        switch (it->kind) {
        case K_TIP64: case K_TIP32: UnregisterTip(it->kind, it->data); break;
        default: break;
        }
    }
    for (int i = 0; i < g_count; i++) {
        Item *it = &g_items[i];
        switch (it->kind) {
        case K_TIP64: case K_TIP32:   // still there after unregistering (a damaged install): the keys go
            RegDeleteTreeW(HKEY_LOCAL_MACHINE, it->kind == K_TIP64 ? L"SOFTWARE\\Classes\\CLSID\\" TIP_CLSID : L"SOFTWARE\\Classes\\WOW6432Node\\CLSID\\" TIP_CLSID);
            break;
        case K_TIPKEY: {
            HKEY h;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\CTF\\TIP", 0, KEY_ALL_ACCESS | it->view, &h) == ERROR_SUCCESS) {
                RegDeleteTreeW(h, TIP_CLSID);
                RegCloseKey(h);
            }
            break;
        }
        case K_IMM:
            if (wcscmp(it->data, L"file")) {
                wchar_t key[160];
                _snwprintf(key, 160, L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts\\%ls", it->data);
                RegDeleteTreeW(HKEY_LOCAL_MACHINE, key);
            } else {
                wchar_t sys[MAX_PATH], p[MAX_PATH];
                GetSystemDirectoryW(sys, MAX_PATH);
                _snwprintf(p, MAX_PATH, L"%ls\\jamotong.ime", sys);
                if (!DeleteFileW(p)) Later(p);
                if (GetSystemWow64DirectoryW(sys, MAX_PATH)) { _snwprintf(p, MAX_PATH, L"%ls\\jamotong.ime", sys); if (!DeleteFileW(p)) Later(p); }
            }
            break;
        case K_DIR: CleanFolder(it->data); break;
        case K_PDATA: DeleteTree(it->data); break;
        case K_MREG: {
            HKEY h;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE", 0, KEY_ALL_ACCESS | it->view, &h) == ERROR_SUCCESS) { RegDeleteTreeW(h, L"Jamotong"); RegCloseKey(h); }
            break;
        }
        case K_UREG: RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Jamotong"); break;
        case K_ULIST: {
            wchar_t lang[MAX_PATH * 2];
            lstrcpynW(lang, it->data, MAX_PATH * 2);
            wchar_t *bar = wcschr(lang, L'|');
            if (!bar) break;
            *bar = 0;
            wchar_t key[300];
            _snwprintf(key, 300, L"Control Panel\\International\\User Profile\\%ls", lang);
            RegDeleteKeyValueW(HKEY_CURRENT_USER, key, bar + 1);
            break;
        }
        case K_UDATA: if (userData) DeleteTree(it->data); break;
        default: break;
        }
    }
    Scan();
    int left = 0;
    for (int i = 0; i < g_count; i++) if (g_items[i].kind != K_UDATA && g_items[i].kind != K_PROC) left++;
    Log(L"done: %d failed, %d left, restart %d", failed, left, (int)(g_reboot || g_pending));
    PostMessageW(g_wnd, WM_DONE, (WPARAM)failed, (LPARAM)left);
    CoUninitialize();
    return 0;
}

// ── window ───────────────────────────────────────────────────────────────────────────
enum { ID_LIST = 100, ID_CHK, ID_REMOVE, ID_SCAN, ID_CLOSE };
static void Fill(void) {
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    int width = 0;
    HDC dc = GetDC(g_list);
    HFONT of = (HFONT)SelectObject(dc, g_font);
    int real = 0;
    for (int i = 0; i < g_count; i++) {
        SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)g_items[i].label);
        SIZE sz;
        GetTextExtentPoint32W(dc, g_items[i].label, (int)wcslen(g_items[i].label), &sz);
        if (sz.cx > width) width = sz.cx;
        if (g_items[i].kind != K_UDATA) real++;
    }
    SelectObject(dc, of);
    ReleaseDC(g_list, dc);
    SendMessageW(g_list, LB_SETHORIZONTALEXTENT, (WPARAM)(width + 20), 0);
    if (g_count == 0) SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)T(L"Nothing of Jamotong was found on this PC.", L"이 PC 에 자모통이 남긴 것이 없습니다."));
    EnableWindow(g_btnRemove, real > 0 || (g_count > 0 && SendMessageW(g_chk, BM_GETCHECK, 0, 0) == BST_CHECKED));
    SetWindowTextW(g_status, g_pending
        ? T(L"A restart is still pending from an earlier removal. Restart Windows, then run this tool again.",
            L"앞선 제거가 재부팅을 기다리고 있습니다. Windows 를 다시 시작한 뒤 이 도구를 다시 실행하세요.")
        : real ? T(L"Remove all deletes everything listed above.", L"'모두 지우기'는 위에 보이는 것을 모두 지웁니다.")
               : T(L"Nothing to remove.", L"지울 것이 없습니다."));
}
static HWND Ctl(const wchar_t *cls, const wchar_t *text, DWORD style, int id) {
    HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}
static void Layout(int w, int h, int dpi) {
    int m = MulDiv(12, dpi, 96), bh = MulDiv(30, dpi, 96), bw = MulDiv(130, dpi, 96), lh = MulDiv(20, dpi, 96);
    HWND intro = GetDlgItem(g_wnd, 99);
    MoveWindow(intro, m, m, w - 2 * m, lh * 3, TRUE);
    int top = m + lh * 3 + m / 2;
    int listH = h - top - (lh * 2 + bh + lh * 2 + m * 3);
    MoveWindow(g_list, m, top, w - 2 * m, listH, TRUE);
    MoveWindow(g_chk, m, top + listH + m / 2, w - 2 * m, lh * 2, TRUE);
    MoveWindow(g_status, m, top + listH + m / 2 + lh * 2, w - 2 * m, lh * 2, TRUE);
    int by = h - m - bh;
    MoveWindow(g_btnRemove, m, by, bw, bh, TRUE);
    MoveWindow(g_btnScan, m * 2 + bw, by, bw, bh, TRUE);
    MoveWindow(g_btnClose, w - m - bw, by, bw, bh, TRUE);
}
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_wnd = hwnd;
        UINT dpi = GetDpiForWindow(hwnd);
        NONCLIENTMETRICSW ncm = { .cbSize = sizeof ncm };
        SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, dpi);
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
        wchar_t intro[700];
        _snwprintf(intro, 700, T(L"Jamotong Cleanup %ls removes every version of Jamotong from this PC: the installer (any version), the old language packs, a registration without an installer, the old IMM32 input method and what they left behind. Your settings stay unless you tick the box.",
                                 L"자모통 정리 %ls 는 이 PC 에서 자모통의 모든 판을 지웁니다: 설치본(어느 판이든), 예전 언어 팩, 설치기 없이 한 등록, 옛 IMM32 입력기와 그것들이 남긴 것. 내 설정은 아래 칸을 체크하지 않으면 남습니다."),
                   JAMOTONG_VERSION);
        HWND it = Ctl(L"STATIC", intro, 0, 99);
        (void)it;
        g_list = Ctl(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | WS_HSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOSEL, ID_LIST);
        g_chk = Ctl(L"BUTTON", T(L"Also remove my settings, layouts and dictionaries (%APPDATA%\\Jamotong)", L"내 설정·자판·사전도 지우기 (%APPDATA%\\Jamotong)"), BS_AUTOCHECKBOX | BS_MULTILINE, ID_CHK);
        g_status = Ctl(L"STATIC", L"", 0, 0);
        g_btnRemove = Ctl(L"BUTTON", T(L"Remove all", L"모두 지우기"), BS_DEFPUSHBUTTON, ID_REMOVE);
        g_btnScan = Ctl(L"BUTTON", T(L"Look again", L"다시 찾기"), 0, ID_SCAN);
        g_btnClose = Ctl(L"BUTTON", T(L"Close", L"닫기"), 0, ID_CLOSE);
        Scan();
        Fill();
        return 0;
    }
    case WM_SIZE: Layout(LOWORD(lp), HIWORD(lp), (int)GetDpiForWindow(hwnd)); return 0;
    case WM_GETMINMAXINFO: {
        int dpi = (int)GetDpiForWindow(hwnd);
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = MulDiv(560, dpi, 96);
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = MulDiv(400, dpi, 96);
        return 0;
    }
    case WM_COMMAND:
        if (g_busy) return 0;
        switch (LOWORD(wp)) {
        case ID_CHK: Fill(); return 0;
        case ID_SCAN: Scan(); Fill(); return 0;
        case ID_CLOSE: DestroyWindow(hwnd); return 0;
        case ID_REMOVE: {
            if (MessageBoxW(hwnd, T(L"Remove everything listed? Programs that use Jamotong keep working with another input method.",
                                    L"보이는 것을 모두 지울까요? 자모통을 쓰던 프로그램은 다른 입력기로 계속 쓸 수 있습니다."),
                            T(L"Jamotong Cleanup", L"자모통 정리"), MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
            g_busy = true;
            EnableWindow(g_btnRemove, FALSE); EnableWindow(g_btnScan, FALSE); EnableWindow(g_btnClose, FALSE); EnableWindow(g_chk, FALSE);
            bool user = SendMessageW(g_chk, BM_GETCHECK, 0, 0) == BST_CHECKED;
            HANDLE th = CreateThread(NULL, 0, Worker, user ? (LPVOID)1 : NULL, 0, NULL);
            if (th) CloseHandle(th); else { g_busy = false; EnableWindow(g_btnClose, TRUE); }
            return 0;
        }
        }
        break;
    case WM_STATUS: SetWindowTextW(g_status, (wchar_t *)lp); free((void *)lp); return 0;
    case WM_DONE: {
        g_busy = false;
        Fill();
        EnableWindow(g_btnScan, TRUE); EnableWindow(g_btnClose, TRUE); EnableWindow(g_chk, TRUE);
        int failed = (int)wp, left = (int)lp;
        if (g_reboot || g_pending || left) {
            const wchar_t *t = T(L"Some files are still in use. Restart Windows, then run this tool again to finish.\n\nRestart now?",
                                 L"아직 쓰이고 있는 파일이 있습니다. Windows 를 다시 시작한 뒤 이 도구를 다시 실행해 마무리하세요.\n\n지금 다시 시작할까요?");
            SetWindowTextW(g_status, T(L"Restart Windows, then run this tool again to finish.", L"Windows 를 다시 시작한 뒤 이 도구를 다시 실행해 마무리하세요."));
            if (MessageBoxW(hwnd, t, T(L"Jamotong Cleanup", L"자모통 정리"), MB_YESNO | MB_ICONINFORMATION) == IDYES) {
                HANDLE tok;
                if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
                    TOKEN_PRIVILEGES tp = { .PrivilegeCount = 1 };
                    LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid);
                    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                    AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL);
                    CloseHandle(tok);
                }
                ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_FLAG_PLANNED);
            }
        } else if (failed) {
            SetWindowTextW(g_status, T(L"Some products could not be removed - see the log in %TEMP%\\jamotong-cleanup.log.",
                                       L"지우지 못한 제품이 있습니다 - %TEMP%\\jamotong-cleanup.log 를 보세요."));
        } else {
            SetWindowTextW(g_status, T(L"Done - nothing of Jamotong is left on this PC.", L"끝났습니다 - 이 PC 에 자모통이 남긴 것이 없습니다."));
            MessageBoxW(hwnd, T(L"Done - nothing of Jamotong is left on this PC.", L"끝났습니다 - 이 PC 에 자모통이 남긴 것이 없습니다."),
                        T(L"Jamotong Cleanup", L"자모통 정리"), MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    }
    case WM_CLOSE: if (g_busy) return 0; break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    (void)prev; (void)cmd;
    g_ko = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_KOREAN;
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    _snwprintf(g_log, MAX_PATH, L"%lsjamotong-cleanup.log", tmp);
    Log(L"== Jamotong Cleanup %ls", JAMOTONG_VERSION);
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(100));
    wc.lpszClassName = L"JamotongCleanup";
    RegisterClassW(&wc);
    HWND w = CreateWindowExW(0, L"JamotongCleanup", T(L"Jamotong Cleanup", L"자모통 정리"), WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 760, 520, NULL, NULL, inst, NULL);
    if (!w) return 1;
    UINT dpi = GetDpiForWindow(w);
    SetWindowPos(w, NULL, 0, 0, MulDiv(760, (int)dpi, 96), MulDiv(520, (int)dpi, 96), SWP_NOMOVE | SWP_NOZORDER);
    ShowWindow(w, show);
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(w, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    return 0;
}
