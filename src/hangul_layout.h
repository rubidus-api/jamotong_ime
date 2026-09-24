#pragma once
#include <windows.h>
#include <stdbool.h>
#include "layout.h"   // LayoutResult, JamoType
#include "klay.h"     // KlayDiag (로드 진단)

// 설정파일(.jmt)로 정의하는 사용자 한글 자판. 세벌식 계열(초/중/종성 분리, 직접 종성)과
// 그 결합 규칙(거센소리/된소리/겹모음/겹받침)을 데이터로 표현한다. moachigi=1 이면 동시치기
// (chord) 런타임(chord.c)이 켜져, 여러 글쇠를 함께 눌러 한 음절을 만들고 모두 떼면 확정한다.
// moachigi=0 이면 순차(이어치기) FSM으로 처리한다. 이 파일에는 특정 자판의 배열 데이터를 담지 않는다.

#define HL_MAX_COMBINE 256

typedef struct {
    JamoType type;      // JAMO_CHO / JAMO_JUNG / JAMO_JONG
    int a, b;           // 입력된 두 자모 인덱스
    int result;         // 결합 결과 인덱스
} HangulCombine;

// 조합 모델 (RFC-0016 P2) — Composition = sebeol | dubeol
#define HL_SEBEOL 0   // 직접 종성: 종성 키가 따로 있다 (기본값, 예전 동작)
#define HL_DUBEOL 1   // 두벌식: 초성 키가 문맥으로 받침이 되고, 다음 모음이 오면 받침이 다음 음절로 넘어간다.
                      //   초성→받침 대응과 받침 분리는 표준 현대 한글 규칙(엔진 계약). 겹받침은 파일의
                      //   'Combine T <받침> <초성을 받침으로 바꾼 번호> = <겹받침>' 표.

// ── 가드 붙은 글쇠 (RFC-0018 §3.6) ─────────────────────────────────────────────
// 한 글쇠가 조합 상태에 따라 다른 낱자를 낸다(순아래·갈마들이). 조건은 로드할 때 **작은 후위
// 프로그램**으로 컴파일해 둔다 — 글쇠를 칠 때는 그것을 훑기만 하므로 문자열 해석이 없다.
//   셈씨: 01 <id> 있나 · 02 <id> 번호 · 03 <n> 상수 · 10 not · 11 and · 12 or · 20~25 eq ne lt le gt ge
//   id: 0=초성 1=중성 2=종성 3=빈 조합
#define HL_MAX_GUARDED   128
#define HL_GUARD_CODE    24
#define HLG_HAS 0x01
#define HLG_IDX 0x02
#define HLG_NUM 0x03
#define HLG_NOT 0x10
#define HLG_AND 0x11
#define HLG_OR  0x12
#define HLG_EQ  0x20
typedef struct HangulGuarded {
    unsigned char key;                  // ASCII 글쇠
    unsigned char len;                  // 프로그램 길이
    unsigned char code[HL_GUARD_CODE];
    LayoutResult  r;                    // 조건이 참일 때 낼 낱자
} HangulGuarded;

typedef struct HangulLayout {
    wchar_t name[64];
    int moachigi;                    // 1=모아치기(순서무관 결합 선언), 0=이어치기(순차)
    int composition;                 // HL_SEBEOL / HL_DUBEOL
    LayoutResult keymap[128];        // ASCII 산출문자 → {type, index}
    HangulCombine combines[HL_MAX_COMBINE];
    int combineCount;
    HangulGuarded guarded[HL_MAX_GUARDED];   // 적은 차례대로 본다 — 처음 맞는 줄이 이긴다
    int guardedCount;
} HangulLayout;

// .jmt 파일에서 로드 (heap 할당, 실패 시 NULL). 소유자가 HangulLayout_Free 로 해제.
HangulLayout *HangulLayout_LoadFromFile(const wchar_t *path, KlayDiag *diag);
HangulLayout *HangulLayout_LoadFromLines(const KlayLines *L, KlayDiag *diag);   // Extends/Include 푼 줄 (RFC-0011 P4)
void HangulLayout_Free(HangulLayout *hl);

// 조합 상태를 보고 이 글쇠가 낼 낱자를 고른다. 없으면 {JAMO_NONE,0}.
//   cho/jung 은 없으면 -1, jong 은 없으면 0 (엔진의 표기 그대로).
LayoutResult HangulLayout_Key(const HangulLayout *hl, wchar_t key, int cho, int jung, int jong);

// (type, a, b) 결합 조회 — 대응 규칙의 result, 없으면 -1. 모아치기 자판은 순서무관으로 (b,a)도 시도.
int HangulLayout_Combine(const HangulLayout *hl, JamoType type, int a, int b);
