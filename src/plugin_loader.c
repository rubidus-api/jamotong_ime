#include "plugin_loader.h"
#include "jlay.h"   // 입력기는 구운 자판만 읽는다 (RFC-0016 P5b-2)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

extern HINSTANCE g_hInst;

// 처음 보는 자판을 켠 채로 들일까 (2026-10-02, 오너: "차이니즈 팩 추가하면 현재 자판에 자동으로 간체 자판 추가").
//   팩 MSI 가 HKLM\SOFTWARE\Jamotong\AutoEnable 에 자판 이름을 적어 둔다(팩을 지우면 함께 지워진다). 이 판단은
//   **처음 볼 때만** 쓰인다 — 설정 파일에 이미 있는 자판은 사용자가 고른 켜짐/꺼짐을 따른다(Config_LoadFromFile 병합).
static bool AutoEnableHint(const wchar_t *name) {
    if (!name || !name[0]) return false;
    DWORD v = 0, sz = sizeof v, type = 0;
    LONG rc = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Jamotong\\AutoEnable", name, RRF_RT_REG_DWORD, &type, &v, &sz);
    return rc == ERROR_SUCCESS && v != 0;
}

// dir 안의 *.jmb(구운 자판) 전부 로드해 목록에 추가 (기본 꺼짐, 팩이 켜 달라고 적어 둔 것은 켬). 같은 이름 자판이 이미 있으면
// (예: DLL 옆과 사용자 저장소에 같은 파일) 새 로드본의 리소스를 해제하고 건너뛴다.
//   오너 결정 2026-09-23: 입력기는 **구운 자판만** 읽는다. `.jmt` 를 굽는 일은 도구가 한다
//   (설치 스크립트가 폴더를 훑고, 관리 앱이 뜰 때 다시 굽는다). 구운 파일은 열면서 검사합과
//   (순차 자판이면) 사전까지 본다 — 성한 자판만 목록에 오른다.
// 본문을 나중에 읽기 (RFC-0020 F1, 입력기 DLL 만): 한글·조합·정적 자판은 머리만 읽어 목록에 올리고, 지금 자판이 될 때 본문을
//   읽는다. 순차 자판은 본문이 작고(사전은 쓸 때 연다) 설정 창이 그 정보를 쓰므로 그대로 읽는다.
static bool g_deferBodies;
void PluginLoader_SetDeferBodies(bool on) { g_deferBodies = on; }
static void LoadJmtDir(JamotongConfig *config, const wchar_t *dir) {
    wchar_t searchPath[MAX_PATH];
    swprintf(searchPath, MAX_PATH, L"%s\\*.jmb", dir);
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        wchar_t fullPath[MAX_PATH];
        swprintf(fullPath, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        LayoutConfig lc;
        memset(&lc, 0, sizeof(lc));
        JLayError lerr = JLAY_OK;
        bool okLoad = false;
        if (g_deferBodies) {
            okLoad = JLay_LoadHeader(fullPath, &lc, &lerr);
            if (okLoad && lc.type == LAYOUT_TYPE_SEQUENCE) {   // 순차 자판은 본문까지 (작다)
                Config_FreeLayoutResources(&lc);
                okLoad = JLay_Load(fullPath, &lc, &lerr);
            } else if (okLoad) {
                lc.deferredPath = _wcsdup(fullPath);
                lc.bodyDeferred = lc.deferredPath != NULL;
                if (!lc.deferredPath) { Config_FreeLayoutResources(&lc); okLoad = false; lerr = JLAY_E_MEMORY; }
            }
        } else okLoad = JLay_Load(fullPath, &lc, &lerr);
        if (!okLoad) {
            // 조용히 사라지지 않게 남긴다. 이 파일은 관리 앱도 링크하므로(JamoDiag 는 DLL 에만
            // 있다) 디버거로 바로 보이는 OutputDebugString 을 쓴다.
            wchar_t line[MAX_PATH + 120];
            _snwprintf(line, MAX_PATH + 120, L"jamotong: skipping %ls - %ls\n", fd.cFileName, JLay_ErrorText(lerr));
            line[MAX_PATH + 119] = L'\0';
            OutputDebugStringW(line);
            continue;
        }
        bool dup = false;
        for (int i = 0; i < config->layoutCount; i++) {
            if (config->layouts[i].name && lc.name && wcscmp(config->layouts[i].name, lc.name) == 0) { dup = true; break; }
        }
        if (dup) { Config_FreeLayoutResources(&lc); continue; }
        lc.enabled = AutoEnableHint(lc.name);   // 사용자 자판 기본 꺼짐 (설정에서 켬) — 팩이 적어 둔 것만 켬
        if (!Config_AppendLayout(config, &lc)) { Config_FreeLayoutResources(&lc); break; }   // 개수 제한 없음(D3)
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

void PluginLoader_LoadAll(JamotongConfig *config) {
    if (!g_hInst) return;
    if (g_deferBodies) Config_SetBodyLoader(JLay_LoadDeferredBody);

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_hInst, path, MAX_PATH);
    wchar_t *lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
    }

    // 구운 자판 로드: *.jmb. 원본(.jmt)은 도구가 굽고, 입력기는 그 산출물만 읽는다.
    // 탐색 순서(RFC-0011 P0 명문화): ① 사용자 %APPDATA%\Jamotong\layouts → ② 기계 전체
    // %PROGRAMDATA%\Jamotong\layouts → ③ DLL 옆 폴더(v1 호환). 같은 이름은 **먼저 읽은 쪽이
    // 이긴다** = 사용자 자판이 기계 전체/내장 배포본을 덮어쓸 수 있다(예전엔 DLL 옆이 먼저라
    // 사용자가 못 덮었다 — 코드 검토 2026-08-21 발견).
    wchar_t userDir[MAX_PATH], machDir[MAX_PATH];
    if (Config_UserLayoutDir(userDir, MAX_PATH)) LoadJmtDir(config, userDir);
    if (Config_MachineLayoutDir(machDir, MAX_PATH)) LoadJmtDir(config, machDir);
    LoadJmtDir(config, path);

    // DLL 플러그인 자판은 없다(RFC-0006 D2, 2026-09-30). TIP 은 explorer 를 비롯한 모든 프로세스에
    // 실리므로 임의 DLL 을 부르면 셸이 통째로 죽을 수 있다 — 자판은 코드 실행 없는 데이터(.jmt)뿐이다.
}
