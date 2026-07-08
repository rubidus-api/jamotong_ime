#pragma once
#include <windows.h>
#include <stdbool.h>

// live config 접근 직렬화용 락 (재진입 가능). 쓰기(ApplyEdited/Free/Rotate)는 함수 내부에서,
// 읽기(현재 레이아웃 get+use)는 호출자(키 핸들러·언어바)에서 획득. dllmain.c에서 초기화.
extern CRITICAL_SECTION g_configLock;

// 전환 단축키 모디파이어 비트마스크 (좌우 무관 = 어느 쪽이든 매치)
#define SMOD_SHIFT 0x01
#define SMOD_CTRL  0x02
#define SMOD_ALT   0x04
#define SMOD_GUI   0x08

// 단축키: 트리거 가상키 + 함께 눌러야 할 모디파이어.
//  vKey 는 좌우를 구분한다 (VK_RMENU=오른쪽 Alt, VK_HANGUL=한/영, VK_SPACE 등). 그래서 "오른쪽
//  Alt만"(vKey=VK_RMENU, mods=0) 같은 것이 표현된다. mods 는 좌우 무관 조합키(Shift+Space 등).
typedef struct {
    UINT vKey;
    UINT mods;   // SMOD_* 비트마스크
} ShortcutKey;

// 단축키가 걸리는 기능. 설정창 Shortcuts 탭의 기능 콤보 순서와 동일해야 한다.
typedef enum {
    SC_FN_ROTATE = 0,   // 자판 전환 (한/영 토글)
    SC_FN_HANJA  = 1,   // 한자/특수문자 변환
    SC_FN_CODE   = 2,   // 유니코드 코드 포인트 직접 입력 (기본 Ctrl+Alt+U)
    SC_FN_COUNT
} ShortcutFn;

#define SHORTCUTS_MAX 8

// 한 기능에 배정된 단축키 목록 (모든 기능이 복수 단축키 허용)
typedef struct {
    ShortcutKey keys[SHORTCUTS_MAX];
    int count;
} ShortcutList;

#include "jamotong_plugin.h"

// 레이아웃 종류 (하이브리드 아키텍처 준비)
typedef enum {
    LAYOUT_TYPE_PASSTHROUGH = 0,
    LAYOUT_TYPE_KOREAN_FSM = 1,
    LAYOUT_TYPE_STATIC_MAP = 2, // .jamo 1:1 텍스트 매핑 자판
    LAYOUT_TYPE_DLL_PLUGIN = 3, // .dll 외부 라이브러리 엔진
    LAYOUT_TYPE_HANGUL_CUSTOM = 4, // .jmt 설정파일 기반 사용자 한글 자판(세벌식 계열·결합규칙)
    LAYOUT_TYPE_CHORD = 5       // .cord 설정파일 기반 일반 코드 자판(ARTSEY류 조합→출력)
} LayoutType;

// 레이아웃 메타데이터
typedef struct {
    LayoutType type;
    int kbdVariant;      // KOREAN_FSM일 때 자판 종류 (KBD_DUBEOL/KBD_SEBEOL, layout.h)
    const wchar_t *name; // C 변수 스타일 식별자. e.g. "en_qwerty", "ko_2bul", "ko_3bul", "en_dvorak"
    wchar_t abbrev[8];   // 언어창/트레이 아이콘용 식별자. 1~4글자를 2x2 격자로 렌더. e.g. "EN","Dv","2벌","eSt","ART". (.jmt 필수)
    
    // LAYOUT_TYPE_STATIC_MAP용 매핑 테이블 (QWERTY 기준 ASCII -> 변환 문자)
    wchar_t charMap[256];
    
    // LAYOUT_TYPE_HANGUL_CUSTOM용 로드된 자판 (HangulLayout*). live config가 소유.
    void* pHangulLayout;
    // LAYOUT_TYPE_CHORD용 로드된 코드 자판 (ChordLayout*). live config가 소유.
    void* pChordLayout;

    // LAYOUT_TYPE_DLL_PLUGIN용 함수 포인터 및 컨텍스트
    HMODULE hPluginModule;
    void* pvPluginContext;
    PFN_JamoPlugin_Initialize pfnInitialize;
    PFN_JamoPlugin_ProcessKey pfnProcessKey;
    PFN_JamoPlugin_Flush pfnFlush;
    PFN_JamoPlugin_Command pfnCommand;
    PFN_JamoPlugin_Uninitialize pfnUninitialize;

    bool enabled;   // 전환 순환에 포함되는가 (설정 체크박스). 기본 켜짐: en_qwerty, ko_2bul 만.
} LayoutConfig;

// IME 동작 옵션 (설정창 'IME Options' 탭). 단축키류는 JamotongConfig.shortcuts 로 통합.
typedef struct {
    bool fullWidth;         // 전각 입력 (영문/기호를 전각 폭 U+FF01~ 으로)
    bool jamoDelete;        // 백스페이스 = 자소 단위 삭제 (끄면 조합 음절 전체 삭제). 기본 켜짐.
    bool showPreview;       // 조합 미리보기 플로팅 오버레이 (RFC-0002). 기본 켜짐.
    wchar_t previewFont[32];// 미리보기 글꼴 face 이름 (32 = LF_FACESIZE). 기본 "Malgun Gothic".
    int previewFontSize;    // 미리보기 글꼴 크기(px). 0=Auto(캐럿 높이 근사), 8~96=고정.
} ImeOptions;

// 글로벌 환경 설정 매니저
typedef struct {
    ShortcutList shortcuts[SC_FN_COUNT];   // 기능별 단축키 목록 (ShortcutFn 인덱스)

    LayoutConfig layouts[8];
    int layoutCount;
    int currentLayoutIndex;

    ImeOptions options;
} JamotongConfig;

// 기본 설정 로드 (임시 하드코딩, 향후 ini 파일 파싱으로 대체)
void Config_LoadDefault(JamotongConfig *config);

// 현재 키 이벤트가 fn 기능의 단축키 목록 중 하나와 일치하는지. vKey/mods 는 아래 헬퍼로 해석해 넘긴다.
bool Config_IsShortcut(const JamotongConfig *config, ShortcutFn fn, UINT vKey, UINT mods);
// 단일 단축키가 현재 키 이벤트와 일치하는지.
bool Config_MatchShortcut(const ShortcutKey *sk, UINT vKey, UINT mods);
// wParam+lParam(스캔코드/확장비트)로 좌우 구분 가상키 해석 (VK_MENU→VK_LMENU/VK_RMENU 등).
UINT Config_ResolveVK(WPARAM wParam, LPARAM lParam);
// 현재 눌린 모디파이어 비트마스크 (SMOD_*, 좌우 무관).
UINT Config_CurrentMods(void);

// 다음 레이아웃으로 순환
void Config_RotateLayout(JamotongConfig *config);

// 현재 레이아웃 가져오기
LayoutConfig* Config_GetCurrentLayout(JamotongConfig *config);

// 설정 내보내기 및 가져오기 (텍스트 형식)
bool Config_SaveToFile(JamotongConfig *config, const wchar_t *filepath);
bool Config_LoadFromFile(JamotongConfig *config, const wchar_t *filepath);
bool Config_UserPath(wchar_t *out, int cch);   // %APPDATA%\Jamotong\config.ini (자동 저장/로드용)

// 리소스 소유권 (플러그인 DLL/컨텍스트, heap name). live config만 소유 — 자세히는 config.c 참조.
void Config_Free(JamotongConfig *cfg);                                       // live 파괴 시 전체 해제
void Config_ApplyEdited(JamotongConfig *live, const JamotongConfig *edited); // 설정 적용(떨어낸 것 해제 후 채택)
void Config_DiscardEdited(JamotongConfig *edited, const JamotongConfig *live);// 설정 취소(비-live 리소스만 해제)
