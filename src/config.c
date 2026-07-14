#include "config.h"
#include <windows.h>   // GetEnvironmentVariableW / CreateDirectoryW (Config_UserPath)
#include "plugin_loader.h"
#include "layout.h"   // KBD_DUBEOL / KBD_SEBEOL
#include "hangul_layout.h"   // HangulLayout_Free (LAYOUT_TYPE_HANGUL_CUSTOM 소유)
#include "chord_layout.h"    // ChordLayout_Free (LAYOUT_TYPE_CHORD 소유)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>   // _wcsdup / free (레이아웃 name·플러그인 리소스 소유권 관리)

void Config_LoadDefault(JamotongConfig *config) {
    // 기본 레이아웃 2개 등록: 영문(패스스루) -> 한글(FSM)
    config->layoutCount = 0;
    
    LayoutConfig en;
    memset(&en, 0, sizeof(en));
    en.type = LAYOUT_TYPE_PASSTHROUGH;
    en.name = _wcsdup(L"en_qwerty");   // 모든 name을 heap으로 통일 → Config_Free가 균일하게 해제
    wcscpy(en.abbrev, L"EN");
    en.enabled = true;                  // 기본 켜짐
    config->layouts[config->layoutCount++] = en;

    LayoutConfig dv;
    memset(&dv, 0, sizeof(dv));
    dv.type = LAYOUT_TYPE_STATIC_MAP;
    Layout_FillDvorak(dv.charMap);     // 드보락 (공개 표준 ANSI)
    dv.name = _wcsdup(L"en_dvorak");
    wcscpy(dv.abbrev, L"Dv");
    dv.enabled = false;                 // 기본 꺼짐 (설정 체크박스로 켬)
    config->layouts[config->layoutCount++] = dv;

    LayoutConfig ko;
    memset(&ko, 0, sizeof(ko));
    ko.type = LAYOUT_TYPE_KOREAN_FSM;
    ko.kbdVariant = KBD_DUBEOL;
    ko.name = _wcsdup(L"ko_2bul");
    wcscpy(ko.abbrev, L"2\xBC8C");   // "2벌"
    ko.enabled = true;                  // 기본 켜짐
    config->layouts[config->layoutCount++] = ko;

    LayoutConfig ko3;
    memset(&ko3, 0, sizeof(ko3));
    ko3.type = LAYOUT_TYPE_KOREAN_FSM;
    ko3.kbdVariant = KBD_SEBEOL;
    ko3.name = _wcsdup(L"ko_3bul");   // 세벌식 최종 (표는 provisional — 실기 검증 필요)
    wcscpy(ko3.abbrev, L"3\xBC8C");   // "3벌"
    ko3.enabled = false;                // 기본 꺼짐
    config->layouts[config->layoutCount++] = ko3;

    // 플러그인 로더 호출 (.jmt 자동 감지) — 로드된 사용자 자판은 기본 꺼짐(enabled=false, zero-init)
    PluginLoader_LoadAll(config);

    config->currentLayoutIndex = 0; // 기본 시작: 영문 QWERTY (안전한 IME 기본값)
    
    // 기본 단축키 (기능별 목록)
    memset(config->shortcuts, 0, sizeof(config->shortcuts));
    ShortcutList *rot = &config->shortcuts[SC_FN_ROTATE];   // 자판 전환: 한/영 키, 오른쪽 Alt, Shift+Space
    rot->count = 3;
    rot->keys[0].vKey = VK_HANGUL; rot->keys[0].mods = 0;
    rot->keys[1].vKey = VK_RMENU;  rot->keys[1].mods = 0;
    rot->keys[2].vKey = VK_SPACE;  rot->keys[2].mods = SMOD_SHIFT;
    ShortcutList *hj = &config->shortcuts[SC_FN_HANJA];     // 한자/특수문자 변환: 한자 키
    hj->count = 1;
    hj->keys[0].vKey = VK_HANJA; hj->keys[0].mods = 0;
    ShortcutList *cd = &config->shortcuts[SC_FN_CODE];      // 유니코드 코드 입력: Ctrl+Alt+U
    cd->count = 1;
    cd->keys[0].vKey = 'U'; cd->keys[0].mods = SMOD_CTRL | SMOD_ALT;
    ShortcutList *st = &config->shortcuts[SC_FN_SETTINGS];  // 설정 창 열기: Ctrl+Alt+K
    st->count = 1;
    st->keys[0].vKey = 'K'; st->keys[0].mods = SMOD_CTRL | SMOD_ALT;

    // IME 옵션 기본값
    config->options.fullWidth = false;
    config->options.jamoDelete = true;         // 백스페이스 = 자소 단위 삭제
    config->options.showPreview = true;        // 조합 미리보기 오버레이 (RFC-0002)
    wcscpy(config->options.previewFont, L"Malgun Gothic");
    config->options.previewFontSize = 0;       // 0 = Auto(캐럿 높이)
}

// 트리거 vKey가 모디파이어 키 자신이면 해당 모디파이어 비트 (자기 비트는 매칭에서 제외해야 함).
static UINT VKToModBit(UINT vk) {
    switch (vk) {
        case VK_LSHIFT: case VK_RSHIFT: case VK_SHIFT:     return SMOD_SHIFT;
        case VK_LCONTROL: case VK_RCONTROL: case VK_CONTROL: return SMOD_CTRL;
        case VK_LMENU: case VK_RMENU: case VK_MENU:        return SMOD_ALT;
        case VK_LWIN: case VK_RWIN:                        return SMOD_GUI;
    }
    return 0;
}

UINT Config_ResolveVK(WPARAM wParam, LPARAM lParam) {
    UINT sc = (UINT)((lParam >> 16) & 0xFF);
    BOOL ext = (BOOL)((lParam >> 24) & 1);
    switch (wParam) {
        case VK_SHIFT:   return (sc == 0x36) ? VK_RSHIFT : VK_LSHIFT;   // 좌우 Shift는 스캔코드로
        case VK_CONTROL: return ext ? VK_RCONTROL : VK_LCONTROL;
        case VK_MENU:    return ext ? VK_RMENU : VK_LMENU;
    }
    return (UINT)wParam;
}

UINT Config_CurrentMods(void) {
    UINT m = 0;
    if (GetKeyState(VK_SHIFT)   & 0x8000) m |= SMOD_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= SMOD_CTRL;
    if (GetKeyState(VK_MENU)    & 0x8000) m |= SMOD_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) m |= SMOD_GUI;
    return m;
}

// 한 단축키가 (좌우 구분 vKey, 모디파이어 mods) 키 이벤트와 일치하는가.
bool Config_MatchShortcut(const ShortcutKey *sk, UINT vKey, UINT mods) {
    if (!sk->vKey) return false;
    UINT eff = mods & ~VKToModBit(vKey);   // 트리거가 모디파이어 자신이면 자기 비트 제외
    return sk->vKey == vKey && sk->mods == eff;
}

bool Config_IsShortcut(const JamotongConfig *config, ShortcutFn fn, UINT vKey, UINT mods) {
    if (fn < 0 || fn >= SC_FN_COUNT) return false;
    const ShortcutList *sl = &config->shortcuts[fn];
    for (int i = 0; i < sl->count && i < SHORTCUTS_MAX; i++) {
        if (Config_MatchShortcut(&sl->keys[i], vKey, mods)) return true;
    }
    return false;
}

void Config_RotateLayout(JamotongConfig *config) {
    EnterCriticalSection(&g_configLock);
    int n = config->layoutCount;
    if (n > 0) {
        // 다음 '켜진(enabled)' 레이아웃으로 순환. 하나만 켜져 있으면 자기 자신으로 되돌아온다.
        for (int i = 1; i <= n; i++) {
            int idx = (config->currentLayoutIndex + i) % n;
            if (config->layouts[idx].enabled) { config->currentLayoutIndex = idx; break; }
        }
    }
    LeaveCriticalSection(&g_configLock);
}

LayoutConfig* Config_GetCurrentLayout(JamotongConfig *config) {
    if (config->layoutCount <= 0) return NULL;
    if (config->currentLayoutIndex < 0 || config->currentLayoutIndex >= config->layoutCount)
        config->currentLayoutIndex = 0;   // 손상된 인덱스 방어 (OOB 방지)
    return &config->layouts[config->currentLayoutIndex];
}

// ── 레이아웃 리소스 소유권 ────────────────────────────────────────────────────────
// 원칙: 실제 리소스(플러그인 DLL/컨텍스트, heap name)는 "live" config 하나만 소유한다.
// 설정 UI의 g_TempConfig 등 값복사본은 포인터를 공유하되 절대 해제하지 않는다. 해제는
// (1) live 파괴 시 Config_Free, (2) 설정 적용/취소 시 reconcile에서만 일어난다.
// 동일 리소스 판별은 name 포인터로 한다(모든 name이 유일한 heap 포인터).

static void Layout_FreeResources(LayoutConfig *L) {
    if (L->type == LAYOUT_TYPE_DLL_PLUGIN) {
        if (L->pfnUninitialize && L->pvPluginContext) L->pfnUninitialize(L->pvPluginContext);
        if (L->hPluginModule) FreeLibrary(L->hPluginModule);
        L->pvPluginContext = NULL; L->hPluginModule = NULL;
    }
    if (L->pHangulLayout) { HangulLayout_Free((HangulLayout*)L->pHangulLayout); L->pHangulLayout = NULL; }
    if (L->pChordLayout) { ChordLayout_Free((ChordLayout*)L->pChordLayout); L->pChordLayout = NULL; }
    if (L->name) { free((void*)L->name); L->name = NULL; }
}
static bool Config_HasLayout(const JamotongConfig *cfg, const LayoutConfig *L) {
    if (!L->name) return false;
    for (int i = 0; i < cfg->layoutCount; i++)
        if (cfg->layouts[i].name == L->name) return true;   // 같은 리소스 = 같은 name 포인터
    return false;
}
// live config의 모든 리소스 해제 (객체 파괴 시).
void Config_Free(JamotongConfig *cfg) {
    EnterCriticalSection(&g_configLock);
    for (int i = 0; i < cfg->layoutCount; i++) Layout_FreeResources(&cfg->layouts[i]);
    cfg->layoutCount = 0;
    LeaveCriticalSection(&g_configLock);
}
// 설정 적용: edited가 떨어낸(삭제/교체) live 레이아웃의 리소스만 해제한 뒤 edited를 채택.
void Config_ApplyEdited(JamotongConfig *live, const JamotongConfig *edited) {
    EnterCriticalSection(&g_configLock);   // 입력 스레드의 현재-레이아웃 사용과 직렬화 → 플러그인 free 안전
    for (int i = 0; i < live->layoutCount; i++)
        if (!Config_HasLayout(edited, &live->layouts[i])) Layout_FreeResources(&live->layouts[i]);
    *live = *edited;
    // enabled 자판이 하나도 없으면 첫 자판을 켠다 — 회전이 영구히 먹통이 되는 0-enabled 상태 방어
    // (삭제 경로 등으로 만들어질 수 있었음, RFC-0004 P0-3).
    if (live->layoutCount > 0) {
        bool any = false;
        for (int i = 0; i < live->layoutCount; i++)
            if (live->layouts[i].enabled) { any = true; break; }
        if (!any) live->layouts[0].enabled = true;
    }
    // 현재 활성 자판이 꺼졌으면 켜진 첫 자판으로 이동 (꺼진 자판이 활성으로 남지 않도록)
    if (live->layoutCount > 0 &&
        (live->currentLayoutIndex < 0 || live->currentLayoutIndex >= live->layoutCount ||
         !live->layouts[live->currentLayoutIndex].enabled)) {
        for (int i = 0; i < live->layoutCount; i++)
            if (live->layouts[i].enabled) { live->currentLayoutIndex = i; break; }
    }
    LeaveCriticalSection(&g_configLock);
}
// 설정 취소/폐기: live가 소유하지 않는(예: import로 새로 만든) edited 리소스만 해제.
void Config_DiscardEdited(JamotongConfig *edited, const JamotongConfig *live) {
    EnterCriticalSection(&g_configLock);
    for (int i = 0; i < edited->layoutCount; i++)
        if (!Config_HasLayout(live, &edited->layouts[i])) Layout_FreeResources(&edited->layouts[i]);
    edited->layoutCount = 0;
    LeaveCriticalSection(&g_configLock);
}

void Config_FreeLayoutResources(LayoutConfig *L) { Layout_FreeResources(L); }

// 편집본에서 idx 자판 제거 (RFC-0004 P0-3): live가 소유하지 않은 리소스는 shift로 사라지기
// 전에 여기서 해제한다. enabled 불변식(최소 1개)은 호출자(설정 UI)가 검사한다.
void Config_RemoveEditedLayout(JamotongConfig *edited, int idx, const JamotongConfig *live) {
    if (idx < 0 || idx >= edited->layoutCount) return;
    if (!live || !Config_HasLayout(live, &edited->layouts[idx]))
        Layout_FreeResources(&edited->layouts[idx]);
    for (int i = idx; i < edited->layoutCount - 1; i++)
        edited->layouts[i] = edited->layouts[i + 1];
    edited->layoutCount--;
}

// 사용자 설정 파일 경로: %APPDATA%\Jamotong\config.ini (디렉터리 없으면 생성).
//   모든 TIP 인스턴스가 Create에서 이걸 로드하고, 설정창 Apply가 여기에 저장 → 세션·프로세스
//   간 설정 공유(설정 "옵션" 버튼이 실제로 동작하려면 필수).
bool Config_UserPath(wchar_t *out, int cch) {
    if (!out || cch < 8) return false;
    wchar_t appdata[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    wchar_t dir[MAX_PATH];
    _snwprintf(dir, MAX_PATH, L"%ls\\Jamotong", appdata);
    dir[MAX_PATH - 1] = L'\0';   // _snwprintf 잘림 시 널 종료 보장
    CreateDirectoryW(dir, NULL);   // 이미 있으면 조용히 실패(무시)
    _snwprintf(out, cch, L"%ls\\config.ini", dir);
    out[cch - 1] = L'\0';
    return true;
}

// 사용자 자판 저장소 %APPDATA%\Jamotong\layouts (없으면 생성). 설정창 Add가 여기로 복사하고
// PluginLoader_LoadAll이 시작 시 자동 로드한다 (외부 .jmt 영속화, RFC-0004 P0-2).
bool Config_UserLayoutDir(wchar_t *out, int cch) {
    if (!out || cch < 8) return false;
    wchar_t appdata[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    wchar_t dir[MAX_PATH];
    _snwprintf(dir, MAX_PATH, L"%ls\\Jamotong", appdata);
    dir[MAX_PATH - 1] = L'\0';
    CreateDirectoryW(dir, NULL);
    _snwprintf(out, cch, L"%ls\\layouts", dir);
    out[cch - 1] = L'\0';
    CreateDirectoryW(out, NULL);
    return true;
}

// 설정 파일 [Shortcuts] 섹션의 기능별 키 이름 (ShortcutFn 인덱스)
static const wchar_t *SC_NAMES[SC_FN_COUNT] = { L"Rotate", L"Hanja", L"Code", L"Settings" };

// Windows 예약 장치 기본 이름(확장자를 붙여도 여전히 장치로 해석됨: CON.jmt → CON 장치).
static bool IsReservedDeviceBase(const wchar_t *name, size_t baseLen) {
    static const wchar_t *dev3[] = { L"CON", L"PRN", L"AUX", L"NUL" };
    static const wchar_t *dev4[] = { L"COM", L"LPT" };   // + 1~9 숫자
    if (baseLen == 3) {
        for (size_t i = 0; i < 4; i++)
            if (_wcsnicmp(name, dev3[i], 3) == 0) return true;
    } else if (baseLen == 4 && name[3] >= L'1' && name[3] <= L'9') {
        for (size_t i = 0; i < 2; i++)
            if (_wcsnicmp(name, dev4[i], 3) == 0) return true;
    }
    return false;
}

bool Config_IsSafeLayoutFileName(const wchar_t *name) {
    if (!name || !name[0]) return false;
    if (wcspbrk(name, L"\\/:")) return false;   // 경로 구분자/드라이브/ADS 금지 → 상위 이동 차단
    size_t n = wcslen(name);
    if (n < 5) return false;                    // "x.jmt" 최소 길이
    if (_wcsicmp(name + n - 4, L".jmt") != 0) return false;   // .jmt 확장자 강제
                                                             //   (끝이 't'라 후행 점/공백도 자동 배제)
    size_t base = n - 4;
    if (IsReservedDeviceBase(name, base)) return false;   // CON.jmt/COM1.jmt 등 장치명 금지
    // 경로 구분자가 없으므로 파일명 내부의 '..'(예: v2..jmt)는 상위 이동을 못 해 안전 → 허용.
    //   단 basename(확장자 제외)이 점/공백뿐이면 거부('.'/'..' 등 특수 항목 트릭 방지).
    for (size_t i = 0; i < base; i++)
        if (name[i] != L'.' && name[i] != L' ') return true;   // 정상 문자 하나라도 있으면 안전
    return false;
}

// [LayoutFile:name] 헤더는 ']'에서 끝난다(파서 `%127l[^]]`). 파일명에 ']'가 있으면 헤더가
//   잘리므로, ']'와 디코딩 마커 '%'를 percent-encode 해서 실어 라운드트립 가능하게 한다.
//   그 밖의 문자(공백·'['·유니코드 등)는 그대로 둔다.
void Config_EncodeLayoutName(const wchar_t *in, wchar_t *out, size_t cch) {
    static const wchar_t *hex = L"0123456789ABCDEF";
    size_t o = 0;
    for (size_t i = 0; in[i]; i++) {
        if (o + 4 >= cch) break;
        if (in[i] == L'%' || in[i] == L']') {
            out[o++] = L'%';
            out[o++] = hex[(in[i] >> 4) & 0xF];
            out[o++] = hex[in[i] & 0xF];
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = L'\0';
}

static int HexDigit(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    return -1;
}

// Config_EncodeLayoutName 의 역변환(제자리, 항상 축소되므로 안전). 임의의 %XX 를 디코드하되,
//   결과 파일명은 반드시 Config_IsSafeLayoutFileName 으로 다시 검사한다(디코드가 만든 '\\' 등 차단).
void Config_DecodeLayoutName(wchar_t *s) {
    wchar_t *r = s, *w = s;
    while (*r) {
        int hi, lo;
        if (r[0] == L'%' && (hi = HexDigit(r[1])) >= 0 && (lo = HexDigit(r[2])) >= 0) {
            *w++ = (wchar_t)(hi * 16 + lo);
            r += 3;
        } else {
            *w++ = *r++;
        }
    }
    *w = L'\0';
}

static void TrimCrLf(wchar_t *str) {
    size_t len = wcslen(str);
    while (len > 0 && (str[len - 1] == L'\n' || str[len - 1] == L'\r')) {
        str[len - 1] = L'\0';
        len--;
    }
}

// 사용자 자판 저장소의 모든 .jmt를 [LayoutFile:name] … [EndLayoutFile] 로 인라인 (Export용).
static void BundleUserLayouts(FILE *fp) {
    wchar_t dir[MAX_PATH];
    if (!Config_UserLayoutDir(dir, MAX_PATH)) return;
    wchar_t pat[MAX_PATH];
    _snwprintf(pat, MAX_PATH, L"%ls\\*.jmt", dir);
    pat[MAX_PATH - 1] = L'\0';   // _snwprintf 잘림 시 널 종료 보장
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        // 복원 시 안전한 파일명만 번들(예약 장치명 등은 복원해도 위험하므로 제외). 파서를 깨는
        //   ']'/'%'는 아래 Config_EncodeLayoutName 이 이스케이프하므로 여기서 드롭하지 않는다.
        if (!Config_IsSafeLayoutFileName(fd.cFileName))
            continue;
        wchar_t full[MAX_PATH];
        _snwprintf(full, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
        full[MAX_PATH - 1] = L'\0';   // _snwprintf 잘림 시 널 종료 보장
        FILE *lf = _wfopen(full, L"r, ccs=UTF-8");
        if (!lf) continue;
        wchar_t encName[MAX_PATH * 3];   // percent-encoding은 최대 3배
        Config_EncodeLayoutName(fd.cFileName, encName, MAX_PATH * 3);
        fwprintf(fp, L"\n[LayoutFile:%ls]\n", encName);
        wchar_t line[512];
        // 본문 각 줄 앞에 공백 마커를 붙인다 — 복원 시 첫 칸을 벗긴다. 본문 안의 '['로
        //   시작하는 줄이 상위 config 파서의 섹션 헤더나 [EndLayoutFile] 센티넬로 오인되어
        //   라운드트립이 깨지는 것을 막는다.
        while (fgetws(line, 512, lf)) {
            TrimCrLf(line);
            fwprintf(fp, L" %ls\n", line);
        }
        fwprintf(fp, L"[EndLayoutFile]\n");
        fclose(lf);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

bool Config_SaveToFile(JamotongConfig *config, const wchar_t *filepath, bool bundleLayouts) {
    FILE *fp = _wfopen(filepath, L"w, ccs=UTF-8");
    if (!fp) return false;

    fwprintf(fp, L"[Layouts]\nCount=%d\n", config->layoutCount);
    for (int i = 0; i < config->layoutCount; i++) {
        fwprintf(fp, L"%d_Type=%d\n", i, config->layouts[i].type);
        fwprintf(fp, L"%d_Name=%ls\n", i, config->layouts[i].name ? config->layouts[i].name : L"");
        fwprintf(fp, L"%d_Enabled=%d\n", i, config->layouts[i].enabled ? 1 : 0);
    }
    
    // 기능별 단축키: <기능이름>Count / <기능이름><i>_Key / <기능이름><i>_Mods
    fwprintf(fp, L"\n[Shortcuts]\n");
    for (int f = 0; f < SC_FN_COUNT; f++) {
        const ShortcutList *sl = &config->shortcuts[f];
        fwprintf(fp, L"%lsCount=%d\n", SC_NAMES[f], sl->count);
        for (int i = 0; i < sl->count && i < SHORTCUTS_MAX; i++) {
            fwprintf(fp, L"%ls%d_Key=%u\n", SC_NAMES[f], i, sl->keys[i].vKey);
            fwprintf(fp, L"%ls%d_Mods=%u\n", SC_NAMES[f], i, sl->keys[i].mods);
        }
    }

    fwprintf(fp, L"\n[Options]\n");
    fwprintf(fp, L"FullWidth=%d\n", config->options.fullWidth ? 1 : 0);
    fwprintf(fp, L"JamoDelete=%d\n", config->options.jamoDelete ? 1 : 0);
    fwprintf(fp, L"ShowPreview=%d\n", config->options.showPreview ? 1 : 0);
    fwprintf(fp, L"PreviewFontSize=%d\n", config->options.previewFontSize);
    fwprintf(fp, L"PreviewFont=%ls\n", config->options.previewFont[0] ? config->options.previewFont : L"Malgun Gothic");

    if (bundleLayouts) BundleUserLayouts(fp);   // Export: 사용자 자판 .jmt 본문 인라인

    fclose(fp);
    return true;
}

// 설정 파일 로드 = '병합(merge)'. 파일에는 메타데이터(자판 이름/켜짐/순서·단축키·옵션)만 있다.
//   실제 자판 리소스(charMap·HangulLayout·플러그인 포인터)는 *config(기본+플러그인 로드본)의 것을
//   그대로 쓰고, 파일의 순서/켜짐만 이름 매칭으로 반영한다.
//   ※ 통째 대입(*config = temp)이었던 구버전의 두 버그를 고침:
//     (1) 기존 config의 heap name/플러그인/HangulLayout 리소스가 전부 누수됐고,
//     (2) 파일에서 온 자판은 리소스가 빈 껍데기라(드보락 charMap 소실, 플러그인 pfn=NULL 호출
//         크래시 위험) 실사용이 깨졌다. 파일에만 있고 현재 없는 자판은 무시한다.
bool Config_LoadFromFile(JamotongConfig *config, const wchar_t *filepath) {
    FILE *fp = _wfopen(filepath, L"r, ccs=UTF-8");
    if (!fp) return false;

    JamotongConfig temp = {0};
    temp.options.jamoDelete = true;   // [Options] 없는 .ini 대비 기본값
    temp.options.showPreview = true; // 구버전 .ini 대비 기본 켜짐 (RFC-0002)
    temp.options.previewFontSize = 0;   // 기본 Auto
    wcscpy(temp.options.previewFont, L"Malgun Gothic");
    bool haveSc[SC_FN_COUNT] = { false };   // 파일에 해당 기능 목록이 명시됐는가 (없으면 기존 유지)
    wchar_t line[256];
    wchar_t fontBuf[32];
    int section = 0; // 1 = Layouts, 2 = Shortcuts, 3 = Options

    // 번들 자판 복원용: 디렉터리는 한 번만 해석(섹션마다 env 읽기+CreateDirectory 반복 방지),
    //   복원 개수는 자판 배열 크기(8)로 제한 — 적대적 config가 사용자 자판을 밀어내지 못하게.
    wchar_t layoutDir[MAX_PATH];
    bool haveLayoutDir = Config_UserLayoutDir(layoutDir, MAX_PATH);
    int lfRestored = 0;

    while (fgetws(line, 256, fp)) {
        TrimCrLf(line);
        // [LayoutFile:name] … [EndLayoutFile] : Export 번들의 사용자 자판 본문 복원(P0-2 남은 절반).
        //   layouts 폴더에 파일이 없을 때만 쓴다(기존 자판 덮어쓰기 방지). 다음 시작 시 자동 로드.
        wchar_t lfName[128];
        if (swscanf(line, L"[LayoutFile:%127l[^]]", lfName) == 1) {
            Config_DecodeLayoutName(lfName);   // Export 의 ']'/'%' percent-encoding 복원
            wchar_t dst[MAX_PATH];
            FILE *out = NULL;
            // 파일명 안전성 검사: 신뢰 못 할 config.ini 를 Import 할 때 [LayoutFile:...] 이름은
            //   공격자가 100% 제어한다. basename + .jmt 인 경우에만 복원 — 이 검사가 없으면
            //   `..\..\...\Startup\evil.bat` 같은 이름으로 %APPDATA%\Jamotong\layouts 밖
            //   (예: 시작프로그램 폴더)에 임의 파일을 심을 수 있다. 복원 개수도 자판 배열
            //   크기(8)로 제한 — 적대적 config가 사용자 자판을 목록에서 밀어내지 못하게.
            if (haveLayoutDir && lfRestored < 8 && Config_IsSafeLayoutFileName(lfName)) {
                _snwprintf(dst, MAX_PATH, L"%ls\\%ls", layoutDir, lfName);
                dst[MAX_PATH - 1] = L'\0';   // _snwprintf 잘림 시 널 종료 보장
                if (GetFileAttributesW(dst) == INVALID_FILE_ATTRIBUTES) {   // 없을 때만 복원
                    out = _wfopen(dst, L"w, ccs=UTF-8");
                    if (out) lfRestored++;
                }
            }
            // 본문은 Export 와 같은 512 버퍼로 읽어 긴 줄이 쪼개지지 않게 한다(마커 격리가
            //   연속 청크에서 깨지는 것을 막음). 각 줄은 선두 공백 마커 — 첫 칸만 벗겨 복원.
            //   본문에 '['로 시작하는 줄이 있어도 상위 섹션/센티넬로 오인되지 않는다.
            wchar_t body[512];
            while (fgetws(body, 512, fp)) {   // [EndLayoutFile]까지 본문 (있으면 파일에 씀)
                TrimCrLf(body);
                if (wcscmp(body, L"[EndLayoutFile]") == 0) break;
                if (out) fwprintf(out, L"%ls\n", body[0] == L' ' ? body + 1 : body);
            }
            if (out) fclose(out);
            section = 0;
            continue;
        }
        if (wcscmp(line, L"[Layouts]") == 0) section = 1;
        else if (wcscmp(line, L"[Shortcuts]") == 0) section = 2;
        else if (wcscmp(line, L"[Options]") == 0) section = 3;
        else if (section == 1) {
            int count;
            if (swscanf(line, L"Count=%d", &count) == 1) temp.layoutCount = count < 0 ? 0 : (count > 8 ? 8 : count);   // 배열 크기(8) 클램프 — 조작된 .ini 오버런 방지
            else {
                int idx, val;
                wchar_t nameBuf[64];
                if (swscanf(line, L"%d_Type=%d", &idx, &val) == 2 && (unsigned)idx < 8) {
                    temp.layouts[idx].type = (LayoutType)val;
                // %l[ 필수: C 표준상 swscanf의 %[ 는 'l' 없이는 char* 대상(glibc가 그렇게 동작).
                // MSVCRT는 %l[ 도 wide로 동일 처리하므로 양쪽 CRT에서 안전.
                } else if (swscanf(line, L"%d_Name=%63l[^\n]", &idx, nameBuf) == 2 && (unsigned)idx < 8) {
                    free((void*)temp.layouts[idx].name);   // 중복 Name 줄이면 이전 것 해제 (누수 방지; NULL이면 no-op)
                    temp.layouts[idx].name = _wcsdup(nameBuf);   // 균일 heap 소유 (Config_Free/reconcile가 관리)
                } else if (swscanf(line, L"%d_Enabled=%d", &idx, &val) == 2 && (unsigned)idx < 8) {
                    temp.layouts[idx].enabled = (val != 0);
                }
            }
        }
        else if (section == 2) {
            int idx, val;
            wchar_t fmt[48];
            bool hit = false;
            for (int f = 0; f < SC_FN_COUNT && !hit; f++) {   // 기능별 이름: RotateCount / Rotate0_Key / ...
                ShortcutList *sl = &temp.shortcuts[f];
                swprintf(fmt, 48, L"%lsCount=%%d", SC_NAMES[f]);
                if (swscanf(line, fmt, &val) == 1) {
                    sl->count = val < 0 ? 0 : (val > SHORTCUTS_MAX ? SHORTCUTS_MAX : val);   // 배열 크기 클램프
                    haveSc[f] = true; hit = true; break;
                }
                swprintf(fmt, 48, L"%ls%%d_Key=%%d", SC_NAMES[f]);
                if (swscanf(line, fmt, &idx, &val) == 2 && (unsigned)idx < SHORTCUTS_MAX) {
                    sl->keys[idx].vKey = (UINT)val; hit = true; break;
                }
                swprintf(fmt, 48, L"%ls%%d_Mods=%%d", SC_NAMES[f]);
                if (swscanf(line, fmt, &idx, &val) == 2 && (unsigned)idx < SHORTCUTS_MAX) {
                    sl->keys[idx].mods = (UINT)val; hit = true; break;
                }
            }
            if (!hit) {   // 구형식(~v0.11): Count/0_Key/0_Mods = 자판 전환 목록
                ShortcutList *rot = &temp.shortcuts[SC_FN_ROTATE];
                if (swscanf(line, L"Count=%d", &val) == 1) {
                    rot->count = val < 0 ? 0 : (val > SHORTCUTS_MAX ? SHORTCUTS_MAX : val);
                    haveSc[SC_FN_ROTATE] = true;
                }
                else if (swscanf(line, L"%d_Key=%d", &idx, &val) == 2 && (unsigned)idx < SHORTCUTS_MAX) rot->keys[idx].vKey = (UINT)val;
                else if (swscanf(line, L"%d_Mods=%d", &idx, &val) == 2 && (unsigned)idx < SHORTCUTS_MAX) rot->keys[idx].mods = (UINT)val;
            }
        }
        else if (section == 3) {
            int val;
            // 구형식(~v0.11): [Options]의 HanjaKey/HanjaMods 단일 한자키 → 한자 목록[0]으로 승격
            if (swscanf(line, L"HanjaKey=%d", &val) == 1) {
                temp.shortcuts[SC_FN_HANJA].keys[0].vKey = (UINT)val;
                if (temp.shortcuts[SC_FN_HANJA].count < 1) temp.shortcuts[SC_FN_HANJA].count = 1;
                haveSc[SC_FN_HANJA] = true;
            }
            else if (swscanf(line, L"HanjaMods=%d", &val) == 1) {
                temp.shortcuts[SC_FN_HANJA].keys[0].mods = (UINT)val;
                if (temp.shortcuts[SC_FN_HANJA].count < 1) temp.shortcuts[SC_FN_HANJA].count = 1;
                haveSc[SC_FN_HANJA] = true;
            }
            else if (swscanf(line, L"FullWidth=%d", &val) == 1) temp.options.fullWidth = (val != 0);
            else if (swscanf(line, L"JamoDelete=%d", &val) == 1) temp.options.jamoDelete = (val != 0);
            // (구버전 .ini의 LegacyImm= 줄은 무시됨 — 옵션 제거, 2026-07-07)
            else if (swscanf(line, L"ShowPreview=%d", &val) == 1) temp.options.showPreview = (val != 0);
            else if (swscanf(line, L"PreviewFontSize=%d", &val) == 1)
                temp.options.previewFontSize = (val <= 0) ? 0 : (val < 8 ? 8 : (val > 96 ? 96 : val));
            else if (swscanf(line, L"PreviewFont=%31l[^\n]", fontBuf) == 1 && fontBuf[0]) {
                wcsncpy(temp.options.previewFont, fontBuf, 31); temp.options.previewFont[31] = L'\0';
            }
        }
    }
    
    fclose(fp);

    // ── 병합: 파일 순서대로, 현재 config에 '이름이 같은' 자판을 찾아 재배열 + enabled 반영 ──
    JamotongConfig merged = *config;   // 값 복사(리소스 포인터 공유 — 해제 없음)
    merged.layoutCount = 0;
    bool used[8] = { false };
    for (int i = 0; i < temp.layoutCount && merged.layoutCount < 8; i++) {
        if (!temp.layouts[i].name) continue;
        for (int j = 0; j < config->layoutCount; j++) {
            if (!used[j] && config->layouts[j].name &&
                wcscmp(config->layouts[j].name, temp.layouts[i].name) == 0) {
                merged.layouts[merged.layoutCount] = config->layouts[j];
                merged.layouts[merged.layoutCount].enabled = temp.layouts[i].enabled;
                merged.layoutCount++;
                used[j] = true;
                break;
            }
        }
        // 매칭 실패(파일에만 있는 자판) → 무시: 빈 껍데기/NULL 플러그인 방지
    }
    for (int j = 0; j < config->layoutCount && merged.layoutCount < 8; j++)   // 파일에 없는(새) 자판은 뒤에 유지
        if (!used[j]) merged.layouts[merged.layoutCount++] = config->layouts[j];
    for (int i = 0; i < 8; i++)   // 매칭용 임시 이름 해제 — Count가 엔트리보다 작은 기형 파일도 전체 해제
        if (temp.layouts[i].name) free((void*)temp.layouts[i].name);

    for (int f = 0; f < SC_FN_COUNT; f++) {
        // 파일에 명시된 기능 목록만 교체. 자판 전환은 0개가 되면 자판을 못 바꾸므로 0이면 기존 유지.
        if (haveSc[f] && (f != SC_FN_ROTATE || temp.shortcuts[f].count > 0))
            merged.shortcuts[f] = temp.shortcuts[f];
    }
    merged.options = temp.options;

    merged.currentLayoutIndex = 0;   // 첫 '켜진' 자판에서 시작 (없으면 0)
    for (int i = 0; i < merged.layoutCount; i++)
        if (merged.layouts[i].enabled) { merged.currentLayoutIndex = i; break; }

    *config = merged;
    return true;
}
