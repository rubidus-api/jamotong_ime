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
    SC_FN_ROTATE   = 0,   // 자판 전환 (한/영 토글)
    SC_FN_HANJA    = 1,   // 한자/특수문자 변환
    SC_FN_CODE     = 2,   // 유니코드 코드 포인트 직접 입력 (기본 Ctrl+Alt+U)
    SC_FN_SETTINGS = 3,   // 설정 창 열기 (기본 Ctrl+Alt+K)
    SC_FN_PASSTHROUGH = 4, // 무간섭(직접 입력) 모드 토글 — 원격 데스크톱 등 (기본 Ctrl+Alt+P)
    SC_FN_COUNT
} ShortcutFn;

#define SHORTCUTS_MAX 8

// 한 기능에 배정된 단축키 목록 (모든 기능이 복수 단축키 허용)
typedef struct {
    ShortcutKey keys[SHORTCUTS_MAX];
    int count;
} ShortcutList;

// 레이아웃 종류 (하이브리드 아키텍처 준비)
typedef enum {
    LAYOUT_TYPE_PASSTHROUGH = 0,
    LAYOUT_TYPE_KOREAN_FSM = 1,
    LAYOUT_TYPE_STATIC_MAP = 2, // .jamo 1:1 텍스트 매핑 자판
    // 3 = 옛 DLL 플러그인 자판 — 없앴다(RFC-0006 D2, 2026-09-30: 코드 실행 없는 데이터). 번호는 다시 쓰지 않는다.
    LAYOUT_TYPE_HANGUL_CUSTOM = 4, // .jmt 설정파일 기반 사용자 한글 자판(세벌식 계열·결합규칙)
    LAYOUT_TYPE_CHORD = 5,      // .cord 설정파일 기반 일반 코드 자판(ARTSEY류 조합→출력)
    LAYOUT_TYPE_SEQUENCE = 6    // .jmt 3판 `Type = input`+`Engine = sequence` 순차 변환(로마자→가나류)
} LayoutType;

// 레이아웃 메타데이터
typedef struct {
    LayoutType type;
    int kbdVariant;      // KOREAN_FSM일 때 자판 종류 (KBD_DUBEOL/KBD_SEBEOL, layout.h)
    const wchar_t *name; // C 변수 스타일 식별자. e.g. "en_qwerty", "ko_2bul", "ko_3bul", "en_dvorak"
    wchar_t abbrev[8];   // 언어창/트레이 아이콘용 식별자. 자판마다 다른 아이콘 표지(모양·색·글자)를 고르는 열쇠 —
                         // 앞 두 글자 = 언어(아이콘 오른쪽 아래), 자판마다 표지는 layout_icon_style.c. e.g. "ENQW","KO2B". (.jmt 필수)
    
    // LAYOUT_TYPE_STATIC_MAP용 매핑 테이블 (QWERTY 기준 ASCII -> 변환 문자)
    wchar_t charMap[256];
    
    // LAYOUT_TYPE_HANGUL_CUSTOM용 로드된 자판 (HangulLayout*). live config가 소유.
    void* pHangulLayout;
    // LAYOUT_TYPE_CHORD용 로드된 코드 자판 (ChordLayout*). live config가 소유.
    void* pChordLayout;
    // LAYOUT_TYPE_SEQUENCE용 로드된 순차 변환표 (SeqLayout*). live config가 소유.
    void* pSeqLayout;


    bool enabled;   // 전환 순환에 포함되는가 (설정 체크박스). 기본 켜짐: en_qwerty, ko_2bul 만.
    // 자판별 선택 (설정의 Layout Options 탭, config.ini [LayoutOptions] "<자판 이름>.<키>=0|1", 2026-10-02).
    //   0 이 기본(켜짐)이라 0 으로 채운 구조체가 예전 동작이다. 지금은 순차 입력 자판(병음·가나)만 쓴다.
    bool optNoSentence;   // Sentence=0: 문장 후보를 내지 않는다
    bool optNoSuggest;    // Suggest=0: 추천 단어(이어질 낱말·줄임)를 내지 않는다
    bool optNoPunct;      // Punctuation=0: 중국어 자판의 문장부호를 바꾸지 않는다 (，。 대신 , .)
    bool optFuzzy;        // Fuzzy=1: 모호음 (z/zh c/ch s/sh n/l an/ang en/eng in/ing)
    bool optNoEmoji;      // Emoji=0: 이모지·기호 후보를 빼고
    bool optNoTones;      // Tones=0: 후보 옆의 성조 병음을 보이지 않는다
    bool optBar;          // Bar=1: 가로 후보줄 (중국어, RFC-0020 P2 — 기본 세로)
    wchar_t optKeys[16];  // Keys=xiaohe|ziranma|microsoft: 쌍병 글쇠 표 (빈 것 = 온 병음)
    // 본문을 나중에 읽는 자판 (RFC-0020 F1): 입력기 DLL 은 한글·조합·정적 자판의 구운 파일에서 머리(종류·이름·약자)만 읽고,
    //   그 자판이 지금 자판이 될 때 본문을 읽는다(Config_GetCurrentLayout). 꺼 둔 자판은 앱마다 본문을 지지 않는다.
    wchar_t *deferredPath;  // 구운 파일 (name 처럼 live 가 소유)
    bool bodyDeferred;      // 아직 본문을 읽지 않았다
} LayoutConfig;

// IME 동작 옵션 (설정창 'IME Options' 탭). 단축키류는 JamotongConfig.shortcuts 로 통합.
typedef struct {
    bool fullWidth;         // 전각 입력 (영문/기호를 전각 폭 U+FF01~ 으로)
    bool jamoDelete;        // 백스페이스 = 자소 단위 삭제 (끄면 조합 음절 전체 삭제). 기본 켜짐.
    bool showPreview;       // 조합 미리보기 플로팅 오버레이 (RFC-0002). 기본 켜짐.
    bool inlineComposition; // 비단명 컨텍스트 문서 인라인 조합 (RFC-0010). 기본 켜짐. 0=항상 commit 전용.
    bool useCompartments;   // TSF compartment 로 한/영·변환모드 발행/구독 (RFC-0012 Phase 1). 기본 켜짐. 킬스위치.
    bool usePreservedKeys;  // 문맥 무관 명령키를 TSF preserved key 로 예약 (RFC-0013 C). 기본 켜짐. 킬스위치.
    bool useUIElements;     // 자체 UI 를 UIElementMgr 게이트로 (RFC-0012 Phase 3). 기본 켜짐. 킬스위치.
    bool useUiHelper;       // UWP 호스트에서 데스크톱 UI 헬퍼에 후보창을 그리게 한다(RFC-0015). 기본 켜짐.
    bool uwpHanjaCycle;     // AppContainer(UWP) 호스트에서 후보창 대신 한자키 순환 변환. 기본 켜짐. 킬스위치.
    bool uwpOwnWindow;      // AppContainer(UWP) 호스트에서도 자체 후보창·코드 입력창을 먼저 쓴다(오너 결정 A9, 2026-09-30).
                            //   끄면 예전처럼 헬퍼 → 순환. 창이 안 보이는 호스트를 만난 사용자를 위한 탈출구.
    wchar_t previewFont[32];// 미리보기 글꼴 face 이름 (32 = LF_FACESIZE). 기본 "Malgun Gothic".
    int previewFontSize;    // 미리보기 글꼴 크기(px). 0=Auto(캐럿 높이 근사), 8~96=고정.
    wchar_t candFont[32];   // 한자 후보창 글꼴 face. 후보·훈음·페이지 표시 전부 이 글꼴 하나.
    int candFontSize;       // 한자 후보창 글꼴 크기(px, 12~72). 기본 24.
} ImeOptions;

// 글로벌 환경 설정 매니저
typedef struct {
    ShortcutList shortcuts[SC_FN_COUNT];   // 기능별 단축키 목록 (ShortcutFn 인덱스)

    // 자판 목록 — 개수 제한 없이 늘어난다(RFC-0006 D3, 오너 결정 2026-09-30 "동적으로").
    //   배열 자체는 이 구조체가 소유하고, 각 자판의 자원(name·HangulLayout 등)은 live 가 소유한다.
    //   구조체를 통째로 대입하지 말 것 — 배열이 공유된다. 복사는 Config_CopyShallow 로 한다.
    LayoutConfig *layouts;
    int layoutCount;
    int layoutCap;
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
// 본문을 나중에 읽는 자판의 본문을 읽는 함수 (플러그인 로더가 건다). 실패하면 그 자판은 글쇠를 응용에 넘긴다(통과).
void Config_SetBodyLoader(bool (*loader)(LayoutConfig *L));
bool Config_EnsureLayoutBody(LayoutConfig *L);
LayoutConfig* Config_GetCurrentLayout(JamotongConfig *config);

// 설정 내보내기 및 가져오기 (텍스트 형식)
// bundleLayouts=true 면 사용자 자판 저장소(%APPDATA%\Jamotong\layouts)의 모든 .jmt 본문을
// 파일 끝에 [LayoutFile:name] 섹션으로 인라인한다(Export 이식성 — 단일 파일로 자판까지 이동).
// Apply(자동 저장)는 false. Load는 그 섹션을 만나면 layouts 폴더에 복원한다(다음 시작 시 로드).
bool Config_SaveToFile(JamotongConfig *config, const wchar_t *filepath, bool bundleLayouts);
bool Config_LoadFromFile(JamotongConfig *config, const wchar_t *filepath);

// ── 설정창 파일 작업은 Apply 때만 (RFC-0008 W1-06 남은 절반, BACKLOGS B8) ──
// Import 의 번들 자판은 restoreDir(스테이징)에 복원하고 이름을 기록한다. Apply = Commit(저장소로 이동,
// 덮어쓰지 않음), Cancel = Discard(스테이징만 지움). restoreDir=NULL 이면 예전처럼 사용자 저장소에 바로.
#define CONFIG_STAGED_MAX 64   // 설정 가져오기 한 번에 되살리는 자판 파일 수(적대적 파일 방어)
typedef struct ConfigStagedLayouts {
    int count;
    wchar_t names[CONFIG_STAGED_MAX][128];
} ConfigStagedLayouts;
bool Config_LoadFromFileEx(JamotongConfig *config, const wchar_t *filepath,
                           const wchar_t *restoreDir, ConfigStagedLayouts *restored);
int  Config_CommitStagedLayouts(const ConfigStagedLayouts *st, const wchar_t *stagingDir, const wchar_t *storeDir);
void Config_DiscardStagedLayouts(const ConfigStagedLayouts *st, const wchar_t *stagingDir);
bool Config_StagingLayoutDir(wchar_t *out, int cch);   // %APPDATA%\Jamotong\layouts.staging
// 원자적 파일 복사(같은 폴더 임시 파일 → 교체). 실패하면 대상은 그대로.
bool Config_CopyFileAtomic(const wchar_t *src, const wchar_t *dst);
bool Config_UserPath(wchar_t *out, int cch);   // %APPDATA%\Jamotong\config.ini (자동 저장/로드용)
// 사용자 자판 저장소 %APPDATA%\Jamotong\layouts — 설정창 Add가 여기로 복사하고, 시작 시
// 자동 로드된다(외부 경로 .jmt가 재시작 후 사라지던 문제의 영속화 경로, RFC-0004 P0-2).
bool Config_UserLayoutDir(wchar_t *out, int cch);

// 주어진 폴더(와 그 아래)를 UWP(AppContainer) 프로세스가 **읽을** 수 있게 한다.
// TIP 은 호스트 프로세스 안에서 돈다 — 호스트가 UWP 앱이면 AppContainer 라서 이 권한이 없는
// 폴더의 파일을 열지 못한다(설정도, TIP DLL 자신도). 설정 폴더 생성 시와 DLL 등록 시 부른다.
void Config_GrantAppContainerRead(const wchar_t *path);
// 사용자 사전 저장소 %APPDATA%\Jamotong\dicts — 구운 사전(.jdb)이 사는 곳 (RFC-0016 P5).
//   자판 파일은 이름만 적고, 찾는 순서는 자판 파일 옆 → 여기 → 기계 전체 → DLL 옆이다.
bool Config_UserDictDir(wchar_t *out, int cch);
// 기계 전체 사전 저장소 %PROGRAMDATA%\Jamotong\dicts (관리자 배포용).
bool Config_MachineDictDir(wchar_t *out, int cch);
// 기계 전체 자판 저장소 %PROGRAMDATA%\Jamotong\layouts (관리자 배포용, RFC-0011 P0).
// 읽기 전용 취급 — 생성 시도는 하되 실패(비관리자)해도 조용히 넘어간다.
bool Config_MachineLayoutDir(wchar_t *out, int cch);
// Import 번들 복원 시 [LayoutFile:name] 이름이 안전한지 검사한다(경로 조작 방어).
//   경로 구분자 없음 + .jmt 확장자 + 예약 장치명 아님 + basename이 점/공백만은 아님일 때 true.
bool Config_IsSafeLayoutFileName(const wchar_t *name);
// 사전 파일 이름이 안전한지 (같은 규칙 + `.jdb` 확장자). 자판 파일이 가리키는 이름을 이걸로 거른다.
bool Config_IsSafeDictFileName(const wchar_t *name);
// 번들 헤더에 파일명을 안전하게 싣기 위한 최소 percent-encoding(']' 와 '%'만).
void Config_EncodeLayoutName(const wchar_t *in, wchar_t *out, size_t cch);
void Config_DecodeLayoutName(wchar_t *s);   // 제자리 역변환

// 리소스 소유권 (플러그인 DLL/컨텍스트, heap name). live config만 소유 — 자세히는 config.c 참조.
void Config_Free(JamotongConfig *cfg);                                       // live 파괴 시 전체 해제
void Config_ApplyEdited(JamotongConfig *live, const JamotongConfig *edited); // 설정 적용(떨어낸 것 해제 후 채택)
void Config_DiscardEdited(JamotongConfig *edited, const JamotongConfig *live);// 설정 취소(비-live 리소스만 해제)
// 자판 목록 배열 (D3): 덧붙이기, 배열까지 복제하는 복사(자판 자원은 공유 — 소유 원칙은 그대로), 배열만 해제.
bool Config_AppendLayout(JamotongConfig *config, const LayoutConfig *layout);
bool Config_CopyShallow(JamotongConfig *dst, const JamotongConfig *src);
void Config_ReleaseLayoutArray(JamotongConfig *config);
// 설정 파일이 적어 둔 자판 개수의 안전 상한 — 제품의 한도가 아니라 조작된 파일이 메모리를 잡아먹지 못하게 하는 선.
#define CONFIG_FILE_LAYOUTS_MAX 256
void Config_FreeLayoutResources(LayoutConfig *L);                            // 단일 자판 리소스 해제(중복 로드본 등)
// 편집본에서 idx 자판 제거. live가 소유하지 않은(Add 직후 등) 리소스는 제거 전에 해제한다 —
// shift로 배열에서 사라지면 Discard/Apply가 볼 수 없어 누수됐다(RFC-0004 P0-3).
void Config_RemoveEditedLayout(JamotongConfig *edited, int idx, const JamotongConfig *live);
