// disk_version.c — 실린 코드가 디스크에서 바뀌었는지 (disk_version.h).
#include "disk_version.h"
#include "version.h"
#include <stdio.h>
#include <string.h>

// kernel32 에 있는 psapi 함수 (Windows 7+). 헤더의 PSAPI_VERSION 에 기대지 않고 직접 선언한다.
DWORD WINAPI K32GetMappedFileNameW(HANDLE process, LPVOID addr, LPWSTR name, DWORD size);

static const wchar_t *Tail(const wchar_t *p) {
    const wchar_t *s = wcsrchr(p, L'\\');
    return s ? s + 1 : p;
}

bool DiskVersion_IsStale(HMODULE mod) {
    if (!mod) mod = GetModuleHandleW(NULL);
    wchar_t path[MAX_PATH], mapped[MAX_PATH + 64];
    DWORD n = GetModuleFileNameW(mod, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    // 매핑된 파일의 **지금** 이름 (장치 경로, \Device\HarddiskVolume3\...) — 파일이 옮겨지면 따라간다.
    DWORD m = K32GetMappedFileNameW(GetCurrentProcess(), (LPVOID)mod, mapped, MAX_PATH + 64);
    if (m == 0 || m >= MAX_PATH + 64) return false;
    mapped[m] = L'\0';
    if (_wcsicmp(Tail(mapped), Tail(path)) != 0) return true;   // 옆으로 치워졌다 (…\Config.Msi\1a2b.rbf)
    // 같은 이름이면 폴더까지 견준다 (장치 경로의 끝이 드라이브 뒤의 경로와 같아야 한다)
    const wchar_t *rest = (path[1] == L':') ? path + 2 : path;
    size_t rl = wcslen(rest), ml = wcslen(mapped);
    return !(ml >= rl && _wcsicmp(mapped + ml - rl, rest) == 0);
}

void DiskVersion_StaleNote(HMODULE mod, const wchar_t *what, wchar_t *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = L'\0';
    if (!DiskVersion_IsStale(mod)) return;
    _snwprintf(out, cap, L"\n\nA new version of Jamotong has been installed, but %ls still runs %ls: it was open "
               L"during the upgrade. Reopen it (for the taskbar, sign out and in) to use the new version. "
               L"No reinstall or restart is needed.", what, JAMOTONG_VERSION);
    out[cap - 1] = L'\0';
}
