#pragma once
#include "lowlay_parse.h"
#include "lowlay_expr.h"

// ── 자판 정의 언어 v4 — 머리·모양·이름 검사 (RFC-0018 P2) ───────────────────────
// 폼 트리(뜻을 모르는 것)를 받아 **이 언어가 아는 것인지** 잰다:
//   · 머리 이름이 닫힌 집합에 있는가 (`E-LOW-HEAD`) — 낱말은 예산이다
//   · 폼의 모양이 맞는가 (`E-LOW-SHAPE`), 갈래 이름이 맞는가 (`E-LOW-KIND`)
//   · 가드 식이 읽히고, 쓰인 상태·가드 이름이 모두 아는 것인가 (`E-LOW-EXPR*`)
//   · 같은 이름을 두 번 선언하지 않았는가 (`E-LOW-DUP`)
// 표를 실제로 만드는 일(글쇠→낱자, 규칙, 직렬화)은 P3 이다. 여기서는 **읽히는가**만 잰다.
#define LOW_MAX_SYMS 128

typedef struct LowSyms {
    wchar_t guard[LOW_MAX_SYMS][LOW_NAME_MAX]; int nGuard;
    wchar_t store[LOW_MAX_SYMS][LOW_NAME_MAX]; int nStore;
    wchar_t var  [LOW_MAX_SYMS][LOW_NAME_MAX]; int nVar;
    wchar_t layer[LOW_MAX_SYMS][LOW_NAME_MAX]; int nLayer;
    wchar_t behav[LOW_MAX_SYMS][LOW_NAME_MAX]; int nBehav;
    wchar_t macro[LOW_MAX_SYMS][LOW_NAME_MAX]; int nMacro;
    wchar_t group[LOW_MAX_SYMS][LOW_NAME_MAX]; int nGroup;
} LowSyms;

typedef struct LowCheckResult {
    wchar_t name[64];        // layout name
    int  format;             // layout format (없으면 0)
    wchar_t engine[16];      // hangul | rules | dict | none
    int  keys, maps, chords, holds, rules, layers, combines;
    LowSyms syms;
} LowCheckResult;

bool LowCheck_Run(const LowTree *tree, LowCheckResult *out, KlayDiag *diag);
