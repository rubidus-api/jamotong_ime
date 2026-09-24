// lowlay_build.c — v4 자판을 엔진이 쓰는 표로 (RFC-0018 P3).
//   여기서 하는 일은 셋이다: 파일을 읽고, 폼을 훑어 표를 채우고, 자모 글자를 번호로 바꾼다.
//   자리(초·중·종)를 못 박는 것은 세 벌 자판이고, 두 벌 자판은 낱자만 말한다 — 배치는 오토마타가 한다.
#include "lowlay_build.h"
#include "lowlay_parse.h"
#include "lowlay_expr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 호환 자모 글자 → 번호. 파일에 `cho "ㄱ"` 처럼 적힌 것을 읽는다.
static const wchar_t *const kCho  = L"ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ";
static const wchar_t *const kJung = L"ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ";
static const wchar_t *const kJong = L" ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ";

static int IndexOf(const wchar_t *table, wchar_t c, int from) {
    for (int i = from; table[i]; i++) if (table[i] == c) return i;
    return -1;
}
static bool IsConsonant(wchar_t c) { return IndexOf(kCho, c, 0) >= 0; }

// 값 하나를 (종류, 번호)로. 글자(`"ㄱ"`)든 번호(`0`)든 받는다. 못 읽으면 거짓.
static bool ValueOf(const LowTok *t, JamoType type, int *idx) {
    if (t->kind == LOW_INT) { *idx = (int)t->num; return true; }
    if (t->kind != LOW_STR || t->strLen < 1) return false;
    const wchar_t *tab = type == JAMO_CHO ? kCho : type == JAMO_JUNG ? kJung : kJong;
    int i = IndexOf(tab, t->str[0], type == JAMO_JONG ? 1 : 0);
    if (i < 0) return false;
    *idx = i;
    return true;
}

static bool ValidIdx(JamoType t, int idx) {
    return t == JAMO_CHO  ? (idx >= 0 && idx <= 18)
         : t == JAMO_JUNG ? (idx >= 0 && idx <= 20)
                          : (idx >= 1 && idx <= 27);
}

static void Err(KlayDiag *d, const LowForm *f, const wchar_t *code, const wchar_t *msg, const wchar_t *help) {
    KlayDiag_Add(d, KLAY_SEV_ERROR, f ? f->line : 0, f ? f->col : 0, code, msg, help);
}

// ── 가드 ──────────────────────────────────────────────────────────────────────
#define BUILD_MAX_GUARDS 32
typedef struct { wchar_t name[LOW_NAME_MAX]; LowExpr *e; } NamedGuard;
typedef struct { NamedGuard v[BUILD_MAX_GUARDS]; int n; } GuardTab;

static int GuardState(void *ctx, const wchar_t *name) {
    (void)ctx;
    if (!wcscmp(name, L"cho"))   return 0;
    if (!wcscmp(name, L"jung"))  return 1;
    if (!wcscmp(name, L"jong"))  return 2;
    if (!wcscmp(name, L"empty")) return 3;
    return -1;
}
static const LowExpr *GuardNamed(void *ctx, const wchar_t *name) {
    const GuardTab *t = (const GuardTab*)ctx;
    for (int i = 0; i < t->n; i++) if (!wcscmp(t->v[i].name, name)) return t->v[i].e;
    return NULL;
}
// 폼의 [from,to) 를 식으로 읽는다. 실패하면 NULL.
static LowExpr *ExprOf(const LowForm *f, int from, int to, KlayDiag *diag) {
    LowTok buf[LOW_EXPR_MAX_TOKENS + 2];
    int n = 0;
    if (from >= to || !LowForm_Flatten(f, from, to, buf, LOW_EXPR_MAX_TOKENS, &n)) return NULL;
    LowTok eof; memset(&eof, 0, sizeof eof); eof.kind = LOW_EOF; eof.line = f->line; eof.col = f->col;
    buf[n++] = eof;
    int used = 0;
    return LowExpr_Parse(buf, n, &used, diag);
}
static int FindWordAt(const LowForm *f, const wchar_t *w) {
    for (int i = 0; i < f->nItems; i++)
        if (f->items[i].kind == LOW_ITEM_TOK && f->items[i].tok.kind == LOW_NAME &&
            !wcscmp(f->items[i].tok.name, w)) return i;
    return -1;
}

static bool HasWord(const LowForm *f, const wchar_t *w) {
    for (int i = 0; i < f->nItems; i++)
        if (f->items[i].kind == LOW_ITEM_TOK && f->items[i].tok.kind == LOW_NAME &&
            !wcscmp(f->items[i].tok.name, w)) return true;
    return false;
}
static const wchar_t *Arg1Name(const LowForm *f) {
    const LowItem *it = LowForm_Item(f, 1);
    return (it && it->kind == LOW_ITEM_TOK && it->tok.kind == LOW_NAME) ? it->tok.name : NULL;
}

bool LowBuild_IsV4(const wchar_t *src) {
    if (!src) return false;
    for (const wchar_t *p = src; *p; ) {
        while (*p == L' ' || *p == L'\t') p++;
        if (!wcsncmp(p, L"layout ", 7) || !wcsncmp(p, L"layout\t", 7)) return true;
        while (*p && *p != L'\n') p++;
        if (*p) p++;
    }
    return false;
}

bool LowBuild_Hangul(const LowTree *t, const LowCheckResult *c, HangulLayout *out, KlayDiag *diag) {
    memset(out, 0, sizeof *out);
    lstrcpynW(out->name, c->name[0] ? c->name : L"custom", 64);
    out->composition = HL_SEBEOL;
    bool ok = true, sawJamo = false, sawSlot = false;

    // 선언된 가드를 먼저 모은다 — 파일 어디에 적혀 있어도 쓰게.
    GuardTab gt = { .n = 0 };
    for (int i = 0; i < t->n; i++) {
        const LowForm *f = t->forms[i];
        const wchar_t *h = LowForm_Head(f);
        if (!h || wcscmp(h, L"guard")) continue;
        const wchar_t *nm = Arg1Name(f);
        int beAt = FindWordAt(f, L"be");
        if (!nm || beAt < 0 || gt.n >= BUILD_MAX_GUARDS) continue;
        LowExpr *e = ExprOf(f, beAt + 1, f->nItems, diag);
        if (!e) { Err(diag, f, L"E-JMT-V4-GUARD", L"this guard cannot be read", NULL); ok = false; continue; }
        lstrcpynW(gt.v[gt.n].name, nm, LOW_NAME_MAX);
        gt.v[gt.n++].e = e;
    }

    for (int i = 0; i < t->n; i++) {
        const LowForm *f = t->forms[i];
        const wchar_t *head = LowForm_Head(f);
        if (!head) continue;

        if (!wcscmp(head, L"map")) {
            const wchar_t *kind = Arg1Name(f);
            if (!kind) continue;
            unsigned char code[HL_GUARD_CODE];
            int codeLen = 0;
            if (HasWord(f, L"when")) {          // 가드 붙은 블록 — 조건을 작은 프로그램으로 굽는다
                int whenAt = FindWordAt(f, L"when");
                LowExpr *ge = ExprOf(f, whenAt + 1, f->nItems, diag);
                LowCompile comp = { GuardState, GuardNamed, &gt };
                codeLen = ge ? LowExpr_Compile(ge, &comp, code, HL_GUARD_CODE) : -1;
                LowExpr_Free(ge);
                if (codeLen <= 0) {
                    Err(diag, f, L"E-JMT-V4-GUARD", L"this guard cannot be compiled for the engine",
                        L"the engine reads cho, jung, jong, empty and guards made of them");
                    ok = false;
                    continue;
                }
            }
            JamoType ty = !wcscmp(kind, L"cho")  ? JAMO_CHO
                        : !wcscmp(kind, L"mid")  ? JAMO_JUNG
                        : !wcscmp(kind, L"jong") ? JAMO_JONG : JAMO_NONE;
            bool jamoKind = !wcscmp(kind, L"jamo");
            if (ty != JAMO_NONE) sawSlot = true;
            if (jamoKind) sawJamo = true;
            if (ty == JAMO_NONE && !jamoKind) continue;    // map char 는 한글 자판에서 글쇠를 잡지 않는다

            for (int e = 0; e < f->nKids; e++) {
                const LowForm *kid = f->kids[e];
                if (kid->nItems != 2) continue;
                const LowTok *key = &kid->items[0].tok, *val = &kid->items[1].tok;
                if (key->kind != LOW_STR || key->strLen != 1) {
                    Err(diag, kid, L"E-JMT-V4-KEY", L"this build maps one-character keys only", NULL);
                    ok = false;
                    continue;
                }
                int k = (int)key->str[0];
                if (k < 0 || k > 127) { Err(diag, kid, L"E-JMT-ASCII", L"key must be an ASCII character", NULL); ok = false; continue; }
                JamoType kt = ty;
                if (jamoKind) {                 // 두 벌: 자음은 초성 자리에, 모음은 중성 자리에 넣는다
                    if (val->kind != LOW_STR || val->strLen < 1) { Err(diag, kid, L"E-JMT-V4-VALUE", L"a jamo letter is expected", NULL); ok = false; continue; }
                    kt = IsConsonant(val->str[0]) ? JAMO_CHO : JAMO_JUNG;
                }
                int idx = 0;
                if (!ValueOf(val, kt, &idx) || !ValidIdx(kt, idx)) {
                    Err(diag, kid, L"E-JMT-V4-VALUE", L"this is not a jamo of that kind",
                        L"write the jamo letter itself, e.g. cho \"ㄱ\"");
                    ok = false;
                    continue;
                }
                if (codeLen > 0) {              // 가드 붙은 줄 — 조건과 함께 따로 담는다
                    if (out->guardedCount >= HL_MAX_GUARDED) {
                        Err(diag, kid, L"E-JMT-RANGE", L"too many guarded keys", NULL);
                        ok = false;
                        continue;
                    }
                    HangulGuarded *g = &out->guarded[out->guardedCount++];
                    g->key = (unsigned char)k;
                    g->len = (unsigned char)codeLen;
                    memcpy(g->code, code, (size_t)codeLen);
                    g->r.type = kt; g->r.index = idx;
                    // 가드만 있는 글쇠도 "낱자 글쇠"로 보이게 기본값을 채워 둔다(소비 판정·표시용).
                    if (out->keymap[k].type == JAMO_NONE) { out->keymap[k].type = kt; out->keymap[k].index = idx; }
                    continue;
                }
                out->keymap[k].type = kt;
                out->keymap[k].index = idx;
            }
            continue;
        }

        if (!wcscmp(head, L"combine")) {
            const wchar_t *kind = Arg1Name(f);
            if (!kind || f->nItems != 6) continue;
            JamoType ty = !wcscmp(kind, L"cho") ? JAMO_CHO : !wcscmp(kind, L"mid") ? JAMO_JUNG : JAMO_JONG;
            int a = 0, b = 0, r = 0;
            if (!ValueOf(&f->items[2].tok, ty, &a) || !ValueOf(&f->items[3].tok, ty, &b) ||
                !ValueOf(&f->items[5].tok, ty, &r) || !ValidIdx(ty, a) || !ValidIdx(ty, b) || !ValidIdx(ty, r)) {
                Err(diag, f, L"E-JMT-V4-VALUE", L"combine takes three jamo of the same kind", NULL);
                ok = false;
                continue;
            }
            if (out->combineCount >= HL_MAX_COMBINE) {
                Err(diag, f, L"E-JMT-RANGE", L"too many combine rules", NULL);
                ok = false;
                continue;
            }
            HangulCombine *hc = &out->combines[out->combineCount++];
            hc->type = ty; hc->a = a; hc->b = b; hc->result = r;
            continue;
        }

        if (!wcscmp(head, L"moachigi")) out->moachigi = 1;
    }

    if (sawJamo && sawSlot) {
        Err(diag, NULL, L"E-JMT-V4-MIX", L"a layout is either two-set (map jamo) or three-set (map cho/mid/jong)",
            L"two-set layouts do not say the slot - the automaton decides it");
        ok = false;
    }
    if (sawJamo) out->composition = HL_DUBEOL;
    for (int i = 0; i < gt.n; i++) LowExpr_Free(gt.v[i].e);
    return ok;
}

// UTF-8 파일 한 덩이를 넓은 글월로. 실패하면 NULL(호출자가 free).
static wchar_t *ReadAllWide(const wchar_t *path) {
    FILE *f = _wfopen(path, L"r, ccs=UTF-8");
    if (!f) return NULL;
    size_t cap = 8192, n = 0;
    wchar_t *buf = (wchar_t*)malloc(cap * sizeof *buf);
    if (!buf) { fclose(f); return NULL; }
    for (;;) {
        if (n + 1024 >= cap) {
            size_t ncap = cap * 2;
            wchar_t *nb = (wchar_t*)realloc(buf, ncap * sizeof *nb);
            if (!nb) { free(buf); fclose(f); return NULL; }
            buf = nb; cap = ncap;
        }
        if (!fgetws(buf + n, 1024, f)) break;
        n += wcslen(buf + n);
    }
    fclose(f);
    buf[n] = L'\0';
    return buf;
}

// 정적 자판(engine none): `map char` 한 장이 곧 자판이다.
static bool BuildStatic(const LowTree *t, const LowCheckResult *c, LayoutConfig *out, KlayDiag *diag) {
    memset(out, 0, sizeof *out);
    for (int i = 0; i < 256; i++) out->charMap[i] = (wchar_t)i;
    bool ok = true, any = false;
    for (int i = 0; i < t->n; i++) {
        const LowForm *f = t->forms[i];
        const wchar_t *head = LowForm_Head(f);
        if (!head || wcscmp(head, L"map")) continue;
        const wchar_t *kind = Arg1Name(f);
        if (!kind || wcscmp(kind, L"char")) continue;
        if (HasWord(f, L"when")) {
            Err(diag, f, L"E-JMT-V4-GUARD", L"a static layout takes no guards", NULL);
            ok = false;
            continue;
        }
        for (int e = 0; e < f->nKids; e++) {
            const LowForm *kid = f->kids[e];
            if (kid->nItems != 2) continue;
            const LowTok *key = &kid->items[0].tok, *val = &kid->items[1].tok;
            if (key->kind != LOW_STR || key->strLen != 1 || val->kind != LOW_STR || val->strLen != 1) {
                Err(diag, kid, L"E-JMT-V4-KEY", L"a static entry maps one character to one character", NULL);
                ok = false;
                continue;
            }
            int k = (int)key->str[0];
            if (k < 0 || k > 255) { Err(diag, kid, L"E-JMT-LATIN1", L"key must be a Latin-1 character", NULL); ok = false; continue; }
            out->charMap[k] = val->str[0];
            any = true;
        }
    }
    out->type = any ? LAYOUT_TYPE_STATIC_MAP : LAYOUT_TYPE_PASSTHROUGH;
    out->name = _wcsdup(c->name[0] ? c->name : L"layout");
    lstrcpynW(out->abbrev, out->name ? out->name : L"??", 4);
    return ok && out->name != NULL;
}

bool LowBuild_LoadFile(const wchar_t *path, LayoutConfig *out, KlayDiag *diag, bool *isV4) {
    if (isV4) *isV4 = false;
    wchar_t *src = ReadAllWide(path);
    if (!src) return false;
    if (!LowBuild_IsV4(src)) { free(src); return false; }
    if (isV4) *isV4 = true;

    LowTree tree;
    bool ok = LowParse_Run(src, &tree, diag);
    LowCheckResult res;
    if (ok) ok = LowCheck_Run(&tree, &res, diag);
    if (ok && (!wcscmp(res.engine, L"none") || !wcscmp(res.engine, L"static"))) {
        ok = BuildStatic(&tree, &res, out, diag);
        LowTree_Free(&tree);
        free(src);
        return ok;
    }
    if (ok) {
        HangulLayout *hl = (HangulLayout*)calloc(1, sizeof *hl);
        if (!hl) ok = false;
        else if (!LowBuild_Hangul(&tree, &res, hl, diag)) { free(hl); ok = false; }
        else {
            memset(out, 0, sizeof *out);
            out->type = LAYOUT_TYPE_HANGUL_CUSTOM;
            out->kbdVariant = KBD_SEBEOL;
            out->pHangulLayout = hl;
            out->name = _wcsdup(hl->name[0] ? hl->name : L"custom");
            lstrcpynW(out->abbrev, out->name ? out->name : L"??", 4);
            if (!out->name) { free(hl); ok = false; }
        }
    }
    LowTree_Free(&tree);
    free(src);
    return ok;
}
