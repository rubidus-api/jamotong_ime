#pragma once
#include "klay.h"
#include "jdict.h"

// ── 순차 변환 자판 (.jmt 3판, RFC-0016 §6.3·§6.4 P5) ───────────────────────────────
// 라틴 글쇠의 **열**을 다른 글자로 바꾸는 공통 경로다 (로마자→가나처럼).
// 오너 결정 2026-09-23: **자판 파일과 사전 파일은 따로다.** 변환표는 자판 파일 안에 못 쓰고
// (`Sequence` 줄은 오류), 컴파일해 둔 사전 파일(.jdb)을 이름으로 가리킨다. 자판을 고를 때
// 자판 문법과 사전 데이터를 모두 점검해, 성한 것만 쓴다.
//
//   FormatVersion = 3
//   Type = input
//   Engine = sequence
//   RequiresJamotong = 0.38.0
//   Dictionary = romaji-kana.jdb     # 자판 파일 옆 → 사용자 사전 폴더 → 기계 전체 → DLL 옆
//   OnUnmatched = flush              # 또는 cancel (기본 flush)
//
// 규칙 (RFC-0016 §6.3):
//   - 접두가 겹치면 **최장 일치**를 기다린다. `n` 과 `na` 가 다 있으면 `n` 하나로는 확정하지 않는다.
//   - 더 갈 곳이 없으면 정책대로: flush = 보류한 리터럴을 확정하고 지금 글자를 처음부터 한 번
//     재처리, cancel = 오류 없이 보류를 버린다. 재처리는 한 번뿐 — 도는 일이 없다.
//   - Backspace 는 미확정 입력 토큰 한 개를 되돌린다. 이미 확정한 남의 글자는 건드리지 않는다.
//   - Esc 는 preedit 만 취소한다.
#define SEQ_MAX_IN      JDICT_MAX_KEY     // 보류할 수 있는 입력 길이 (사전 키 한도와 같다)
#define SEQ_MAX_OUT     JDICT_MAX_VALUE

enum { SEQ_UNMATCHED_FLUSH = 0, SEQ_UNMATCHED_CANCEL = 1 };

typedef struct SeqLayout {
    wchar_t  name[64];
    wchar_t  dictFile[64];       // 자판 파일이 적은 이름 (검사 통과한 것)
    wchar_t  dictPath[260];      // 실제로 연 경로
    int      onUnmatched;        // SEQ_UNMATCHED_*
    int      maxIn;              // 사전에 있는 가장 긴 키
    JDict   *dict;               // 매핑한 사전 (이 자판이 소유한다)
} SeqLayout;

// 보류 중인 입력. 확정한 글자는 문서가 갖고 있으므로 여기 남기지 않는다.
typedef struct SeqState {
    wchar_t pending[SEQ_MAX_IN + 1];
} SeqState;

// 한 번의 입력이 낳은 결과. committed = 지금 문서에 넣을 글자들, composing = 아직 보류(미리보기).
typedef struct SeqResult {
    wchar_t committed[192];      // 한 번의 재처리가 낳을 수 있는 최대보다 넉넉하다
    wchar_t composing[SEQ_MAX_IN + 1];
    bool    eaten;               // 엔진이 이 글쇠를 먹었는가 (false 면 응용으로 그냥 보낸다)
} SeqResult;

// 자판 줄에서 읽는다. layoutPath 는 자판 파일의 전체 경로(사전을 그 옆에서 먼저 찾는다, NULL 허용).
SeqLayout *SeqLayout_LoadFromLines(const KlayLines *L, const wchar_t *layoutPath, KlayDiag *diag);
SeqLayout *SeqLayout_LoadFromFile(const wchar_t *path, KlayDiag *diag);
void       SeqLayout_Free(SeqLayout *sl);
// 자판을 고를 때의 전수 점검 (사전 데이터까지). 통과 못 하면 이 자판을 쓰지 않는다.
bool       SeqLayout_Verify(const SeqLayout *sl, KlayDiag *diag);
// sl->dictFile 이 가리키는 사전을 찾아 열고 전수 점검한다 (자판 파일이든 구운 자판이든 같은 길).
//   layoutPath = 자판 파일/구운 자판의 전체 경로(그 옆을 먼저 본다, NULL 허용). diag 는 NULL 허용.
bool       SeqLayout_OpenDict(SeqLayout *sl, const wchar_t *layoutPath, KlayDiag *diag);

void      SeqKb_Init(SeqState *st);
// 이 글쇠를 엔진이 먹을 것인가 (TSF 의 OnTestKeyDown 용 — 실제로 상태를 바꾸지 않는다).
bool      SeqKb_WouldEat(const SeqState *st, const SeqLayout *sl, wchar_t ch);
// 글자 하나. 표의 입력 알파벳 밖이면 eaten=false 이며, 보류가 있었으면 그 리터럴을 확정해서 돌려준다
// (그 글쇠 자체는 응용이 처리한다 — 사이띄개·숫자·엔터가 보류를 잃지 않게).
SeqResult SeqKb_Key(SeqState *st, const SeqLayout *sl, wchar_t ch);
// 보류 입력 한 글자를 되돌린다. 보류가 없으면 eaten=false (응용의 백스페이스).
SeqResult SeqKb_Backspace(SeqState *st, const SeqLayout *sl);
// preedit 만 취소. 보류가 없으면 eaten=false.
SeqResult SeqKb_Cancel(SeqState *st);
// 경계(자판 전환·포커스 상실·비활성화): 보류한 리터럴을 확정한다.
SeqResult SeqKb_Flush(SeqState *st, const SeqLayout *sl);
