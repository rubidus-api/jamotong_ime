#pragma once
#include "lowlay_lex.h"

// ── 자판 정의 언어 v4 — 폼 트리 (RFC-0018 P2) ───────────────────────────────────
// 어휘기가 낸 토큰을 **폼**으로 묶는다. 폼 하나는 이렇게 생겼다:
//     <이름> <인자>* .                     닫개는 점
//     <이름> <인자>* do <폼>* end          블록을 여는 `do` 도 머리 폼을 닫는다
//   인자 안에 폼이 올 때는 괄호로 감싼다:  key "f" be ht (mid "ㅏ") (layer num) .
// 폼 하나는 `.` 이나 `do` 로 **꼭 한 번** 닫힌다 — 그래서 닫개의 수가 검사합이 된다(`E-LOW-DOT`).
//
// 이 단계는 **뜻을 모른다.** `layout` 인지 `rule` 인지는 다음 단계(lowlay_check)가 본다.
// 그래야 새 지시문이 생겨도 파서를 고치지 않는다.
#define LOW_FORM_MAX_DEPTH 8

typedef struct LowForm LowForm;

typedef enum { LOW_ITEM_TOK = 0, LOW_ITEM_FORM } LowItemKind;
typedef struct LowItem {
    LowItemKind kind;
    LowTok      tok;      // LOW_ITEM_TOK (문자열은 토큰 배열이 소유한다 — 여기서는 빌려 쓴다)
    LowForm    *form;     // LOW_ITEM_FORM
} LowItem;

struct LowForm {
    int       line, col;
    LowItem  *items;   int nItems;    // items[0] 이 머리(보통 이름)
    LowForm **kids;    int nKids;     // do … end 안의 폼들
    bool      block;                  // do 로 닫혔는가
};

typedef struct LowTree {
    LowForm **forms; int n;
    LowTokens toks;                   // 글월 리터럴의 주인 — 트리가 살아 있는 동안 함께 산다
} LowTree;

bool LowParse_Run(const wchar_t *src, LowTree *out, KlayDiag *diag);
void LowTree_Free(LowTree *t);

// 도움 함수 — 검사·구축 단계가 쓴다.
const wchar_t *LowForm_Head(const LowForm *f);                 // 머리 이름 (없으면 NULL)
const LowItem *LowForm_Item(const LowForm *f, int i);          // 범위 밖이면 NULL
// 항목 [from,to) 를 토큰 줄로 되편다 — 식 해석기가 토큰을 먹기 때문이다. 하위 폼은 괄호로 되돌린다.
bool LowForm_Flatten(const LowForm *f, int from, int to, LowTok *out, int cap, int *n);
