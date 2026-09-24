// lowlay_check.c — 폼 트리를 이 언어의 눈으로 잰다 (RFC-0018 P2).
//   두 번 훑는다. 먼저 선언(`guard` `store` `var` `layer` `behavior` `macro` `group`)을 모으고,
//   그 다음에 가드 식의 이름을 **모은 뒤에** 검사한다 — 파일 아래에서 선언한 가드를 위에서 써도 되게.
#include "lowlay_check.h"
#include <stdlib.h>
#include <string.h>

#define CHK_ERR(f, code, msg, help) \
    do { KlayDiag_Add(diag, KLAY_SEV_ERROR, (f)->line, (f)->col, (code), (msg), (help)); ok = false; } while (0)

// 아는 머리 이름 — 낱말은 예산이다(RFC-0018 §3.9). 여기 없는 것은 오류다.
static const wchar_t *const kHeads[] = {
    L"layout", L"engine", L"requires", L"pipeline", L"dictionary", L"chordterm",
    L"guard", L"store", L"var", L"keys", L"combine",
    L"key", L"map", L"chord", L"hold", L"behavior", L"layer", L"macro", L"group",
    NULL
};
// 가드에서 물을 수 있는 상태 이름 (RFC-0018 §3.3)
static const wchar_t *const kStates[] = {
    L"cho", L"jung", L"jong", L"empty", L"syllables",
    L"repeat", L"prev", L"count",
    L"hangul", L"caps", L"shift", L"layer", L"mode",
    L"before", L"after", L"true", L"false",
    NULL
};

static bool InList(const wchar_t *const *list, const wchar_t *w) {
    for (int i = 0; list[i]; i++) if (!wcscmp(list[i], w)) return true;
    return false;
}

static bool IsTokWord(const LowItem *it, const wchar_t *w) {
    return it && it->kind == LOW_ITEM_TOK && it->tok.kind == LOW_NAME && !wcscmp(it->tok.name, w);
}
static bool IsTokKind(const LowItem *it, LowTokKind k) {
    return it && it->kind == LOW_ITEM_TOK && it->tok.kind == k;
}
static const wchar_t *ItemName(const LowItem *it) {
    return IsTokKind(it, LOW_NAME) ? it->tok.name : NULL;
}

static bool AddSym(wchar_t (*tab)[LOW_NAME_MAX], int *n, const wchar_t *name) {
    if (*n >= LOW_MAX_SYMS) return false;
    for (int i = 0; i < *n; i++) if (!wcscmp(tab[i], name)) return false;   // 두 번 선언
    wcscpy_s(tab[*n], LOW_NAME_MAX, name);
    (*n)++;
    return true;
}
static bool HasSym(wchar_t (*tab)[LOW_NAME_MAX], int n, const wchar_t *name) {
    for (int i = 0; i < n; i++) if (!wcscmp(tab[i], name)) return true;
    return false;
}

#define Flatten(f,a,b,o,c,n) LowForm_Flatten((f),(a),(b),(o),(c),(n))

typedef struct { const LowSyms *syms; } NameCtx;
static bool KnownName(void *ctx, const LowQuery *q) {
    const LowSyms *s = ((NameCtx*)ctx)->syms;
    if (InList(kStates, q->name)) return true;
    return HasSym((wchar_t (*)[LOW_NAME_MAX])s->guard, s->nGuard, q->name) ||
           HasSym((wchar_t (*)[LOW_NAME_MAX])s->var,   s->nVar,   q->name);
}

// `when` 부터 끝 자리까지를 식으로 읽고 이름을 검사한다. 없으면 참(가드 없음).
static bool CheckGuard(const LowForm *f, int from, int to, const LowSyms *syms, KlayDiag *diag) {
    if (from >= to) {
        KlayDiag_Add(diag, KLAY_SEV_ERROR, f->line, f->col, L"E-LOW-SHAPE",
                     L"'when' needs a guard expression", L"key \"f\" when (cho and jung) be jong \"\u3141\" .");
        return false;
    }
    for (int i = from; i < to; i++) {            // 닫개 낱말이 가드 안에 들어오면 모양이 틀린 것이다
        const wchar_t *w = ItemName(&f->items[i]);
        if (w && (!wcscmp(w, L"be") || !wcscmp(w, L"do") || !wcscmp(w, L"end"))) {
            KlayDiag_Add(diag, KLAY_SEV_ERROR, f->line, f->col, L"E-LOW-SHAPE",
                         L"a guard cannot hold 'be', 'do' or 'end'", NULL);
            return false;
        }
    }
    LowTok buf[LOW_EXPR_MAX_TOKENS + 2];
    int n = 0;
    if (!Flatten(f, from, to, buf, LOW_EXPR_MAX_TOKENS, &n)) {
        KlayDiag_Add(diag, KLAY_SEV_ERROR, f->line, f->col, L"E-LOW-EXPR", L"guard is too long (64 tokens)", NULL);
        return false;
    }
    LowTok eof; memset(&eof, 0, sizeof eof); eof.kind = LOW_EOF; eof.line = f->line; eof.col = f->col;
    buf[n++] = eof;
    int used = 0;
    LowExpr *e = LowExpr_Parse(buf, n, &used, diag);
    if (!e) return false;
    bool ok = true;
    if (buf[used].kind != LOW_EOF) {
        KlayDiag_Add(diag, KLAY_SEV_ERROR, buf[used].line, buf[used].col, L"E-LOW-EXPR",
                     L"unexpected token in the guard", L"a guard runs from 'when' to 'be' or 'do'");
        ok = false;
    }
    NameCtx nc = { syms };
    if (!LowExpr_CheckNames(e, KnownName, &nc, diag)) ok = false;
    LowExpr_Free(e);
    return ok;
}

// items 에서 낱말의 자리. 없으면 -1.
static int FindWord(const LowForm *f, const wchar_t *w) {
    for (int i = 0; i < f->nItems; i++) if (IsTokWord(&f->items[i], w)) return i;
    return -1;
}

static bool CheckForm(const LowForm *f, LowCheckResult *r, KlayDiag *diag, bool secondPass);

// map/layer/macro/group 블록 안쪽
static bool CheckBlockKids(const LowForm *f, LowCheckResult *r, KlayDiag *diag, bool second, const wchar_t *what) {
    bool ok = true;
    for (int i = 0; i < f->nKids; i++) {
        const LowForm *k = f->kids[i];
        if (!wcscmp(what, L"map")) {
            // 항목 하나: <글쇠 문자열> <값> .
            if (k->nItems != 2 || !IsTokKind(&k->items[0], LOW_STR)) {
                KlayDiag_Add(diag, KLAY_SEV_ERROR, k->line, k->col, L"E-LOW-SHAPE",
                             L"a map entry is: \"<key>\" <value> .", L"the key is always a string literal");
                ok = false;
                continue;
            }
            if (k->items[0].tok.strLen == 0) {
                KlayDiag_Add(diag, KLAY_SEV_ERROR, k->line, k->col, L"E-LOW-SHAPE", L"the key string is empty", NULL);
                ok = false;
            }
            if (second) r->keys++;
        } else if (!CheckForm(k, r, diag, second)) ok = false;
    }
    return ok;
}

static bool CheckForm(const LowForm *f, LowCheckResult *r, KlayDiag *diag, bool second) {
    bool ok = true;
    const wchar_t *head = LowForm_Head(f);
    if (!head) {
        CHK_ERR(f, L"E-LOW-HEAD", L"a form starts with a name", NULL);
        return false;
    }
    if (!InList(kHeads, head)) {
        wchar_t msg[160];
        swprintf(msg, 160, L"unknown directive '%ls'", head);
        CHK_ERR(f, L"E-LOW-HEAD", msg, L"a misspelled directive is an error, never ignored");
        return false;
    }

    const int n = f->nItems;
    const LowItem *a1 = LowForm_Item(f, 1), *a2 = LowForm_Item(f, 2);
    const int beAt = FindWord(f, L"be");
    const int whenAt = FindWord(f, L"when");

    // ── 선언 (첫 훑기에서 이름을 모은다) ──────────────────────────────────────
    if (!wcscmp(head, L"guard") || !wcscmp(head, L"store") || !wcscmp(head, L"var") ||
        !wcscmp(head, L"behavior")) {
        if (n < 3 || !ItemName(a1) || beAt != 2) {
            CHK_ERR(f, L"E-LOW-SHAPE", L"write: <directive> <name> be <value> .", NULL);
            return false;
        }
        if (!second) {
            wchar_t (*tab)[LOW_NAME_MAX]; int *cnt;
            if (!wcscmp(head, L"guard"))      { tab = r->syms.guard; cnt = &r->syms.nGuard; }
            else if (!wcscmp(head, L"store")) { tab = r->syms.store; cnt = &r->syms.nStore; }
            else if (!wcscmp(head, L"var"))   { tab = r->syms.var;   cnt = &r->syms.nVar;   }
            else                              { tab = r->syms.behav; cnt = &r->syms.nBehav; }
            if (!AddSym(tab, cnt, ItemName(a1))) {
                wchar_t msg[160];
                swprintf(msg, 160, L"'%ls' is declared twice (or there are too many)", ItemName(a1));
                CHK_ERR(f, L"E-LOW-DUP", msg, NULL);
            }
            return ok;
        }
        if (!wcscmp(head, L"guard")) return CheckGuard(f, beAt + 1, n, &r->syms, diag) && ok;
        if (!wcscmp(head, L"store") && !IsTokKind(LowForm_Item(f, 3), LOW_STR)) {
            CHK_ERR(f, L"E-LOW-SHAPE", L"a store holds a string", L"store vowels be \"aeiou\" .");
        }
        if (!wcscmp(head, L"var") && !IsTokKind(LowForm_Item(f, 3), LOW_INT)) {
            CHK_ERR(f, L"E-LOW-SHAPE", L"a var holds a number", L"var mode be 0 .");
        }
        return ok;
    }

    if (!second) {   // 첫 훑기: 블록 이름만 모으고 안쪽은 보지 않는다
        if (!wcscmp(head, L"layer") && ItemName(a1)) AddSym(r->syms.layer, &r->syms.nLayer, ItemName(a1));
        if (!wcscmp(head, L"macro") && ItemName(a1)) AddSym(r->syms.macro, &r->syms.nMacro, ItemName(a1));
        if (!wcscmp(head, L"group") && ItemName(a1)) AddSym(r->syms.group, &r->syms.nGroup, ItemName(a1));
        if (!wcscmp(head, L"layer")) for (int i = 0; i < f->nKids; i++) CheckForm(f->kids[i], r, diag, false);
        return true;
    }

    // ── 머리부 ────────────────────────────────────────────────────────────────
    if (!wcscmp(head, L"layout")) {
        const wchar_t *what = ItemName(a1);
        static const wchar_t *const kFields[] = { L"name", L"format", L"author", L"license", L"version", L"description", NULL };
        if (!what || !InList(kFields, what) || n < 3) {
            CHK_ERR(f, L"E-LOW-SHAPE", L"write: layout name \"...\" . (name|format|author|license|version|description)", NULL);
            return false;
        }
        if (!wcscmp(what, L"format")) {
            if (!IsTokKind(a2, LOW_INT)) { CHK_ERR(f, L"E-LOW-SHAPE", L"layout format takes a number", NULL); }
            else r->format = (int)a2->tok.num;
        } else if (!wcscmp(what, L"name")) {
            if (!IsTokKind(a2, LOW_STR)) { CHK_ERR(f, L"E-LOW-SHAPE", L"layout name takes a string", NULL); }
            else lstrcpynW(r->name, a2->tok.str, 64);
        } else if (!IsTokKind(a2, LOW_STR)) {
            CHK_ERR(f, L"E-LOW-SHAPE", L"this layout field takes a string", NULL);
        }
        return ok;
    }
    if (!wcscmp(head, L"engine")) {
        static const wchar_t *const kEngines[] = { L"hangul", L"rules", L"dict", L"none", NULL };
        const wchar_t *e = ItemName(a1);
        if (n != 2 || !e || !InList(kEngines, e)) {
            CHK_ERR(f, L"E-LOW-KIND", L"engine is one of: hangul rules dict none", NULL);
            return false;
        }
        lstrcpynW(r->engine, e, 16);
        return ok;
    }
    if (!wcscmp(head, L"requires") || !wcscmp(head, L"pipeline")) {
        if (n < 2) CHK_ERR(f, L"E-LOW-SHAPE", L"this directive needs at least one name", NULL);
        for (int i = 1; i < n; i++) if (!ItemName(&f->items[i]))
            CHK_ERR(f, L"E-LOW-SHAPE", L"this directive takes names", NULL);
        return ok;
    }
    if (!wcscmp(head, L"dictionary")) {
        if (n != 2 || !IsTokKind(a1, LOW_STR)) CHK_ERR(f, L"E-LOW-SHAPE", L"dictionary takes a file name string", NULL);
        return ok;
    }
    if (!wcscmp(head, L"chordterm")) {
        if (n != 2 || !IsTokKind(a1, LOW_INT)) CHK_ERR(f, L"E-LOW-SHAPE", L"chordterm takes a number of milliseconds", NULL);
        return ok;
    }
    if (!wcscmp(head, L"keys")) {
        if (n < 2) CHK_ERR(f, L"E-LOW-SHAPE", L"keys takes the key strings of this layout", NULL);
        for (int i = 1; i < n; i++) if (!IsTokKind(&f->items[i], LOW_STR))
            CHK_ERR(f, L"E-LOW-SHAPE", L"keys takes string literals", NULL);
        return ok;
    }
    if (!wcscmp(head, L"combine")) {
        static const wchar_t *const kKinds[] = { L"cho", L"mid", L"jong", NULL };
        const wchar_t *kind = ItemName(a1);
        if (!kind || !InList(kKinds, kind)) { CHK_ERR(f, L"E-LOW-KIND", L"combine is cho, mid or jong", NULL); return false; }
        if (beAt != 4 || n != 6 || !IsTokKind(a2, LOW_STR) || !IsTokKind(LowForm_Item(f, 3), LOW_STR) ||
            !IsTokKind(LowForm_Item(f, 5), LOW_STR)) {
            CHK_ERR(f, L"E-LOW-SHAPE", L"write: combine jong \"ㄱ\" \"ㅅ\" be \"ㄳ\" .", NULL);
            return false;
        }
        r->combines++;
        return ok;
    }

    // ── 글쇠·조합·홀드 ────────────────────────────────────────────────────────
    if (!wcscmp(head, L"key") || !wcscmp(head, L"chord") || !wcscmp(head, L"hold")) {
        // 글쇠 자리: 문자열(그 글자를 내는 글쇠·글쇠열) 또는 물리 글쇠 `(vk 이름)` / `(scan 0x10)`
        bool keyOk = IsTokKind(a1, LOW_STR) && a1->tok.strLen > 0;
        if (!keyOk && a1 && a1->kind == LOW_ITEM_FORM) {
            const wchar_t *h2 = LowForm_Head(a1->form);
            keyOk = h2 && (!wcscmp(h2, L"vk") || !wcscmp(h2, L"scan")) && a1->form->nItems == 2;
        }
        if (!keyOk) {
            CHK_ERR(f, L"E-LOW-SHAPE", L"the key is a string literal or (vk <name>) / (scan <code>)",
                    L"key \"k\" be cho \"ㄱ\" .");
            return false;
        }
        if (beAt < 0 || beAt + 1 >= n) {
            CHK_ERR(f, L"E-LOW-SHAPE", L"this form needs 'be <value>'", NULL);
            return false;
        }
        if (whenAt >= 0) {
            if (whenAt != 2 || whenAt + 1 >= beAt) { CHK_ERR(f, L"E-LOW-SHAPE", L"write: <directive> \"<key>\" when <guard> be <value> .", NULL); return false; }
            if (!CheckGuard(f, whenAt + 1, beAt, &r->syms, diag)) ok = false;
        }
        if (!wcscmp(head, L"key")) r->keys++;
        else if (!wcscmp(head, L"chord")) r->chords++;
        else r->holds++;
        return ok;
    }

    // ── 블록 ──────────────────────────────────────────────────────────────────
    if (!wcscmp(head, L"map")) {
        static const wchar_t *const kKinds[] = { L"jamo", L"cho", L"mid", L"jong", L"char", NULL };
        const wchar_t *kind = ItemName(a1);
        if (!f->block) { CHK_ERR(f, L"E-LOW-SHAPE", L"map opens a block with 'do'", NULL); return false; }
        if (!kind || !InList(kKinds, kind)) { CHK_ERR(f, L"E-LOW-KIND", L"map is jamo, cho, mid, jong or char", NULL); return false; }
        if (whenAt >= 0 && !CheckGuard(f, whenAt + 1, n, &r->syms, diag)) ok = false;
        // 갈래 뒤에는 `when <가드>` 말고 올 것이 없다 — 윗글쇠는 글쇠 문자열이 이미 말한다("Q" = Shift+q)
        if (n > 2 && whenAt != 2) {
            CHK_ERR(f, L"E-LOW-SHAPE", L"after the kind only 'when <guard>' may come",
                    L"the shift face is written in the key string itself: \"Q\" is shift+q");
        }
        r->maps++;
        return CheckBlockKids(f, r, diag, second, L"map") && ok;
    }
    if (!wcscmp(head, L"layer") || !wcscmp(head, L"macro") || !wcscmp(head, L"group")) {
        if (!f->block) { CHK_ERR(f, L"E-LOW-SHAPE", L"this directive opens a block with 'do'", NULL); return false; }
        if (!ItemName(a1)) { CHK_ERR(f, L"E-LOW-SHAPE", L"this block needs a name", NULL); return false; }
        if (!wcscmp(head, L"layer")) {
            if (whenAt >= 0 && !CheckGuard(f, whenAt + 1, n, &r->syms, diag)) ok = false;
            r->layers++;
            return CheckBlockKids(f, r, diag, second, L"layer") && ok;
        }
        if (!wcscmp(head, L"group")) {
            r->rules += f->nKids;        // 규칙의 속은 P3 에서 본다
            for (int i = 0; i < f->nKids; i++) {
                const wchar_t *kh = LowForm_Head(f->kids[i]);
                if (!kh || wcscmp(kh, L"rule")) {
                    KlayDiag_Add(diag, KLAY_SEV_ERROR, f->kids[i]->line, f->kids[i]->col,
                                 L"E-LOW-SHAPE", L"a group holds rules", NULL);
                    ok = false;
                }
            }
            return ok;
        }
        return ok;                       // macro 의 속은 P3 (동작 계층)
    }
    return ok;
}

bool LowCheck_Run(const LowTree *tree, LowCheckResult *out, KlayDiag *diag) {
    memset(out, 0, sizeof *out);
    bool ok = true;
    for (int i = 0; i < tree->n; i++) if (!CheckForm(tree->forms[i], out, diag, false)) ok = false;
    for (int i = 0; i < tree->n; i++) if (!CheckForm(tree->forms[i], out, diag, true))  ok = false;
    if (out->format && out->format != 4) {
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 0, L"E-LOW-FORMAT", L"this jamotong reads layout format 4", NULL);
        ok = false;
    }
    return ok;
}
