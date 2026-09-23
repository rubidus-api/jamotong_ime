// seq_layout.c — 순차 변환 자판 (.jmt 3판 `Type = input` + `Engine = sequence`, RFC-0016 §6.3).
//   표만으로 도는 엔진이다: 사전도, 후보창도, 네트워크도 없다.
#include "seq_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ── 표 ─────────────────────────────────────────────────────────────────────────────
static const SeqEntry *ExactOf(const SeqLayout *sl, const wchar_t *s) {
    for (int i = 0; i < sl->count; i++) if (!wcscmp(sl->v[i].in, s)) return &sl->v[i];
    return NULL;
}
// s 로 시작하면서 s 보다 긴 정의가 있는가 (= 더 기다릴 값어치가 있는가)
static bool HasLonger(const SeqLayout *sl, const wchar_t *s) {
    size_t n = wcslen(s);
    for (int i = 0; i < sl->count; i++)
        if (wcslen(sl->v[i].in) > n && !wcsncmp(sl->v[i].in, s, n)) return true;
    return false;
}
static bool StartsAny(const SeqLayout *sl, wchar_t ch) {
    for (int i = 0; i < sl->count; i++) if (sl->v[i].in[0] == ch) return true;
    return false;
}
// buf 의 앞부분과 온전히 맞는 가장 긴 정의
static const SeqEntry *LongestPrefix(const SeqLayout *sl, const wchar_t *buf) {
    const SeqEntry *best = NULL; size_t bn = 0;
    for (int i = 0; i < sl->count; i++) {
        size_t n = wcslen(sl->v[i].in);
        if (n > bn && !wcsncmp(buf, sl->v[i].in, n)) { best = &sl->v[i]; bn = n; }
    }
    return best;
}

// ── 결과 담기 ───────────────────────────────────────────────────────────────────────
static void Emit(SeqResult *r, const wchar_t *s) {
    size_t have = wcslen(r->committed), add = wcslen(s);
    size_t cap = sizeof(r->committed) / sizeof(r->committed[0]);
    if (have + add + 1 > cap) return;   // 한 번의 입력이 낳을 수 있는 최대치보다 넉넉하다
    wcscpy(r->committed + have, s);
}
// 막다른 길: 앞에서부터 확정할 수 있는 만큼 확정하고, 남은 꼬리는 다시 보류한다.
//   atBoundary 면 더 기다리지 않는다(자판 전환·포커스 상실) — 남은 것은 리터럴로 확정한다.
static void FlushBuf(SeqState *st, const SeqLayout *sl, wchar_t *buf, SeqResult *r, bool atBoundary) {
    st->pending[0] = L'\0';
    while (*buf) {
        if (!atBoundary && HasLonger(sl, buf)) {          // 꼬리가 아직 자랄 수 있다
            lstrcpynW(st->pending, buf, SEQ_MAX_IN + 1);
            return;
        }
        const SeqEntry *e = LongestPrefix(sl, buf);
        if (e) { Emit(r, e->out); buf += wcslen(e->in); }
        else   { wchar_t lit[2] = { buf[0], 0 }; Emit(r, lit); buf++; }   // 표에 없는 글자는 친 그대로
    }
}

void SeqKb_Init(SeqState *st) { st->pending[0] = L'\0'; }

SeqResult SeqKb_Key(SeqState *st, const SeqLayout *sl, wchar_t ch) {
    SeqResult r; memset(&r, 0, sizeof r);
    if (ch < 0x21 || ch > 0x7E) {
        // 사이띄개·글쇠 아닌 문자는 엔진이 만지지 않는다 (단추·단축키·낱말 나누기를 응용에 맡긴다).
        // 보류가 있었으면 잃지 않도록 먼저 확정하고, 글쇠 자체는 응용으로 보낸다.
        if (st->pending[0]) { r = SeqKb_Flush(st, sl); r.eaten = false; }
        return r;
    }
    if (!st->pending[0] && !StartsAny(sl, ch)) {   // 표 밖의 글쇠 — 응용이 그대로 받는다
        r.eaten = false;
        return r;
    }
    wchar_t buf[SEQ_MAX_IN + 2];
    lstrcpynW(buf, st->pending, SEQ_MAX_IN + 2);
    size_t n = wcslen(buf);
    if (n + 1 < SEQ_MAX_IN + 2) { buf[n] = ch; buf[n + 1] = L'\0'; }
    r.eaten = true;
    if (HasLonger(sl, buf)) {                      // 최장 일치를 기다린다
        lstrcpynW(st->pending, buf, SEQ_MAX_IN + 1);
    } else if (ExactOf(sl, buf)) {
        Emit(&r, ExactOf(sl, buf)->out);
        st->pending[0] = L'\0';
    } else if (sl->onUnmatched == SEQ_UNMATCHED_CANCEL) {
        st->pending[0] = L'\0';                    // 오류 없이 취소 — 보류와 그 글쇠가 사라진다
    } else {
        FlushBuf(st, sl, buf, &r, false);          // 확정 + 한 번의 재처리
    }
    lstrcpynW(r.composing, st->pending, SEQ_MAX_IN + 1);
    return r;
}

bool SeqKb_WouldEat(const SeqState *st, const SeqLayout *sl, wchar_t ch) {
    if (!sl || ch < 0x21 || ch > 0x7E) return false;
    return st->pending[0] != L'\0' || StartsAny(sl, ch);
}

SeqResult SeqKb_Backspace(SeqState *st, const SeqLayout *sl) {
    (void)sl;
    SeqResult r; memset(&r, 0, sizeof r);
    size_t n = wcslen(st->pending);
    if (n == 0) return r;                          // 확정된 남의 글자는 추측해서 지우지 않는다
    st->pending[n - 1] = L'\0';
    r.eaten = true;
    lstrcpynW(r.composing, st->pending, SEQ_MAX_IN + 1);
    return r;
}

SeqResult SeqKb_Cancel(SeqState *st) {
    SeqResult r; memset(&r, 0, sizeof r);
    if (!st->pending[0]) return r;
    st->pending[0] = L'\0';
    r.eaten = true;
    return r;
}

SeqResult SeqKb_Flush(SeqState *st, const SeqLayout *sl) {
    SeqResult r; memset(&r, 0, sizeof r);
    if (!st->pending[0]) return r;
    wchar_t buf[SEQ_MAX_IN + 2];
    lstrcpynW(buf, st->pending, SEQ_MAX_IN + 2);
    r.eaten = true;
    FlushBuf(st, sl, buf, &r, true);
    lstrcpynW(r.composing, st->pending, SEQ_MAX_IN + 1);
    return r;
}

// ── 로더 ───────────────────────────────────────────────────────────────────────────
static void TrimEnds(wchar_t *s) {
    size_t n = wcslen(s);
    while (n && (s[n-1] == L'\n' || s[n-1] == L'\r' || s[n-1] == L' ' || s[n-1] == L'\t')) s[--n] = L'\0';
}
static const wchar_t *SkipWs(const wchar_t *p) { while (*p == L' ' || *p == L'\t') p++; return p; }
static bool AtEnd(const wchar_t *p) { p = SkipWs(p); return *p == L'\0' || *p == L'#'; }

static const wchar_t *const kSeqDirectives[] = { L"Sequence", L"Engine", L"OnUnmatched", NULL };

SeqLayout *SeqLayout_LoadFromLines(const KlayLines *L, KlayDiag *diag) {
    SeqLayout *sl = (SeqLayout *)calloc(1, sizeof(SeqLayout));
    if (!sl) return NULL;
    wcscpy(sl->name, L"sequence");
    sl->onUnmatched = SEQ_UNMATCHED_FLUSH;
    unsigned char *srcFile = (unsigned char *)calloc(SEQ_MAX_ENTRIES, 1);
    if (!srcFile) { free(sl); return NULL; }
    bool bad = false;
    #define FAIL(c, code, msg, help) do { KlayDiag_Add(diag, KLAY_SEV_ERROR, lineno, (c), code, msg, help); bad = true; } while (0)

    wchar_t line[256];
    for (int li = 0; li < L->n; li++) {
        lstrcpynW(line, L->v[li].text, 256);
        const int lineno = L->v[li].line;
        if (diag) diag->curFile = L->files[L->v[li].file];
        TrimEnds(line);
        wchar_t *p = line;
        while (*p == L' ' || *p == L'\t') p++;
        if (*p == L'\0' || *p == L'#') continue;
        const int col0 = (int)(p - line) + 1;

        if (swscanf(p, L"Name = %63l[^\n]", sl->name) == 1) { TrimEnds(sl->name); continue; }
        if (!wcsncmp(p, L"OnUnmatched", 11)) {
            wchar_t v[32] = {0};
            if (swscanf(p, L"OnUnmatched = %31ls", v) != 1)
                FAIL(col0, L"E-JMT-VALUE", L"OnUnmatched needs a value", L"OnUnmatched = flush | cancel");
            else if (!_wcsicmp(v, L"flush"))  sl->onUnmatched = SEQ_UNMATCHED_FLUSH;
            else if (!_wcsicmp(v, L"cancel")) sl->onUnmatched = SEQ_UNMATCHED_CANCEL;
            else FAIL(col0, L"E-JMT-VALUE", L"OnUnmatched must be flush or cancel",
                      L"flush types the pending letters, cancel drops them");
            continue;
        }
        if (!wcsncmp(p, L"Sequence ", 9)) {
            const wchar_t *q = SkipWs(p + 9);
            wchar_t in[SEQ_MAX_IN + 2] = {0}, out[SEQ_MAX_OUT + 2] = {0};
            if (*q != L'"' || !Klay_ParseQuoted(&q, in, SEQ_MAX_IN + 2)) {
                FAIL(col0, L"E-JMT-STRING", L"Sequence: the input side must be a quoted string",
                     L"e.g. Sequence \"ka\" = emit \"\\u{304B}\"");
                continue;
            }
            for (size_t i = 0; in[i]; i++)
                if (in[i] < 0x21 || in[i] > 0x7E) {
                    FAIL(col0, L"E-JMT-SEQ", L"Sequence: the input side takes printable ASCII without spaces",
                         L"the input side is what is typed on the keyboard");
                    in[0] = L'\0'; break;
                }
            if (!in[0]) continue;
            q = SkipWs(q);
            if (*q != L'=') { FAIL(col0, L"E-JMT-ACTION", L"Sequence: expected '=' after the input string", NULL); continue; }
            q = SkipWs(q + 1);
            if (wcsncmp(q, L"emit", 4) || (q[4] != L' ' && q[4] != L'\t' && q[4] != L'"')) {
                FAIL(col0, L"E-JMT-ACTION", L"Sequence: the action must be 'emit \"...\"'",
                     L"emit makes the engine's own letters; use text only in chord layouts");
                continue;
            }
            q = SkipWs(q + 4);
            if (*q != L'"' || !Klay_ParseQuoted(&q, out, SEQ_MAX_OUT + 2)) {
                FAIL(col0, L"E-JMT-STRING", L"Sequence: emit needs a quoted string", NULL); continue;
            }
            if (!AtEnd(q)) { FAIL(col0, L"E-JMT-ACTION", L"Sequence: unexpected text after the emitted string", NULL); continue; }
            if (wcslen(in) > SEQ_MAX_IN || wcslen(out) > SEQ_MAX_OUT) {
                FAIL(col0, L"E-JMT-LIMIT", L"Sequence: the input takes up to 8 letters and emit up to 16", NULL); continue;
            }
            int at = -1;
            for (int i = 0; i < sl->count; i++) if (!wcscmp(sl->v[i].in, in)) { at = i; break; }
            if (at >= 0) {
                if (srcFile[at] == (unsigned char)L->v[li].file) {   // 같은 파일 안의 중복은 실수다
                    FAIL(col0, L"E-JMT-DUP-SEQ", L"this sequence is defined twice in one file",
                         L"remove one of the two lines");
                    continue;
                }
                // 다른 파일(Extends/Include): 나중 정의가 이긴다 (RFC-0016 §4)
            } else {
                if (sl->count >= SEQ_MAX_ENTRIES) { FAIL(col0, L"E-JMT-LIMIT", L"too many sequences (max 512)", NULL); continue; }
                at = sl->count++;
            }
            wcscpy(sl->v[at].in, in);
            wcscpy(sl->v[at].out, out);
            srcFile[at] = (unsigned char)L->v[li].file;
            if ((int)wcslen(in) > sl->maxIn) sl->maxIn = (int)wcslen(in);
            continue;
        }
        if (!wcsncmp(p, L"Engine", 6)) continue;        // 통합 로더가 이미 읽었다
        if (KlayHeader_IsKnownKey(p)) continue;
        if (Klay_UnknownLine(diag, p, lineno, col0, kSeqDirectives)) bad = true;
    }
    #undef FAIL
    if (diag) diag->curFile = NULL;
    free(srcFile);
    if (bad || sl->count == 0) {
        if (!bad) KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-SEQ",
                               L"a sequence layout needs at least one Sequence line",
                               L"e.g. Sequence \"ka\" = emit \"\\u{304B}\"");
        free(sl);
        return NULL;
    }
    return sl;
}

SeqLayout *SeqLayout_LoadFromFile(const wchar_t *path, KlayDiag *diag) {
    KlayLines L;
    bool built = KlayLines_Build(&L, path, diag);
    SeqLayout *sl = built ? SeqLayout_LoadFromLines(&L, diag) : NULL;
    KlayLines_Free(&L);
    return sl;
}

void SeqLayout_Free(SeqLayout *sl) { free(sl); }
