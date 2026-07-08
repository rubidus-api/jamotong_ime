#include "plugin_loader.h"
#include "klay.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

extern HINSTANCE g_hInst;

// dir 안의 *.jmt 전부 로드해 목록에 추가 (기본 꺼짐). 같은 이름 자판이 이미 있으면(예: DLL 옆과
// 사용자 저장소에 같은 파일) 새 로드본의 리소스를 해제하고 건너뛴다.
static void LoadJmtDir(JamotongConfig *config, const wchar_t *dir) {
    wchar_t searchPath[MAX_PATH];
    swprintf(searchPath, MAX_PATH, L"%s\\*.jmt", dir);
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (config->layoutCount >= 8) break;
        wchar_t fullPath[MAX_PATH];
        swprintf(fullPath, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        LayoutConfig lc;
        memset(&lc, 0, sizeof(lc));
        if (!Klay_Load(fullPath, &lc)) continue;
        bool dup = false;
        for (int i = 0; i < config->layoutCount; i++) {
            if (config->layouts[i].name && lc.name && wcscmp(config->layouts[i].name, lc.name) == 0) { dup = true; break; }
        }
        if (dup) { Config_FreeLayoutResources(&lc); continue; }
        lc.enabled = false;   // 사용자 자판 기본 꺼짐 (설정에서 켬)
        config->layouts[config->layoutCount++] = lc;
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

void PluginLoader_LoadAll(JamotongConfig *config) {
    if (!g_hInst) return;

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_hInst, path, MAX_PATH);
    wchar_t *lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
    }

    // 통합 자판 설정파일 로드: *.jmt (Type = static | hangul | chord). 자판 데이터는 사용자 파일.
    // ① DLL 옆 폴더 → ② 사용자 저장소 %APPDATA%\Jamotong\layouts (설정창 Add가 복사해 두는 곳 —
    //    외부 경로 .jmt가 재시작 후 사라지던 문제의 영속화 경로, RFC-0004 P0-2).
    LoadJmtDir(config, path);
    wchar_t userDir[MAX_PATH];
    if (Config_UserLayoutDir(userDir, MAX_PATH)) LoadJmtDir(config, userDir);

    // plugin_*.dll 자동 로드는 비활성화했다 (안정성/보안).
    // 이유: 이 함수는 TIP 생성 시(JamotongTextService_Create) 실행되고, TIP는 언어바/시스템
    // 트레이 표시를 위해 explorer.exe 를 비롯한 모든 호스트 프로세스에 로드된다. 여기서 임의의
    // plugin_*.dll 을 LoadLibrary 하고 그 초기화 함수를 호출하면, 그 플러그인이 조금이라도
    // 잘못되면 호스트(예: explorer)가 통째로 죽는다(작업표시줄 사라짐). 사용자 자판은 이제
    // 코드 실행이 없는 데이터 방식(.jmt: static/hangul/chord)으로 충분히 표현되므로, 임의 코드를
    // 셸 프로세스에 주입하는 이 경로는 제거한다. (다시 켜려면 입력 프로세스에서만 지연 로드하도록
    // 안전화가 선행되어야 한다.)
}
