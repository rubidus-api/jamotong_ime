// lowlay_expr.c — 가드 식의 해석과 평가 (RFC-0018 P1).
//   문법(왼쪽이 약하게 묶는다):
//     식   := 논리합
//     논리합 := 논리곱 ( or 논리곱 )*
//     논리곱 := 비교   ( and 비교 )*
//     비교   := 단항 [ (eq|ne|lt|le|gt|ge) 단항 ]        비교는 이어 쓸 수 없다
//     단항   := not 단항 | 원자
//     원자   := 수 | 문자 | 글월 | ( 식 ) | 이름 [인자]
//   `이름 인자` 는 상태 하나를 가리킨다(`layer num`). 인자는 하나까지다 — 더 필요해지면 RFC 로 늘린다.
#include "lowlay_expr.h"
#include <stdlib.h>
#include <string.h>

typedef enum { LX_NUM, LX_STR, LX_NAME, LX_NOT, LX_AND, LX_OR, LX_CMP } LowExprKind;
typedef enum { CMP_EQ, CMP_NE, CMP_LT, CMP_LE, CMP_GT, CMP_GE } LowCmp;

struct LowExpr {
    LowExprKind kind;
    long long   num;                      // LX_NUM
    wchar_t     name[LOW_NAME_MAX];       // LX_NAME
    wchar_t     arg[LOW_NAME_MAX];        // LX_NAME 의 인자 (없으면 빈 글월)
    LowCmp      cmp;
    LowExpr    *l, *r;
};

typedef struct {
    const LowTok *v;
    int n, at, depth;
    KlayDiag *diag;
    bool bad;
} P;

static void Err(P *p, const wchar_t *code, const wchar_t *msg, const wchar_t *help) {
    const LowTok *t = &p->v[p->at < p->n ? p->at : p->n - 1];
    KlayDiag_Add(p->diag, KLAY_SEV_ERROR, t->line, t->col, code, msg, help);
    p->bad = true;
}

static LowExpr *New(LowExprKind k) {
    LowExpr *e = (LowExpr*)calloc(1, sizeof *e);
    if (e) e->kind = k;
    return e;
}

void LowExpr_Free(LowExpr *e) {
    if (!e) return;
    LowExpr_Free(e->l);
    LowExpr_Free(e->r);
    free(e);
}

static bool IsWord(const LowTok *t, const wchar_t *w) {
    return t->kind == LOW_NAME && !wcscmp(t->name, w);
}
static bool IsReserved(const LowTok *t) {
    static const wchar_t *const kOps[] = { L"and", L"or", L"not", L"eq", L"ne", L"lt", L"le", L"gt", L"ge", NULL };
    if (t->kind != LOW_NAME) return false;
    for (int i = 0; kOps[i]; i++) if (!wcscmp(t->name, kOps[i])) return true;
    return false;
}

static LowExpr *ParseOr(P *p);

static LowExpr *ParseAtom(P *p) {
    const LowTok *t = &p->v[p->at];
    if (t->kind == LOW_INT || t->kind == LOW_CHAR) {
        LowExpr *e = New(LX_NUM);
        if (!e) return NULL;
        e->num = t->num; p->at++;
        return e;
    }
    if (t->kind == LOW_STR) {                      // 글월은 비교에서만 뜻이 있다 (길이 1 = 코드값)
        LowExpr *e = New(LX_NUM);
        if (!e) return NULL;
        e->num = t->strLen == 1 ? (long long)(unsigned)t->str[0] : -1;
        p->at++;
        return e;
    }
    if (t->kind == LOW_LP) {
        if (p->depth + 1 > LOW_EXPR_MAX_DEPTH) { Err(p, L"E-LOW-EXPR", L"expression is nested too deep (8)", NULL); return NULL; }
        p->at++; p->depth++;
        LowExpr *e = ParseOr(p);
        p->depth--;
        if (!e) return NULL;
        if (p->v[p->at].kind != LOW_RP) { Err(p, L"E-LOW-EXPR", L"missing ')'", NULL); LowExpr_Free(e); return NULL; }
        p->at++;
        return e;
    }
    if (t->kind == LOW_NAME && !IsReserved(t)) {
        LowExpr *e = New(LX_NAME);
        if (!e) return NULL;
        wcscpy_s(e->name, LOW_NAME_MAX, t->name);
        if (!wcscmp(t->name, L"true"))  { e->kind = LX_NUM; e->num = 1; p->at++; return e; }
        if (!wcscmp(t->name, L"false")) { e->kind = LX_NUM; e->num = 0; p->at++; return e; }
        p->at++;
        const LowTok *a = &p->v[p->at];             // 인자 하나까지 (`layer num`)
        if (a->kind == LOW_NAME && !IsReserved(a)) { wcscpy_s(e->arg, LOW_NAME_MAX, a->name); p->at++; }
        return e;
    }
    Err(p, L"E-LOW-EXPR", L"expected a value, a name or '('", L"operators are words: not and or eq ne lt le gt ge");
    return NULL;
}

static LowExpr *ParseUnary(P *p) {
    if (IsWord(&p->v[p->at], L"not")) {
        p->at++;
        if (p->depth + 1 > LOW_EXPR_MAX_DEPTH) { Err(p, L"E-LOW-EXPR", L"expression is nested too deep (8)", NULL); return NULL; }
        p->depth++;
        LowExpr *in = ParseUnary(p);
        p->depth--;
        if (!in) return NULL;
        LowExpr *e = New(LX_NOT);
        if (!e) { LowExpr_Free(in); return NULL; }
        e->l = in;
        return e;
    }
    return ParseAtom(p);
}

static LowExpr *ParseCmp(P *p) {
    LowExpr *l = ParseUnary(p);
    if (!l) return NULL;
    static const struct { const wchar_t *w; LowCmp c; } kC[] = {
        { L"eq", CMP_EQ }, { L"ne", CMP_NE }, { L"lt", CMP_LT },
        { L"le", CMP_LE }, { L"gt", CMP_GT }, { L"ge", CMP_GE },
    };
    for (size_t i = 0; i < sizeof kC / sizeof kC[0]; i++) {
        if (IsWord(&p->v[p->at], kC[i].w)) {
            p->at++;
            LowExpr *r = ParseUnary(p);
            if (!r) { LowExpr_Free(l); return NULL; }
            LowExpr *e = New(LX_CMP);
            if (!e) { LowExpr_Free(l); LowExpr_Free(r); return NULL; }
            e->cmp = kC[i].c; e->l = l; e->r = r;
            for (size_t j = 0; j < sizeof kC / sizeof kC[0]; j++)
                if (IsWord(&p->v[p->at], kC[j].w)) {
                    Err(p, L"E-LOW-EXPR", L"comparisons cannot be chained", L"write (a lt b) and (b lt c)");
                    LowExpr_Free(e);
                    return NULL;
                }
            return e;
        }
    }
    return l;
}

static LowExpr *ParseAnd(P *p) {
    LowExpr *l = ParseCmp(p);
    if (!l) return NULL;
    while (IsWord(&p->v[p->at], L"and")) {
        p->at++;
        LowExpr *r = ParseCmp(p);
        if (!r) { LowExpr_Free(l); return NULL; }
        LowExpr *e = New(LX_AND);
        if (!e) { LowExpr_Free(l); LowExpr_Free(r); return NULL; }
        e->l = l; e->r = r; l = e;
    }
    return l;
}

static LowExpr *ParseOr(P *p) {
    LowExpr *l = ParseAnd(p);
    if (!l) return NULL;
    while (IsWord(&p->v[p->at], L"or")) {
        p->at++;
        LowExpr *r = ParseAnd(p);
        if (!r) { LowExpr_Free(l); return NULL; }
        LowExpr *e = New(LX_OR);
        if (!e) { LowExpr_Free(l); LowExpr_Free(r); return NULL; }
        e->l = l; e->r = r; l = e;
    }
    return l;
}

LowExpr *LowExpr_Parse(const LowTok *v, int n, int *used, KlayDiag *diag) {
    P p = { v, n, 0, 0, diag, false };
    if (n > LOW_EXPR_MAX_TOKENS + 1) {   // +1 은 EOF
        KlayDiag_Add(diag, KLAY_SEV_ERROR, v[0].line, v[0].col, L"E-LOW-EXPR",
                     L"expression is too long (64 tokens)", NULL);
        if (used) *used = 0;
        return NULL;
    }
    LowExpr *e = ParseOr(&p);
    if (used) *used = p.at;
    if (!e || p.bad) { LowExpr_Free(e); return NULL; }
    return e;
}

LowExpr *LowExpr_ParseText(const wchar_t *src, KlayDiag *diag) {
    LowTokens t;
    if (!LowLex_Run(src, &t, diag)) { LowTokens_Free(&t); return NULL; }
    int used = 0;
    LowExpr *e = LowExpr_Parse(t.v, t.n, &used, diag);
    if (e && t.v[used].kind != LOW_EOF) {
        KlayDiag_Add(diag, KLAY_SEV_ERROR, t.v[used].line, t.v[used].col, L"E-LOW-EXPR",
                     L"unexpected token after the expression", NULL);
        LowExpr_Free(e);
        e = NULL;
    }
    LowTokens_Free(&t);
    return e;
}

long long LowExpr_Eval(const LowExpr *e, const LowState *st) {
    if (!e) return 0;
    switch (e->kind) {
        case LX_NUM: case LX_STR: return e->num;
        case LX_NOT: return LowExpr_Eval(e->l, st) ? 0 : 1;
        case LX_AND: return LowExpr_Eval(e->l, st) ? (LowExpr_Eval(e->r, st) ? 1 : 0) : 0;   // 단락
        case LX_OR:  return LowExpr_Eval(e->l, st) ? 1 : (LowExpr_Eval(e->r, st) ? 1 : 0);
        case LX_CMP: {
            long long a = LowExpr_Eval(e->l, st), b = LowExpr_Eval(e->r, st);
            switch (e->cmp) {
                case CMP_EQ: return a == b; case CMP_NE: return a != b;
                case CMP_LT: return a <  b; case CMP_LE: return a <= b;
                case CMP_GT: return a >  b; default:     return a >= b;
            }
        }
        case LX_NAME: {
            if (!st || !st->read) return 0;
            LowQuery q = { e->name, e->arg[0] ? e->arg : NULL };
            long long v = 0;
            return st->read(st->ctx, &q, &v) ? v : 0;
        }
    }
    return 0;
}

bool LowExpr_CheckNames(const LowExpr *e, bool (*known)(void *ctx, const LowQuery *q), void *ctx,
                        KlayDiag *diag) {
    if (!e) return true;
    if (e->kind == LX_NAME) {
        LowQuery q = { e->name, e->arg[0] ? e->arg : NULL };
        if (known && !known(ctx, &q)) {
            wchar_t msg[160];
            swprintf(msg, 160, L"unknown state name '%ls'", e->name);
            KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 0, L"E-LOW-EXPR-NAME", msg,
                         L"a misspelled name is an error, never a silent false");
            return false;
        }
        return true;
    }
    bool ok = LowExpr_CheckNames(e->l, known, ctx, diag);
    if (!LowExpr_CheckNames(e->r, known, ctx, diag)) ok = false;
    return ok;
}

// ── 후위 프로그램으로 컴파일 ──────────────────────────────────────────────────
#define HLG_HAS 0x01
#define HLG_IDX 0x02
#define HLG_NUM 0x03
#define HLG_NOT 0x10
#define HLG_AND 0x11
#define HLG_OR  0x12
#define HLG_EQ  0x20

static int Emit(unsigned char *out, int cap, int at, unsigned char a) {
    if (at < 0 || at >= cap) return -1;
    out[at] = a;
    return at + 1;
}
static int Comp(const LowExpr *e, const LowCompile *c, unsigned char *out, int cap, int at, int depth, bool wantIdx);

static int CompName(const LowExpr *e, const LowCompile *c, unsigned char *out, int cap, int at, int depth, bool wantIdx) {
    if (e->arg[0]) return -1;                      // `layer num` 같은 인자 있는 상태는 아직 못 싣는다
    int id = c && c->state ? c->state(c->ctx, e->name) : -1;
    if (id >= 0) {
        at = Emit(out, cap, at, wantIdx ? HLG_IDX : HLG_HAS);
        return Emit(out, cap, at, (unsigned char)id);
    }
    const LowExpr *sub = c && c->guard ? c->guard(c->ctx, e->name) : NULL;
    if (!sub || depth >= 4) return -1;             // 가드가 가드를 부르는 깊이는 넷까지
    return Comp(sub, c, out, cap, at, depth + 1, wantIdx);
}

static int Comp(const LowExpr *e, const LowCompile *c, unsigned char *out, int cap, int at, int depth, bool wantIdx) {
    if (!e || at < 0) return -1;
    switch (e->kind) {
        case LX_NUM: case LX_STR:
            if (e->num < 0 || e->num > 255) return -1;
            at = Emit(out, cap, at, HLG_NUM);
            return Emit(out, cap, at, (unsigned char)e->num);
        case LX_NAME:
            return CompName(e, c, out, cap, at, depth, wantIdx);
        case LX_NOT:
            at = Comp(e->l, c, out, cap, at, depth, false);
            return Emit(out, cap, at, HLG_NOT);
        case LX_AND: case LX_OR:
            at = Comp(e->l, c, out, cap, at, depth, false);
            at = Comp(e->r, c, out, cap, at, depth, false);
            return Emit(out, cap, at, e->kind == LX_AND ? HLG_AND : HLG_OR);
        case LX_CMP:
            at = Comp(e->l, c, out, cap, at, depth, true);    // 견줄 때는 번호를 본다
            at = Comp(e->r, c, out, cap, at, depth, true);
            return Emit(out, cap, at, (unsigned char)(HLG_EQ + (int)e->cmp));
    }
    return -1;
}

int LowExpr_Compile(const LowExpr *e, const LowCompile *c, unsigned char *out, int cap) {
    int n = Comp(e, c, out, cap, 0, 0, false);
    return n;
}
