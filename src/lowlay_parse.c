// lowlay_parse.c — 토큰을 폼 트리로 (RFC-0018 P2).
//   규칙은 셋뿐이다:
//     · 폼은 이름으로 열리고 `.` 또는 `do` 로 닫힌다. 개행은 닫지 않는다.
//     · `do` 로 닫힌 폼은 `end` 까지를 자식으로 가진다.
//     · 괄호 안은 폼 하나이며 `)` 로 닫힌다 — 그 안에는 점이 오지 않는다.
//   뜻은 보지 않는다. 머리 이름이 무엇인지는 다음 단계가 정한다.
#include "lowlay_parse.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const LowTok *v;
    int n, at, depth;
    KlayDiag *diag;
    bool bad;
} PS;

static void Err(PS *p, const wchar_t *code, const wchar_t *msg, const wchar_t *help) {
    const LowTok *t = &p->v[p->at < p->n ? p->at : p->n - 1];
    KlayDiag_Add(p->diag, KLAY_SEV_ERROR, t->line, t->col, code, msg, help);
    p->bad = true;
}

static bool IsWord(const LowTok *t, const wchar_t *w) {
    return t->kind == LOW_NAME && !wcscmp(t->name, w);
}

static LowForm *FormNew(int line, int col) {
    LowForm *f = (LowForm*)calloc(1, sizeof *f);
    if (f) { f->line = line; f->col = col; }
    return f;
}

static void FormFree(LowForm *f) {
    if (!f) return;
    for (int i = 0; i < f->nItems; i++) if (f->items[i].kind == LOW_ITEM_FORM) FormFree(f->items[i].form);
    free(f->items);
    for (int i = 0; i < f->nKids; i++) FormFree(f->kids[i]);
    free(f->kids);
    free(f);
}

static bool AddItem(LowForm *f, const LowItem *it) {
    LowItem *nv = (LowItem*)realloc(f->items, (size_t)(f->nItems + 1) * sizeof *nv);
    if (!nv) return false;
    f->items = nv;
    f->items[f->nItems++] = *it;
    return true;
}

static bool AddKid(LowForm *f, LowForm *kid) {
    LowForm **nv = (LowForm**)realloc(f->kids, (size_t)(f->nKids + 1) * sizeof *nv);
    if (!nv) return false;
    f->kids = nv;
    f->kids[f->nKids++] = kid;
    return true;
}

// 괄호 안의 폼 하나. `(` 는 이미 먹었다.
static LowForm *ParseParen(PS *p) {
    if (p->depth + 1 > LOW_FORM_MAX_DEPTH) {
        Err(p, L"E-LOW-DEPTH", L"forms are nested too deep (8)", NULL);
        return NULL;
    }
    p->depth++;
    LowForm *f = FormNew(p->v[p->at].line, p->v[p->at].col);
    if (!f) { p->depth--; return NULL; }
    while (p->at < p->n) {
        const LowTok *t = &p->v[p->at];
        if (t->kind == LOW_RP) { p->at++; p->depth--; return f; }
        if (t->kind == LOW_EOF) { Err(p, L"E-LOW-PAREN", L"missing ')'", NULL); break; }
        if (t->kind == LOW_DOT) { Err(p, L"E-LOW-DOT", L"a dot cannot close a form inside parentheses", NULL); break; }
        LowItem it;
        memset(&it, 0, sizeof it);
        if (t->kind == LOW_LP) {
            p->at++;
            LowForm *sub = ParseParen(p);
            if (!sub) break;
            it.kind = LOW_ITEM_FORM; it.form = sub;
        } else {
            it.kind = LOW_ITEM_TOK; it.tok = *t;
            p->at++;
        }
        if (!AddItem(f, &it)) { if (it.kind == LOW_ITEM_FORM) FormFree(it.form); break; }
    }
    p->depth--;
    FormFree(f);
    return NULL;
}

static LowForm *ParseForm(PS *p, bool inBlock);

// 한 폼: 인자들을 모으다가 `.` 이나 `do` 를 만난다.
static LowForm *ParseForm(PS *p, bool inBlock) {
    const LowTok *first = &p->v[p->at];
    LowForm *f = FormNew(first->line, first->col);
    if (!f) return NULL;

    while (p->at < p->n) {
        const LowTok *t = &p->v[p->at];
        if (t->kind == LOW_EOF) {
            Err(p, L"E-LOW-DOT", L"this form is never closed", L"close it with ' .' or open a block with 'do'");
            FormFree(f);
            return NULL;
        }
        if (t->kind == LOW_DOT) { p->at++; return f; }          // 닫개
        if (IsWord(t, L"end")) {
            if (inBlock && f->nItems == 0) { FormFree(f); return NULL; }   // 빈 자리에서 만난 end — 블록의 끝
            Err(p, L"E-LOW-END", L"'end' closes a block, not a form", L"close the form with ' .' first");
            FormFree(f);
            return NULL;
        }
        if (IsWord(t, L"do")) {                                  // 머리를 닫고 블록을 연다
            p->at++;
            f->block = true;
            if (p->depth + 1 > LOW_FORM_MAX_DEPTH) { Err(p, L"E-LOW-DEPTH", L"blocks are nested too deep (8)", NULL); FormFree(f); return NULL; }
            p->depth++;
            for (;;) {
                if (p->at >= p->n || p->v[p->at].kind == LOW_EOF) {
                    Err(p, L"E-LOW-END", L"this block is never closed by 'end'", NULL);
                    p->depth--; FormFree(f); return NULL;
                }
                if (IsWord(&p->v[p->at], L"end")) { p->at++; p->depth--; return f; }
                LowForm *kid = ParseForm(p, true);
                if (!kid) {
                    if (p->bad) { p->depth--; FormFree(f); return NULL; }
                    continue;                                    // 빈 폼(방금 end) — 위에서 걸린다
                }
                if (!AddKid(f, kid)) { FormFree(kid); p->depth--; FormFree(f); return NULL; }
            }
        }
        LowItem it;
        memset(&it, 0, sizeof it);
        if (t->kind == LOW_LP) {
            p->at++;
            LowForm *sub = ParseParen(p);
            if (!sub) { FormFree(f); return NULL; }
            it.kind = LOW_ITEM_FORM; it.form = sub;
        } else if (t->kind == LOW_RP) {
            Err(p, L"E-LOW-PAREN", L"unmatched ')'", NULL);
            FormFree(f);
            return NULL;
        } else {
            it.kind = LOW_ITEM_TOK; it.tok = *t;
            p->at++;
        }
        if (!AddItem(f, &it)) { if (it.kind == LOW_ITEM_FORM) FormFree(it.form); FormFree(f); return NULL; }
    }
    FormFree(f);
    return NULL;
}

bool LowParse_Run(const wchar_t *src, LowTree *out, KlayDiag *diag) {
    memset(out, 0, sizeof *out);
    if (!LowLex_Run(src, &out->toks, diag)) { LowTokens_Free(&out->toks); return false; }

    PS p = { out->toks.v, out->toks.n, 0, 0, diag, false };
    while (p.at < p.n && p.v[p.at].kind != LOW_EOF) {
        if (IsWord(&p.v[p.at], L"end")) {
            Err(&p, L"E-LOW-END", L"'end' without a block", NULL);
            break;
        }
        LowForm *f = ParseForm(&p, false);
        if (!f) break;
        LowForm **nv = (LowForm**)realloc(out->forms, (size_t)(out->n + 1) * sizeof *nv);
        if (!nv) { FormFree(f); break; }
        out->forms = nv;
        out->forms[out->n++] = f;
    }
    return !p.bad;
}

void LowTree_Free(LowTree *t) {
    if (!t) return;
    for (int i = 0; i < t->n; i++) FormFree(t->forms[i]);
    free(t->forms);
    LowTokens_Free(&t->toks);
    memset(t, 0, sizeof *t);
}

const wchar_t *LowForm_Head(const LowForm *f) {
    if (!f || f->nItems == 0) return NULL;
    const LowItem *it = &f->items[0];
    return (it->kind == LOW_ITEM_TOK && it->tok.kind == LOW_NAME) ? it->tok.name : NULL;
}

const LowItem *LowForm_Item(const LowForm *f, int i) {
    if (!f || i < 0 || i >= f->nItems) return NULL;
    return &f->items[i];
}

static bool FlattenForm(const LowForm *sub, LowTok *out, int cap, int *n) {
    LowTok lp; memset(&lp, 0, sizeof lp); lp.kind = LOW_LP; lp.line = sub->line; lp.col = sub->col;
    if (*n >= cap) return false;
    out[(*n)++] = lp;
    if (!LowForm_Flatten(sub, 0, sub->nItems, out, cap, n)) return false;
    LowTok rp; memset(&rp, 0, sizeof rp); rp.kind = LOW_RP; rp.line = sub->line; rp.col = sub->col;
    if (*n >= cap) return false;
    out[(*n)++] = rp;
    return true;
}

bool LowForm_Flatten(const LowForm *f, int from, int to, LowTok *out, int cap, int *n) {
    for (int i = from; i < to; i++) {
        const LowItem *it = &f->items[i];
        if (it->kind == LOW_ITEM_TOK) {
            if (*n >= cap) return false;
            out[(*n)++] = it->tok;
        } else if (!FlattenForm(it->form, out, cap, n)) return false;
    }
    return true;
}
