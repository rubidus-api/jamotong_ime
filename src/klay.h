#pragma once
#include "config.h"

// ── .jmt 로드 진단 (RFC-0011 P1) ────────────────────────────────────────────────────
// 오류 하나에서 멈추지 않고 최대 KLAY_DIAG_MAX 개까지 모은다. 경고는 로드를 막지 않고, 오류는 막는다.
// line/message 는 '첫 오류'(예전 호출부 호환). line 0 = 파일 열기 실패/미상.
#define KLAY_DIAG_MAX 16
typedef enum KlaySeverity { KLAY_SEV_ERROR = 0, KLAY_SEV_WARNING = 1 } KlaySeverity;
typedef struct KlayDiagItem {
    int          line;          // 1-based (0 = 파일 전체)
    int          col;           // 1-based (0 = 미상)
    KlaySeverity severity;
    wchar_t      code[24];      // 예: E-JMT-RANGE, W-JMT-UNKNOWN-KEY
    wchar_t      message[160];  // 영어 사유
    wchar_t      help[160];     // 고치는 법 (없으면 빈 문자열)
    wchar_t      file[64];      // 그 줄이 나온 파일 (Extends/Include 로 다른 파일일 수 있다, RFC-0011 P4)
} KlayDiagItem;
typedef struct KlayDiag {
    int     line;               // 첫 오류 줄 (호환)
    wchar_t message[160];       // 첫 오류 사유 (호환)
    int     formatVersion;      // 파서 문맥: 머리부의 FormatVersion (없으면 1)
    int     count, errors, warnings;
    const wchar_t *curFile;     // 파서가 지금 읽는 줄의 파일 이름 (Add 가 항목에 복사)
    wchar_t topFile[64];        // 최상위 파일 이름 — 형식화 때 이 파일의 항목은 호출자가 준 이름으로
    KlayDiagItem items[KLAY_DIAG_MAX];
} KlayDiag;

void KlayDiag_Init(KlayDiag *d);
void KlayDiag_Add(KlayDiag *d, KlaySeverity sev, int line, int col,
                  const wchar_t *code, const wchar_t *msg, const wchar_t *help);
// 사람이 읽는 형식: "<file>:<line>:<col>: error: msg\n   help: ...\n" 줄들.
void Klay_DiagFormat(const KlayDiag *d, const wchar_t *file, wchar_t *out, size_t cch);

// ── 머리부 메타데이터 (RFC-0011 P2) ─────────────────────────────────────────────────
typedef struct KlayMeta {
    int     formatVersion;      // 없으면 1
    wchar_t id[64], version[32], author[128], license[64];
    wchar_t homepage[160], description[160], locale[16], requires[16];
} KlayMeta;

// 머리부 키인가 (FormatVersion·Type·Id·Name·Abbrev·Version·Author·License·Homepage·Description·Locale·
// RequiresJamotong). 파서는 자기 지시문이 아닌 줄에서 이걸 먼저 묻고, 아니면 Klay_UnknownLine.
bool KlayHeader_IsKnownKey(const wchar_t *line);
// 모르는 줄: `Word = …` 꼴이면 미지 머리부 키 경고(철자 제안), 아니면 v2 에서 오류·v1 에서 경고.
//   known = 그 자판 종류의 지시문 이름 목록(NULL 끝) — 철자 제안에 쓴다. 반환 = 오류였는가.
bool Klay_UnknownLine(KlayDiag *d, const wchar_t *line, int lineno, int col, const wchar_t *const *known);
// ── 글쇠 머리 해석 (RFC-0011 P6) ────────────────────────────────────────────────────
// `<keys> [base|shift] =` 에서 글쇠 문자열을 US QWERTY 가 내는 문자들로 바꾼다.
//   keys: 문자 나열(예 `khj`) 또는 물리 글쇠 하나 — `@Q`(US 자리 이름), `@SC10`(스캔코드 16진),
//   `@VK_OEM_1`(가상키 이름). shift 면 각 문자를 US Shift 문자로(`q`→`Q`, `1`→`!`).
//   allowAltGr=false 면 altgr 는 오류(엔진이 AltGr 면을 읽지 않는다).
// 성공 시 out 에 문자들, *specPos 에 '=' 다음 위치. 실패 시 진단을 내고 false.
bool Klay_ParseKeyHead(const wchar_t *p, wchar_t *out, size_t cch, const wchar_t **specPos,
                       KlayDiag *d, int lineno, int col0);

// "a.b.c" 판 비교 (-1/0/1). 모자란 자리는 0.
int Klay_CompareVersion(const wchar_t *a, const wchar_t *b);

// ── 줄 원천 (RFC-0011 P4) ──────────────────────────────────────────────────────────
// 파일을 한 번 읽어 `Extends`(한 줄, 깊이 4, 순환 거부, 종류 일치)와 `Include` 를 풀어 합친 줄 목록.
// 기반 자판의 줄이 먼저, 자기 줄이 뒤 — 파서는 "뒤가 이긴다"로 덮어쓴다. `@이름` = 내장 자판.
#define KLAY_MAX_FILES 12
typedef struct KlayLine { wchar_t *text; int line; int file; } KlayLine;
typedef struct KlayLines {
    KlayLine *v; int n, cap;
    wchar_t files[KLAY_MAX_FILES][64];
    int nfiles;
} KlayLines;
bool KlayLines_Build(KlayLines *L, const wchar_t *path, KlayDiag *d);
void KlayLines_Free(KlayLines *L);

// 내장 자판을 완전한 .jmt 텍스트로 (ko_3bul·en_dvorak·en_qwerty). 옮길 수 없으면 false + why.
//   두벌식(ko_2bul)은 자음이 다음 음절로 넘어가는 규칙이 오토마타 안에 있어 hangul .jmt 로 못 옮긴다.
bool Klay_BuiltinText(const wchar_t *name, wchar_t *out, size_t cch, wchar_t *why, size_t whyCch);

// 통합 자판 설정파일(.jmt) 로더. 파일 첫머리의 `Type =` 값으로 종류를 정한다:
//   Type = static   1:1 문자 리맵 (드보락류)          → LAYOUT_TYPE_STATIC_MAP (charMap)
//   Type = hangul   세벌식 계열 한글 (초/중/종성·결합) → LAYOUT_TYPE_HANGUL_CUSTOM (기본값)
//   Type = chord    조합→동작 (ARTSEY류·레이어·마우스) → LAYOUT_TYPE_CHORD
// 성공 시 out(LayoutConfig)을 채우고 true. name/리소스는 live config가 소유(Config_Free가 해제).
// diag(NULL 허용): 진단을 모은다(경고 포함). meta(NULL 허용): 머리부 메타데이터.
bool Klay_Load(const wchar_t *path, LayoutConfig *out, KlayDiag *diag);
bool Klay_LoadEx(const wchar_t *path, LayoutConfig *out, KlayDiag *diag, KlayMeta *meta);
