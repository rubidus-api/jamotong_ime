#pragma once
#include "klay.h"

// ── 순차 변환 자판 (.jmt 3판, RFC-0016 §6.3·§6.4 P5) ───────────────────────────────
// 라틴 글쇠의 **열**을 다른 글자로 바꾸는 공통 경로다 (로마자→가나처럼). 사전은 쓰지 않는다 —
// 파일이 준 표만 본다. 한글 오토마타(hangul)·조합 자판(chord)과 나란한 세 번째 엔진이다.
//
//   FormatVersion = 3
//   Type = input
//   Engine = sequence
//   RequiresJamotong = 0.37.0
//   OnUnmatched = flush        # 또는 cancel (기본 flush)
//   Sequence "ka" = emit "\u{304B}"
//
// 규칙 (RFC-0016 §6.3):
//   - 접두가 겹치면 **최장 일치**를 기다린다. `n` 과 `na` 가 다 있으면 `n` 하나로는 확정하지 않는다.
//   - 더 갈 곳이 없으면 정책대로: flush = 보류한 리터럴을 확정하고 지금 글자를 처음부터 한 번
//     재처리, cancel = 오류 없이 보류를 버린다. 재처리는 한 번뿐 — 도는 일이 없다.
//   - Backspace 는 미확정 입력 토큰 한 개를 되돌린다. 이미 확정한 남의 글자는 건드리지 않는다.
//   - Esc 는 preedit 만 취소한다.
#define SEQ_MAX_ENTRIES 512
#define SEQ_MAX_IN      8        // 입력 쪽 글자 수 한도 (ASCII)
#define SEQ_MAX_OUT     16       // 출력 쪽 글자 수 한도 (UTF-16)

enum { SEQ_UNMATCHED_FLUSH = 0, SEQ_UNMATCHED_CANCEL = 1 };

typedef struct SeqEntry {
    wchar_t in[SEQ_MAX_IN + 1];
    wchar_t out[SEQ_MAX_OUT + 1];
} SeqEntry;

typedef struct SeqLayout {
    wchar_t  name[64];
    int      count;
    int      onUnmatched;        // SEQ_UNMATCHED_*
    int      maxIn;              // 표에 있는 가장 긴 입력 길이
    SeqEntry v[SEQ_MAX_ENTRIES];
} SeqLayout;

// 보류 중인 입력. 확정한 글자는 문서가 갖고 있으므로 여기 남기지 않는다.
typedef struct SeqState {
    wchar_t pending[SEQ_MAX_IN + 1];
} SeqState;

// 한 번의 입력이 낳은 결과. committed = 지금 문서에 넣을 글자들, composing = 아직 보류(미리보기).
typedef struct SeqResult {
    wchar_t committed[192];   // 한 번의 재처리가 낳을 수 있는 최대(9토큰x16)보다 넉넉하다
    wchar_t composing[SEQ_MAX_IN + 1];
    bool    eaten;               // 엔진이 이 글쇠를 먹었는가 (false 면 응용으로 그냥 보낸다)
} SeqResult;

SeqLayout *SeqLayout_LoadFromLines(const KlayLines *L, KlayDiag *diag);
SeqLayout *SeqLayout_LoadFromFile(const wchar_t *path, KlayDiag *diag);
void       SeqLayout_Free(SeqLayout *sl);

void      SeqKb_Init(SeqState *st);
// 글자 하나. 표의 입력 알파벳 밖이면 eaten=false 이며, 보류가 있었으면 그 리터럴을 확정해서 돌려준다
// (그 글쇠 자체는 응용이 처리한다 — 사이띄개·숫자·엔터가 보류를 잃지 않게).
SeqResult SeqKb_Key(SeqState *st, const SeqLayout *sl, wchar_t ch);
// 이 글쇠를 엔진이 먹을 것인가 (TSF 의 OnTestKeyDown 용 — 실제로 상태를 바꾸지 않는다).
bool      SeqKb_WouldEat(const SeqState *st, const SeqLayout *sl, wchar_t ch);
// 보류 입력 한 글자를 되돌린다. 보류가 없으면 eaten=false (응용의 백스페이스).
SeqResult SeqKb_Backspace(SeqState *st, const SeqLayout *sl);
// preedit 만 취소. 보류가 없으면 eaten=false.
SeqResult SeqKb_Cancel(SeqState *st);
// 경계(자판 전환·포커스 상실·비활성화): 보류한 리터럴을 확정한다.
SeqResult SeqKb_Flush(SeqState *st, const SeqLayout *sl);
