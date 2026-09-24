#pragma once
#include "lowlay_lex.h"

// ── 자판 정의 언어 v4 — 식과 가드 (RFC-0018 §3.3, P1) ───────────────────────────
// 특수문자를 쓰지 않는다. 연산자는 낱말이다:
//     not · and · or · eq ne lt le gt ge   (and 가 or 보다 강하게 묶는다)
// 값은 정수·문자(코드값)·상태 이름이다. 상태는 `이름` 또는 `이름 인자` 꼴로 읽는다 — `layer num`.
// 식에는 부작용이 없다. 대입(`set`)은 동작이지 식이 아니다.
//
// 유한성(§3.8): 토큰 64개·괄호 깊이 8을 넘기면 `E-LOW-EXPR` 로 거절한다. 로드할 때 잰다.
#define LOW_EXPR_MAX_TOKENS 64
#define LOW_EXPR_MAX_DEPTH  8

typedef struct LowExpr LowExpr;

// 상태 읽기 — 엔진이 물려준다. 모르는 이름이면 거짓을 돌려준다(그러면 값은 0).
typedef struct LowQuery { const wchar_t *name; const wchar_t *arg; } LowQuery;
typedef struct LowState {
    bool (*read)(void *ctx, const LowQuery *q, long long *out);
    void *ctx;
} LowState;

// 토큰 열에서 식 하나를 읽는다. used 에 먹은 토큰 수를 넣는다. 실패하면 NULL 이고 diag 에 사유가 쌓인다.
LowExpr *LowExpr_Parse(const LowTok *v, int n, int *used, KlayDiag *diag);
// 글 한 줄에서 곧장 (시험·도구용). 남는 토큰이 있으면 오류다.
LowExpr *LowExpr_ParseText(const wchar_t *src, KlayDiag *diag);
long long LowExpr_Eval(const LowExpr *e, const LowState *st);
// 쓰인 상태 이름을 모두 확인한다(모르는 이름은 조용히 거짓이 되지 않고 오류다).
bool LowExpr_CheckNames(const LowExpr *e, bool (*known)(void *ctx, const LowQuery *q), void *ctx,
                        KlayDiag *diag);
void LowExpr_Free(LowExpr *e);
