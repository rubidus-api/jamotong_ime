// lowlay_chord.c — v4 문법으로 적은 조합 자판을 엔진 표로 (RFC-0018).
//   동작의 **뜻은 여기서 정하지 않는다.** v4 폼을 3판 동작 문자열로 되적어 chord_parse.c 의
//   해석기에 넘긴다(ChordLayout_ActionText). 같은 뜻을 두 곳에 적으면 언젠가 갈라지기 때문이다.
//
//   v4                                  →  3판 동작 문자열
//   text "the"                             text "the"
//   key back                               key back
//   key back (mods shift ctrl)             key back mods(shift,ctrl)
//   mod shift · layer num                  mod shift · layer num
//   oneshot (mod shift) · sticky (…)       oneshot mod(shift)
//   momentary (layer num)                  momentary layer(num)
//   toggle (layer mouse)                   toggle layer(mouse)
//   pointer (move 1 0) (profile fast)      pointer move(1,0) profile(fast)
//   pointer (click left)                   pointer click(left)
//   mouse move 1 0                         mouse move 1 0
//   macro sign · cancel                    macro sign · cancel actions
#include "lowlay_build.h"
#include "lowlay_parse.h"
#include "chord_layout.h"
#include <stdlib.h>
#include <string.h>

#define CHORD_RHS_MAX 512

static void ChErr(KlayDiag *d, const LowForm *f, const wchar_t *code, const wchar_t *msg, const wchar_t *help) {
    KlayDiag_Add(d, KLAY_SEV_ERROR, f ? f->line : 0, f ? f->col : 0, code, msg, help);
}

static const wchar_t *ItemNameOf(const LowItem *it) {
    return (it && it->kind == LOW_ITEM_TOK && it->tok.kind == LOW_NAME) ? it->tok.name : NULL;
}
static bool Append(wchar_t *buf, size_t cap, const wchar_t *s) {
    size_t n = wcslen(buf), m = wcslen(s);
    if (n + m + 1 >= cap) return false;
    wcscat(buf, s);
    return true;
}
static bool AppendQuoted(wchar_t *buf, size_t cap, const wchar_t *s) {
    if (!Append(buf, cap, L"\"")) return false;
    for (; *s; s++) {
        wchar_t one[3];
        int k = 0;
        if (*s == L'"' || *s == L'\\') one[k++] = L'\\';
        one[k++] = *s;
        one[k] = L'\0';
        if (!Append(buf, cap, one)) return false;
    }
    return Append(buf, cap, L"\"");
}
static bool AppendInt(wchar_t *buf, size_t cap, long long v) {
    wchar_t num[24];
    swprintf(num, 24, L"%lld", v);
    return Append(buf, cap, num);
}

// 하위 폼 `(이름 인자…)` 을 `이름(인자,인자)` 꼴로.
static bool ParenForm(const LowForm *f, wchar_t *buf, size_t cap) {
    const wchar_t *head = LowForm_Head(f);
    if (!head) return false;
    if (!Append(buf, cap, head) || !Append(buf, cap, L"(")) return false;
    for (int i = 1; i < f->nItems; i++) {
        if (i > 1 && !Append(buf, cap, L",")) return false;
        const LowItem *it = &f->items[i];
        if (it->kind != LOW_ITEM_TOK) return false;
        if (it->tok.kind == LOW_NAME) { if (!Append(buf, cap, it->tok.name)) return false; }
        else if (it->tok.kind == LOW_INT) { if (!AppendInt(buf, cap, it->tok.num)) return false; }
        else if (it->tok.kind == LOW_STR) { if (!Append(buf, cap, it->tok.str)) return false; }
        else return false;
    }
    return Append(buf, cap, L")");
}

// 폼의 [from,to) 를 3판 동작 문자열로 되적는다.
static bool RenderAction(const LowForm *f, int from, int to, wchar_t *buf, size_t cap, bool isHold) {
    buf[0] = L'\0';
    if (from >= to) return false;
    const wchar_t *verb = ItemNameOf(&f->items[from]);
    if (!verb) return false;

    if (!wcscmp(verb, L"text") || !wcscmp(verb, L"symbol")) {
        const LowItem *s = LowForm_Item(f, from + 1);
        if (to - from != 2 || !s || s->kind != LOW_ITEM_TOK || s->tok.kind != LOW_STR) return false;
        return Append(buf, cap, verb) && Append(buf, cap, L" ") && AppendQuoted(buf, cap, s->tok.str);
    }
    if (!wcscmp(verb, L"cancel")) {
        if (to - from != 1) return false;
        return Append(buf, cap, L"cancel actions");
    }
    if (!wcscmp(verb, L"sticky")) verb = L"oneshot";   // ZMK 의 낱말을 같은 뜻으로 받는다
    // 짧게 적은 `mod shift` · `layer num` 은 자리에 맞게 편다 —
    //   chord 자리면 한 번만 먹는 원샷, hold 자리면 누르고 있는 동안만(모멘터리).
    if ((!wcscmp(verb, L"mod") || !wcscmp(verb, L"layer")) && to - from == 2) {
        const wchar_t *arg = ItemNameOf(&f->items[from + 1]);
        if (!arg) return false;
        return Append(buf, cap, isHold ? L"momentary " : L"oneshot ") && Append(buf, cap, verb) &&
               Append(buf, cap, L"(") && Append(buf, cap, arg) && Append(buf, cap, L")");
    }

    if (!Append(buf, cap, verb)) return false;
    for (int i = from + 1; i < to; i++) {
        if (!Append(buf, cap, L" ")) return false;
        const LowItem *it = &f->items[i];
        if (it->kind == LOW_ITEM_FORM) { if (!ParenForm(it->form, buf, cap)) return false; }
        else if (it->tok.kind == LOW_NAME) { if (!Append(buf, cap, it->tok.name)) return false; }
        else if (it->tok.kind == LOW_INT) { if (!AppendInt(buf, cap, it->tok.num)) return false; }
        else if (it->tok.kind == LOW_STR) { if (!AppendQuoted(buf, cap, it->tok.str)) return false; }
        else return false;
    }
    return true;
}

static int FindWordIdx(const LowForm *f, const wchar_t *w) {
    for (int i = 0; i < f->nItems; i++) {
        const wchar_t *n = ItemNameOf(&f->items[i]);
        if (n && !wcscmp(n, w)) return i;
    }
    return -1;
}

// `chord "ar" be …` / `hold "e" be …` 한 줄
static bool AddChord(ChordLayout *cl, const LowForm *f, int layer, bool isHold, KlayDiag *diag) {
    const LowItem *k = LowForm_Item(f, 1);
    int beAt = FindWordIdx(f, L"be");
    if (!k || k->kind != LOW_ITEM_TOK || k->tok.kind != LOW_STR || k->tok.strLen < 1 || beAt < 2) {
        ChErr(diag, f, L"E-JMT-SHAPE", L"write: chord \"<keys>\" be <action> .", NULL);
        return false;
    }
    unsigned mask = 0;
    for (int i = 0; i < k->tok.strLen; i++) {
        int c = (int)k->tok.str[i];
        if (c < 0 || c > 127 || cl->keyBit[c] < 0) {
            ChErr(diag, f, L"E-JMT-CHORD-KEY", L"this key is not one of the layout's chord keys",
                  L"declare them first: keys \"arts\" \"eyio\" .");
            return false;
        }
        mask |= 1u << cl->keyBit[c];
    }
    if (cl->chordCount >= CL_MAX_CHORDS) {
        ChErr(diag, f, L"E-JMT-RANGE", L"too many chords", NULL);
        return false;
    }
    wchar_t rhs[CHORD_RHS_MAX];
    if (!RenderAction(f, beAt + 1, f->nItems, rhs, CHORD_RHS_MAX, isHold)) {
        ChErr(diag, f, L"E-JMT-ACTION", L"this action cannot be read", NULL);
        return false;
    }
    ChordEntry e;
    memset(&e, 0, sizeof e);
    e.mask = mask; e.layer = layer; e.isHold = isHold ? 1 : 0;
    int rc = ChordLayout_ActionText(cl, &e, rhs, e.isHold, false);
    if (rc != 0) {
        ChErr(diag, f, L"E-JMT-ACTION", L"this action is not one the engine knows",
              L"text, key, mod, layer, oneshot, momentary, toggle, switch, pointer, mouse, macro, cancel");
        return false;
    }
    cl->chords[cl->chordCount++] = e;
    return true;
}

static bool AddMacro(ChordLayout *cl, const LowForm *f, KlayDiag *diag) {
    const wchar_t *name = ItemNameOf(LowForm_Item(f, 1));
    if (!name || !f->block) { ChErr(diag, f, L"E-JMT-SHAPE", L"write: macro <name> do ... end", NULL); return false; }
    if (cl->macroCount >= CL_MAX_MACROS) { ChErr(diag, f, L"E-JMT-RANGE", L"too many macros", NULL); return false; }
    ChordMacro *m = &cl->macros[cl->macroCount];
    memset(m, 0, sizeof *m);
    lstrcpynW(m->name, name, 32);
    m->first = cl->stepCount;
    int withDepth = 0;
    for (int i = 0; i < f->nKids; i++) {
        const LowForm *kid = f->kids[i];
        if (cl->stepCount >= CL_MAX_STEPS) { ChErr(diag, kid, L"E-JMT-RANGE", L"too many macro steps", NULL); return false; }
        wchar_t line[CHORD_RHS_MAX];
        if (!RenderAction(kid, 0, kid->nItems, line, CHORD_RHS_MAX, false)) {
            ChErr(diag, kid, L"E-JMT-ACTION", L"this macro step cannot be read", NULL);
            return false;
        }
        ChordMacroStep st;
        memset(&st, 0, sizeof st);
        if (ChordLayout_MacroStepText(cl, &st, line, &withDepth) != 0) {
            ChErr(diag, kid, L"E-JMT-ACTION", L"this macro step is not one the engine knows",
                  L"text, key, pointer, wait, with (mods ...), endwith");
            return false;
        }
        cl->steps[cl->stepCount++] = st;
        m->count++;
    }
    if (withDepth != 0) { ChErr(diag, f, L"E-JMT-ACTION", L"a 'with' in this macro is never closed by 'endwith'", NULL); return false; }
    cl->macroCount++;
    return true;
}

bool LowBuild_Chord(const LowTree *t, const LowCheckResult *c, ChordLayout *cl, KlayDiag *diag) {
    memset(cl, 0, sizeof *cl);
    lstrcpynW(cl->name, c->name[0] ? c->name : L"chord", 64);
    cl->v3 = 1;                                  // v4 는 언제나 3판 판정 규칙을 쓴다
    cl->comboTermMs = 50;
    cl->holdTermMs = 200;
    cl->holdPolicy = CHORD_HOLD_INTERRUPT;
    for (int i = 0; i < 128; i++) cl->keyBit[i] = -1;
    ChordLayout_LayerId(cl, L"base");            // 0 번은 언제나 base
    bool ok = true;

    // 1) 글쇠와 타이밍 먼저 — 조합이 그것을 가리킨다
    int bit = 0;
    for (int i = 0; i < t->n; i++) {
        const LowForm *f = t->forms[i];
        const wchar_t *head = LowForm_Head(f);
        if (!head) continue;
        if (!wcscmp(head, L"keys")) {
            for (int j = 1; j < f->nItems; j++) {
                const LowItem *it = &f->items[j];
                if (it->kind != LOW_ITEM_TOK || it->tok.kind != LOW_STR) { ChErr(diag, f, L"E-JMT-SHAPE", L"keys takes string literals", NULL); ok = false; continue; }
                for (int k = 0; k < it->tok.strLen; k++) {
                    int ch = (int)it->tok.str[k];
                    if (ch < 0 || ch > 127) { ChErr(diag, f, L"E-JMT-ASCII", L"a chord key must be an ASCII character", NULL); ok = false; continue; }
                    if (bit > 31) { ChErr(diag, f, L"E-JMT-RANGE", L"a chord layout holds at most 32 keys", NULL); ok = false; continue; }
                    cl->keyBit[ch] = bit++;
                }
            }
        } else if (!wcscmp(head, L"chordterm") || !wcscmp(head, L"holdterm")) {
            const LowItem *v = LowForm_Item(f, 1);
            bool isCombo = !wcscmp(head, L"chordterm");
            long long ms = (v && v->kind == LOW_ITEM_TOK && v->tok.kind == LOW_INT) ? v->tok.num : -1;
            long long hi = isCombo ? 1000 : 5000;
            if (ms < 1 || ms > hi) { ChErr(diag, f, L"E-JMT-RANGE", isCombo ? L"chordterm must be 1..1000 ms" : L"holdterm must be 1..5000 ms", NULL); ok = false; }
            else if (isCombo) cl->comboTermMs = (int)ms;
            else cl->holdTermMs = (int)ms;
        } else if (!wcscmp(head, L"holdpolicy")) {
            const wchar_t *v = ItemNameOf(LowForm_Item(f, 1));
            if (v && !wcscmp(v, L"interrupt")) cl->holdPolicy = CHORD_HOLD_INTERRUPT;
            else if (v && !wcscmp(v, L"timeout")) cl->holdPolicy = CHORD_HOLD_TIMEOUT;
            else { ChErr(diag, f, L"E-JMT-KIND", L"holdpolicy is interrupt or timeout", NULL); ok = false; }
        }
    }
    // 2) 매크로 — 조합이 이름으로 가리키므로 먼저 만든다
    for (int i = 0; i < t->n; i++) {
        const LowForm *f = t->forms[i];
        const wchar_t *head = LowForm_Head(f);
        if (head && !wcscmp(head, L"macro") && !AddMacro(cl, f, diag)) ok = false;
    }
    // 3) 조합과 레이어
    for (int i = 0; i < t->n; i++) {
        const LowForm *f = t->forms[i];
        const wchar_t *head = LowForm_Head(f);
        if (!head) continue;
        if (!wcscmp(head, L"chord") || !wcscmp(head, L"hold")) {
            if (!AddChord(cl, f, 0, !wcscmp(head, L"hold"), diag)) ok = false;
        } else if (!wcscmp(head, L"layer") && f->block) {
            const wchar_t *name = ItemNameOf(LowForm_Item(f, 1));
            int layer = name ? ChordLayout_LayerId(cl, name) : -1;
            if (layer < 0) { ChErr(diag, f, L"E-JMT-RANGE", L"too many layers (or the layer has no name)", NULL); ok = false; continue; }
            for (int k = 0; k < f->nKids; k++) {
                const LowForm *kid = f->kids[k];
                const wchar_t *kh = LowForm_Head(kid);
                if (!kh) continue;
                if (!wcscmp(kh, L"chord") || !wcscmp(kh, L"hold")) {
                    if (!AddChord(cl, kid, layer, !wcscmp(kh, L"hold"), diag)) ok = false;
                } else if (wcscmp(kh, L"map") != 0) {     // map 은 조합 자판에서 뜻이 없다 — 조용히 지나친다
                    ChErr(diag, kid, L"E-JMT-SHAPE", L"a layer of a chord layout holds chord and hold lines", NULL);
                    ok = false;
                }
            }
        }
    }
    if (cl->chordCount == 0) {
        ChErr(diag, NULL, L"E-JMT-SHAPE", L"a chord layout needs at least one chord", NULL);
        ok = false;
    }
    return ok;
}
