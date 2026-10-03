#pragma once
#include "klay.h"
#include "jdict.h"
#include <windows.h>   // SYSTEMTIME (날짜·시간 후보)

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
//   Dictionary = romaji-kana.jdb     # 자판 파일 옆 → 사용자 사전 폴더 → 기계 전체
//   OnUnmatched = flush              # 또는 cancel (기본 flush)
//   Key jkl; = 0                     # (선택) 앞단 조합 인식기 — §6.3
//   Chord jk = symbol "k"            #   조합이 먼저 결정하고, 그 symbol 만 엔진으로 간다
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

// 중국어 병음 방식 (자판 파일 `Chinese = simplified | traditional`, 2026-10-03). 켜면:
//   - 치는 동안 후보가 뜨고, 사이띄개는 첫(하이라이트) 후보를 고르고, `-` `=` 는 쪽을 넘기고, 엔터는 친 로마자를 그대로,
//     Esc 는 읽기를 지운다 (입력기·후보창의 몫 — 엔진은 아래 도구를 준다).
//   - `'` 는 음절 끊기다 (`xi'an` → 西安): 낱말이 끊기를 넘으면 그만큼 글자가 있어야 한다.
//   - 모음 없는 낱말(첫 글자 줄임 `zg` → 中国)은 읽기 전체에 모음이 없을 때만 쓴다 (`wangguo` 의 `ng` 가 끼지 않게).
//   - `rq` `sj` `xq` 는 오늘 날짜·지금 시각·요일, `[` `]` 는 후보의 첫·끝 글자만 (以词定字), 문장부호는 중국어 꼴로.
enum { SEQ_ZH_NONE = 0, SEQ_ZH_SIMPLIFIED = 1, SEQ_ZH_TRADITIONAL = 2 };
#define SEQ_SEPARATOR L'\''

// 읽기·후보 (RFC-0016 §6.4). 후보 사전이 없으면 이 자리는 비어 있고 동작도 예전 그대로다.
#define SEQ_MAX_READING 64     // 우리 소유 preedit 에 쌓을 수 있는 글자 수
#define SEQ_MAX_CANDS   72     // 한 번에 내놓는 후보 수 (후보창 여덟 쪽). 병음 한 음절은 후보가 수백이다
#define SEQ_PREFIX_CANDS 27    // 읽기 전체가 아닌 앞부분 하나에서 가져오는 후보 수 (세 쪽)

typedef struct SeqCandidates {
    unsigned generation;                        // 이 묶음이 어느 읽기의 것인가 (늦은 결과를 버린다)
    int      count;
    wchar_t  items[SEQ_MAX_CANDS][SEQ_MAX_OUT + 1];
    int      consumed[SEQ_MAX_CANDS];           // 그 후보가 쓰는 읽기 앞부분의 글자 수 (나머지는 읽기에 남는다)
} SeqCandidates;

typedef struct SeqLayout {
    wchar_t  name[64];
    // 앞단 조합 인식기 (RFC-0016 §6.3, 선택). 있으면 조합이 먼저 결정하고 그 `symbol` 만 엔진으로
    // 들어간다. 없으면 생키가 바로 엔진으로 가는 빠른 경로다. 이 자판이 소유한다(ChordLayout*).
    void    *chord;
    wchar_t  dictFile[64];       // 자판 파일이 적은 이름 (검사 통과한 것)
    wchar_t  dictPath[260];      // 실제로 연 경로
    int      onUnmatched;        // SEQ_UNMATCHED_*
    int      maxIn;              // 사전에 있는 가장 긴 키
    JDict   *dict;               // 매핑한 사전 (이 자판이 소유한다)
    // 후보 사전 (선택, §6.4). 있으면 사전이 낸 글자를 바로 확정하지 않고 읽기 버퍼에 쌓았다가
    // 변환 글쇠에서 후보를 내놓는다. 없으면 예전과 똑같이 바로 확정한다.
    wchar_t  candFile[64];
    JDict   *cand;
    int      convertVk;          // 변환 글쇠 (VK_*), 0 = 없음
    int      zh;                 // SEQ_ZH_* (중국어 병음 방식, 구운 자판 판 7)
} SeqLayout;

// 보류 중인 입력. 확정한 글자는 문서가 갖고 있으므로 여기 남기지 않는다.
typedef struct SeqState {
    wchar_t  pending[SEQ_MAX_IN + 1];            // 아직 사전을 만나지 못한 친 글자들
    wchar_t  reading[SEQ_MAX_READING + 1];       // 우리 소유 preedit (후보 사전이 있을 때만 찬다)
    unsigned generation;                         // 읽기가 바뀔 때마다 오른다 (후보 snapshot 의 주인)
    bool     candOpen;                           // 후보를 내놓은 상태인가
    wchar_t  lastCommit;                         // 마지막으로 확정한 글자 (숫자 뒤의 . , 는 그대로 둔다)
    bool     dquoteOpen, squoteOpen;             // 중국어 따옴표 짝 (“ ” / ‘ ’ — 번체는 「 」 / 『 』)
} SeqState;

// 한 번의 입력이 낳은 결과. committed = 지금 문서에 넣을 글자들, composing = 아직 보류(미리보기).
typedef struct SeqResult {
    wchar_t committed[192];      // 한 번의 재처리가 낳을 수 있는 최대보다 넉넉하다
    wchar_t composing[SEQ_MAX_READING + 1];   // 보류든 읽기든 다 담는다 (읽기가 더 길다)
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
// 앞단 조합이 낸 `symbol` 을 엔진에 넣는다 (RFC-0016 §6.3). 한 글자씩 넣은 결과를 합쳐 돌려준다.
//   엔진이 받지 않는 글자(그 사전으로 시작할 수 없는 글자)는 **친 그대로 확정한다** — symbol 은
//   글쇠가 아니라 논리 입력이라, 엔진이 안 받는다고 사라지면 안 된다.
SeqResult SeqKb_Symbol(SeqState *st, const SeqLayout *sl, const wchar_t *sym);
// 지금 읽기 (우리 소유 preedit). 후보 사전이 없으면 언제나 빈 문자열.
const wchar_t *SeqKb_Reading(const SeqState *st);
// 변환 글쇠를 지금 받을 수 있는가 (보류만 있어도 받는다 — 보류를 먼저 읽기로 정착시킨다).
bool      SeqKb_CanConvert(const SeqState *st, const SeqLayout *sl);
// 변환 글쇠: 보류한 글자를 읽기로 정착시킨 뒤 그 읽기의 후보를 내놓는다 — 읽기 전체의 후보가 먼저,
// 그다음 앞부분의 후보를 긴 것부터(이어 치기). 읽기가 비었거나 어느 앞부분에도 후보가 없으면 false.
bool      SeqKb_Convert(SeqState *st, const SeqLayout *sl, SeqCandidates *out);
// 자판별 선택 (설정의 Layout Options, 2026-10-02). SeqKb_Convert 는 둘 다 켠 것과 같다.
#define SEQ_CONV_SENTENCE 1u   // 낱말 둘 이상으로 가른 문장을 첫 후보로
#define SEQ_CONV_PREDICT  2u   // 추천 단어: 읽기로 시작하는 더 긴 낱말·성어 (고르면 읽기 전체를 그것으로)
#define SEQ_CONV_ALL      (SEQ_CONV_SENTENCE | SEQ_CONV_PREDICT)
#define SEQ_PREDICT_CANDS 9    // 추천 단어 수
#define SEQ_WHOLE_FIRST   5    // 첫 쪽: 읽기 그대로의 후보 이만큼 다음에
#define SEQ_PREDICT_FIRST 4    //        추천 단어 이만큼 (문장 후보와 합쳐 한 쪽 아홉)
bool      SeqKb_ConvertEx(SeqState *st, const SeqLayout *sl, unsigned flags, SeqCandidates *out);
// 후보 하나를 고른다. snapshot 의 세대가 지금과 다르면(늦게 온 결과) 버린다. 앞부분만 쓰는 후보면
// 나머지 읽기가 남고 composing 에 실린다 — SeqKb_Reading 이 비어 있지 않으면 이어서 다시 변환한다.
SeqResult SeqKb_Choose(SeqState *st, const SeqLayout *sl, const SeqCandidates *cands, int index);
// 후보를 접는다 — 읽기는 그대로 남는다.
SeqResult SeqKb_CancelCandidates(SeqState *st);
// 경계(자판 전환·포커스 상실·비활성화): 보류한 리터럴을 확정한다.
SeqResult SeqKb_Flush(SeqState *st, const SeqLayout *sl);

// ── 중국어 병음 방식의 도구 (sl->zh != SEQ_ZH_NONE) ─────────────────────────────────
// 확정한 글자를 엔진에 알린다 (숫자 뒤 문장부호 규칙). SeqApply 가 문서에 넣을 때마다 부른다.
void      SeqKb_NoteCommitted(SeqState *st, const wchar_t *text);
// 친 문장부호를 중국어 꼴로 (，。？！、；：“”‘’（）【】《》……——￥·～). 바꿀 것이 아니면 false.
//   숫자 바로 뒤의 `.` `,` `:` 는 그대로(3.14, 1,000, 12:30). 따옴표는 여닫이가 번갈아 나온다.
bool      SeqKb_Punct(SeqState *st, const SeqLayout *sl, wchar_t ch, wchar_t *out, int cap);
// 이 글자가 바꿀 문장부호인가 (상태를 바꾸지 않는다 — OnTestKeyDown 용).
bool      SeqKb_IsPunct(const SeqLayout *sl, wchar_t ch);
// 지금 읽기를 가장 그럴듯한 변환으로 모두 확정한다 (문장부호·글자 아닌 글쇠가 읽기 뒤에 왔을 때).
SeqResult SeqKb_CommitBest(SeqState *st, const SeqLayout *sl, unsigned flags);
// 以词定字: 후보 index 의 첫(last=false)·끝(last=true) 글자만 확정하고, 그 후보가 쓰는 읽기는 지운다.
SeqResult SeqKb_ChoosePart(SeqState *st, const SeqLayout *sl, const SeqCandidates *cands, int index, bool last);
// 날짜·시간 후보를 out 에 붙인다 (읽기가 rq·sj·xq 일 때). 시험이 시각을 고정하도록 st 가 아닌 인자로 받는다.
int       SeqKb_DateTimeCands(const SeqLayout *sl, const wchar_t *reading, const SYSTEMTIME *now,
                              wchar_t items[][SEQ_MAX_OUT + 1], int cap);
