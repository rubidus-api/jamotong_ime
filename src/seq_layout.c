// seq_layout.c — 순차 변환 **런타임**: 사전을 열고 친 글자열을 바꾼다 (RFC-0016 §6.3).
//   파일을 읽는 일은 도구 쪽(seq_parse.c)이다 — 입력기는 구운 자판만 읽는다.
#include "seq_layout.h"
#include "config.h"      // Config_IsSafeDictFileName / 사전 폴더
#include "chord_layout.h"   // 앞단 조합 인식기 (RFC-0016 §6.3)
#include "kana_tables.h"    // 일본어 방식의 F8~F10 (생성한 표)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ── 사전에 묻는 두 가지 (엔진이 쓰는 말로) ─────────────────────────────────────────
// 친 글쇠를 읽기로 바꾸는 표: 쌍병을 골랐으면 그 표, 아니면 Dictionary.
static const JDict *KeyDict(const SeqLayout *sl) {
    if (sl->scheme > 0 && sl->scheme <= sl->nScheme && sl->schemeDict[sl->scheme - 1]) return sl->schemeDict[sl->scheme - 1];
    return sl->dict;
}
static bool HasLonger(const SeqLayout *sl, const wchar_t *s) { return JDict_HasLonger(KeyDict(sl), s); }
static bool Exact(const SeqLayout *sl, const wchar_t *s, wchar_t *out, int cap) {
    const jdchar *v = NULL; int n = 0;
    if (!JDict_Exact(KeyDict(sl), s, &v, &n)) return false;
    return JDict_CopyValue(v, n, out, cap) >= 0;
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
static void Deliver(SeqState *st, const SeqLayout *sl, SeqResult *r, const wchar_t *s);

static void FlushBuf(SeqState *st, const SeqLayout *sl, wchar_t *buf, SeqResult *r, bool atBoundary) {
    st->pending[0] = L'\0';
    while (*buf) {
        if (!atBoundary && HasLonger(sl, buf)) {          // 꼬리가 아직 자랄 수 있다
            lstrcpynW(st->pending, buf, SEQ_MAX_IN + 1);
            return;
        }
        const jdchar *v = NULL; int vn = 0, kn = 0;
        if (JDict_LongestPrefix(KeyDict(sl), buf, &kn, &v, &vn)) {
            wchar_t out[SEQ_MAX_OUT + 1];
            if (JDict_CopyValue(v, vn, out, SEQ_MAX_OUT + 1) >= 0) Deliver(st, sl, r, out);
            buf += kn;
        } else {
            wchar_t lit[2] = { buf[0], 0 };
            Deliver(st, sl, r, lit);                       // 사전에 없는 글자는 친 그대로
            buf++;
        }
    }
}

void SeqKb_Init(SeqState *st) {
    unsigned gen = st->generation;   // 세대는 초기화로 되돌리지 않는다 — 늦게 온 후보를 계속 걸러야 한다
    memset(st, 0, sizeof *st);
    st->generation = gen + 1;
}

const wchar_t *SeqKb_Reading(const SeqState *st) { return st ? st->reading : L""; }

// 사전이 낸 글자를 어디에 둘까: 후보 사전이 있으면 우리 소유 읽기, 없으면 바로 확정 (§6.4)
static void Deliver(SeqState *st, const SeqLayout *sl, SeqResult *r, const wchar_t *s) {
    if (!s || !s[0]) return;
    if (sl->zh && s[0] == SEQ_SEPARATOR && !st->reading[0]) s++;   // 쌍병 표는 음절 앞에 끊기를 붙인다 — 맨 앞의 것은 버린다
    if (!s[0]) return;
    if (!sl->cand) { Emit(r, s); return; }
    size_t have = wcslen(st->reading), add = wcslen(s);
    if (have + add > SEQ_MAX_READING) {   // 읽기가 꽉 차면 앞부분을 확정해 자리를 낸다
        Emit(r, st->reading);
        st->reading[0] = L'\0';
        have = 0;
        if (add > SEQ_MAX_READING) { Emit(r, s); return; }
    }
    wcscpy(st->reading + have, s);
    st->generation++;                     // 읽기가 바뀌었다 — 먼저 낸 후보는 이제 남의 것이다
}

// 지금 보여 줄 조합 문자열: 읽기 + 아직 사전을 못 만난 글자들
static void SeqComposing(const SeqState *st, SeqResult *r) {
    size_t n = wcslen(st->reading);
    if (n > sizeof(r->composing) / sizeof(r->composing[0]) - 1) n = sizeof(r->composing) / sizeof(r->composing[0]) - 1;
    wmemcpy(r->composing, st->reading, n);
    r->composing[n] = L'\0';
    lstrcpynW(r->composing + n, st->pending,
              (int)(sizeof(r->composing) / sizeof(r->composing[0]) - n));
}

// V 모드 (중국어 방식): 빈 읽기에서 v 로 시작하고 뒤에 숫자·식의 글자만 온 읽기
static bool IsVChar(wchar_t c) {
    return (c >= L'0' && c <= L'9') || c == L'.' || c == L'+' || c == L'-' || c == L'*' || c == L'/' || c == L'(' || c == L')';
}
bool SeqKb_IsVMode(const SeqLayout *sl, const wchar_t *reading) {
    if (!sl || !sl->zh || !reading || reading[0] != L'v') return false;
    for (const wchar_t *q = reading + 1; *q; q++) if (!IsVChar(*q)) return false;
    return true;
}

bool SeqKb_WouldEat(const SeqState *st, const SeqLayout *sl, wchar_t ch) {
    if (!SeqLayout_EnsureOpen((SeqLayout *)sl) || ch < 0x21 || ch > 0x7E) return false;   // 쓸 때 연다 (B22)
    if (sl->ja && st->noPunct && !st->pending[0] && SeqKb_IsJaPunctKey(ch)) return false;   // 일본어: 문장부호를 끈 자판
    if (sl->zh && ch == SEQ_SEPARATOR) return st->pending[0] || st->reading[0];   // 음절 끊기 (읽기 안에서만)
    if (sl->zh && !st->pending[0] && SeqKb_IsVMode(sl, st->reading) && IsVChar(ch)) return true;   // V 모드의 숫자·식
    if (sl->zh && ch == L'V' && sl->scheme > 0 && !st->pending[0] && !st->reading[0]) return true;  // 쌍병의 V 모드는 Shift+V
    if (st->pending[0]) return true;
    wchar_t one[2] = { ch, 0 };
    return JDict_HasLonger(KeyDict(sl), one) || JDict_Exact(KeyDict(sl), one, NULL, NULL);
}

SeqResult SeqKb_Key(SeqState *st, const SeqLayout *sl, wchar_t ch) {
    SeqResult r; memset(&r, 0, sizeof r);
    if (!SeqLayout_EnsureOpen((SeqLayout *)sl)) return r;
    if (ch < 0x21 || ch > 0x7E) {
        // 사이띄개·글쇠 아닌 문자는 엔진이 만지지 않는다 (단추·단축키·낱말 나누기를 응용에 맡긴다).
        // 보류가 있었으면 잃지 않도록 먼저 확정하고, 글쇠 자체는 응용으로 보낸다.
        if (st->pending[0]) { r = SeqKb_Flush(st, sl); r.eaten = false; }
        return r;
    }
    if (sl->zh && ch == SEQ_SEPARATOR) {   // 음절 끊기: 보류를 읽기로 정착시키고 읽기에 `'` 하나
        if (!st->pending[0] && !st->reading[0]) return r;   // 읽기 밖이면 문장부호다 (입력기가 바꾼다)
        if (st->pending[0]) {
            wchar_t pb[SEQ_MAX_IN + 2];
            lstrcpynW(pb, st->pending, SEQ_MAX_IN + 2);
            FlushBuf(st, sl, pb, &r, true);
        }
        size_t m = wcslen(st->reading);
        if (m > 0 && st->reading[m - 1] != SEQ_SEPARATOR && m < SEQ_MAX_READING) {
            st->reading[m] = SEQ_SEPARATOR; st->reading[m + 1] = L'\0';
            st->generation++;
        }
        r.eaten = true;
        SeqComposing(st, &r);
        return r;
    }
    if (sl->zh && !st->pending[0]
        && ((SeqKb_IsVMode(sl, st->reading) && IsVChar(ch)) || (ch == L'V' && sl->scheme > 0 && !st->reading[0]))) {
        size_t m = wcslen(st->reading);   // V 모드: 숫자·식은 읽기에 그대로 (쌍병의 Shift+V 는 v 로 시작)
        if (m < SEQ_MAX_READING) { st->reading[m] = (ch == L'V') ? L'v' : ch; st->reading[m + 1] = L'\0'; st->generation++; }
        r.eaten = true;
        SeqComposing(st, &r);
        return r;
    }
    if (!st->pending[0] && !SeqKb_WouldEat(st, sl, ch)) {   // 사전 밖의 글쇠 — 응용이 그대로 받는다
        r.eaten = false;
        return r;
    }
    wchar_t buf[SEQ_MAX_IN + 2];
    lstrcpynW(buf, st->pending, SEQ_MAX_IN + 2);
    size_t n = wcslen(buf);
    if (n + 1 < SEQ_MAX_IN + 2) { buf[n] = ch; buf[n + 1] = L'\0'; }
    r.eaten = true;
    wchar_t out[SEQ_MAX_OUT + 1];
    if (HasLonger(sl, buf)) {                      // 최장 일치를 기다린다
        lstrcpynW(st->pending, buf, SEQ_MAX_IN + 1);
    } else if (Exact(sl, buf, out, SEQ_MAX_OUT + 1)) {
        Deliver(st, sl, &r, out);
        st->pending[0] = L'\0';
    } else if (sl->onUnmatched == SEQ_UNMATCHED_CANCEL) {
        st->pending[0] = L'\0';                    // 오류 없이 취소 — 보류와 그 글쇠가 사라진다
    } else {
        FlushBuf(st, sl, buf, &r, false);          // 확정 + 한 번의 재처리
    }
    // 보류만 싣지 않는다 — **읽기까지** 보여야 한다. 읽기만 있고 보류가 없을 때 빈 문자열을
    // 돌려주면, 후보 사전을 쓰는 자판에서 친 글자가 화면 어디에도 안 보인다(실기 2026-09-24).
    SeqComposing(st, &r);
    return r;
}

SeqResult SeqKb_Symbol(SeqState *st, const SeqLayout *sl, const wchar_t *sym) {
    SeqResult r; memset(&r, 0, sizeof r);
    if (!SeqLayout_EnsureOpen((SeqLayout *)sl) || !sym) return r;
    for (const wchar_t *p = sym; *p; p++) {
        SeqResult one = SeqKb_Key(st, sl, *p);
        if (one.committed[0]) Emit(&r, one.committed);
        if (!one.eaten && !one.committed[0]) {
            // 엔진이 받지 않는 논리 입력은 그대로 찍는다 (글쇠였다면 응용이 받았을 자리다)
            wchar_t lit[2] = { *p, 0 };
            Emit(&r, lit);
        }
    }
    r.eaten = true;   // symbol 은 언제나 우리가 처리한다 (글쇠가 아니라 조합의 결과다)
    SeqComposing(st, &r);
    return r;
}

SeqResult SeqKb_Backspace(SeqState *st, const SeqLayout *sl) {
    (void)sl;
    SeqResult r; memset(&r, 0, sizeof r);
    size_t n = wcslen(st->pending);
    if (n > 0) { st->pending[n - 1] = L'\0'; r.eaten = true; SeqComposing(st, &r); return r; }
    size_t m = wcslen(st->reading);
    if (m > 0) {   // 우리 소유 읽기에서 한 글자 (확정된 남의 글자는 건드리지 않는다)
        st->reading[m - 1] = L'\0';
        st->generation++;
        st->candOpen = false;
        r.eaten = true;
        SeqComposing(st, &r);
        return r;
    }
    return r;
}

SeqResult SeqKb_Cancel(SeqState *st) {
    SeqResult r; memset(&r, 0, sizeof r);
    if (!st->pending[0] && !st->reading[0]) return r;
    st->pending[0] = L'\0';
    st->reading[0] = L'\0';
    st->generation++;      // 먼저 낸 후보는 이제 남의 것이다
    st->candOpen = false;
    r.eaten = true;
    return r;
}

SeqResult SeqKb_Flush(SeqState *st, const SeqLayout *sl) {
    SeqResult r; memset(&r, 0, sizeof r);
    if (!sl || !sl->dict || (!st->pending[0] && !st->reading[0])) return r;
    r.eaten = true;
    if (st->pending[0]) {
        wchar_t buf[SEQ_MAX_IN + 2];
        lstrcpynW(buf, st->pending, SEQ_MAX_IN + 2);
        FlushBuf(st, sl, buf, &r, true);
    }
    if (st->reading[0]) {   // 읽은 그대로 확정한다 — 고르지 않은 것을 대신 고르지 않는다
        // 문절 편집 중(일본어, 0.73.0)이면 화면에 보이는 바꾼 문장을 — 사용자가 보고 있던 것이 들어가야 한다
        Emit(&r, (st->conv[0] && st->convGen == st->generation) ? st->conv : st->reading);
        st->conv[0] = L'\0';
        st->reading[0] = L'\0';
        st->generation++;
        st->candOpen = false;
    }
    SeqComposing(st, &r);
    return r;
}

// ── 후보 (RFC-0016 §6.4) ───────────────────────────────────────────────────────────
// 변환 글쇠를 지금 받을 수 있는가. 읽기가 비어 있어도 **보류한 글자**가 있으면 받는다 —
// 보류를 먼저 읽기로 정착시키기 때문이다(아래 SeqKb_Convert).
bool SeqKb_CanConvert(const SeqState *st, const SeqLayout *sl) {
    return st && SeqLayout_EnsureOpen((SeqLayout *)sl) && sl->cand && (st->reading[0] || st->pending[0]);
}

// ── 읽기의 조각 (중국어 병음 방식: 음절 끊기 `'` 와 모음 없는 줄임) ─────────────────────
static bool IsVowel(wchar_t c) { return c == L'a' || c == L'e' || c == L'i' || c == L'o' || c == L'u' || c == L'v'; }
// 조각의 사전 키 (끊기를 뺀 것)와 끊기 수. 모음이 있으면 *vowel = true.
static int StripKey(const wchar_t *raw, int len, wchar_t *key, int cap, bool *vowel) {
    int k = 0, o = 0;
    if (vowel) *vowel = false;
    for (int i = 0; i < len && raw[i]; i++) {
        if (raw[i] == SEQ_SEPARATOR) { k++; continue; }
        if (vowel && IsVowel(raw[i])) *vowel = true;
        if (o + 1 < cap) key[o++] = raw[i];
    }
    key[o] = L'\0';
    return k;
}
// 후보의 글자 수 (서로게이트 쌍은 한 글자)
static int CandChars(const JDict *d, int index) {
    const jdchar *v = NULL; int vn = 0;
    if (!JDict_CandidateAt(d, index, &v, &vn)) return 0;
    wchar_t buf[SEQ_MAX_OUT + 1];
    if (JDict_CopyValue(v, vn, buf, SEQ_MAX_OUT + 1) < 0) return 0;
    int n = 0;
    for (const wchar_t *q = buf; *q; q++) if (!(*q >= 0xDC00 && *q <= 0xDFFF)) n++;
    return n;
}
// 읽기 조각 raw[0..len) 을 낱말 하나로 볼 수 있으면 그 사전의 범위와 글자 수 하한을 준다.
//   중국어 방식: 끊기로 시작·끝나지 않고, 끊기 k 개를 넘으면 글자가 k+1 개 이상이어야 하며, 모음 없는 조각은
//   읽기 전체가 모음 없을 때(allowBare)만 받는다.
static bool SegRange(const SeqLayout *sl, const wchar_t *raw, int len, bool allowBare,
                     int *first, int *count, int *minChars) {
    *minChars = 0;
    if (!sl->zh) {
        wchar_t seg[SEQ_MAX_READING + 1];
        lstrcpynW(seg, raw, len + 1);
        return JDict_Candidates(sl->cand, seg, first, count);
    }
    if (len <= 0 || raw[0] == SEQ_SEPARATOR || raw[len - 1] == SEQ_SEPARATOR) return false;
    wchar_t key[SEQ_MAX_READING + 1]; bool vowel = false;
    int k = StripKey(raw, len, key, SEQ_MAX_READING + 1, &vowel);
    if (!vowel && !allowBare) return false;
    if (!JDict_Candidates(sl->cand, key, first, count)) return false;
    *minChars = k ? k + 1 : 0;
    return true;
}
// 범위에서 글자 수 하한을 채우는 첫 후보 (앞의 32개까지 본다). 없으면 -1.
static int FirstFitting(const SeqLayout *sl, int first, int count, int minChars) {
    if (minChars <= 0) return count > 0 ? first : -1;
    for (int i = 0; i < count && i < 32; i++) if (CandChars(sl->cand, first + i) >= minChars) return first + i;
    return -1;
}
static bool ReadingBare(const SeqLayout *sl, const wchar_t *reading) {   // 읽기 전체에 모음이 없는가
    if (!sl->zh) return true;
    for (const wchar_t *q = reading; *q; q++) if (IsVowel(*q)) return false;
    return true;
}

// 일본어 방식 (0.70.0): mozc 의 비용은 연결 비용과 함께 쓰는 값이라, 낱말 비용만으로는 가나 그대로의 항목(비용 0 이
//   많다)이 앞서고 문장이 잘게 갈린다. 낱말마다 큰 벌점(JA_SEG_PENALTY)을 주고, 읽기와 같은 히라가나(두 글자 이상)·
//   가타카나 항목은 비싸게 본다 — 실제 사전으로 맞춘 값(日本語, 私は日本語を, 電車で会社に行く).
#define JA_SEG_PENALTY  4000
#define JA_HIRA_PENALTY 3000
#define JA_KATA_PENALTY 5000
static int JaKanaPenalty(const wchar_t *cand, const wchar_t *seg, int len) {
    if ((int)wcslen(cand) != len) return 0;
    bool hira = true, kata = true;
    for (int i = 0; i < len; i++) {
        wchar_t s = seg[i], c = cand[i];
        if (c != s) hira = false;
        if (!(c == s || (s >= 0x3041 && s <= 0x3096 && c == (wchar_t)(s + 0x60)))) kata = false;
    }
    if (hira) return len > 1 ? JA_HIRA_PENALTY : 0;
    return kata ? JA_KATA_PENALTY : 0;
}
// 일본어: 범위의 앞 12개 가운데 고친 비용이 가장 싼 후보
static int JaPick(const SeqLayout *sl, const wchar_t *seg, int len, int first, int count, int *costOut) {
    int best = -1, bestCost = 0x7FFFFFFF;
    for (int i = 0; i < count && i < 12; i++) {
        const jdchar *v = NULL; int vn = 0;
        wchar_t buf[SEQ_MAX_OUT + 1];
        if (!JDict_CandidateAt(sl->cand, first + i, &v, &vn) || JDict_CopyValue(v, vn, buf, SEQ_MAX_OUT + 1) < 0) break;
        int c = JDict_CostAt(sl->cand, first + i);
        if (c < 0) c = 0;
        c += JaKanaPenalty(buf, seg, len);
        if (c < bestCost) { bestCost = c; best = first + i; }
    }
    if (costOut) *costOut = bestCost;
    return best;
}
static int g_jaPickCost;   // 일본어: SegPick 이 고른 후보의 고친 비용 (입력 스레드 하나가 쓴다)

// 조각을 낱말 하나로 볼 때 쓸 후보(가장 싼 것). 모호음이면 비슷한 철자도 본다. 없으면 -1.
static int SegPick(const SeqLayout *sl, const wchar_t *raw, int len, bool bare, unsigned flags) {
    int first = 0, count = 0, minChars = 0, best = -1, bestCost = 0x7FFFFFFF;
    if (sl->ja) {
        if (!SegRange(sl, raw, len, bare, &first, &count, &minChars)) return -1;
        return JaPick(sl, raw, len, first, count, &g_jaPickCost);
    }
    if (SegRange(sl, raw, len, bare, &first, &count, &minChars)) {
        best = FirstFitting(sl, first, count, minChars);
        if (best >= 0) { bestCost = JDict_CostAt(sl->cand, best); if (bestCost < 0) bestCost = 0x7FFF0000; }
    }
    if (!(flags & SEQ_CONV_FUZZY) || !sl->zh || len <= 0 || raw[0] == SEQ_SEPARATOR || raw[len - 1] == SEQ_SEPARATOR) return best;
    wchar_t key[SEQ_MAX_READING + 1]; bool vowel = false;
    int k = StripKey(raw, len, key, SEQ_MAX_READING + 1, &vowel);
    if (!vowel && !bare) return best;
    wchar_t fk[16][SEQ_MAX_READING + 1];
    int nf = SeqKb_FuzzyKeys(key, fk, 16);
    for (int v = 1; v < nf; v++) {
        if (!JDict_Candidates(sl->cand, fk[v], &first, &count)) continue;
        int idx = FirstFitting(sl, first, count, k ? k + 1 : 0);
        if (idx < 0) continue;
        int c = JDict_CostAt(sl->cand, idx) + 50;   // 친 철자 그대로의 것이 조금 앞선다
        if (best < 0 || c < bestCost) { best = idx; bestCost = c; }
    }
    return best;
}

// ── 모호음: z/zh c/ch s/sh n/l (성모), an/ang en/eng in/ing (운모) ──────────────────────
//   음절을 가르지 않고 키 안의 자리마다 바꿔 본다 — 틀린 꼴은 사전에 없으니 걸러진다. 바꾼 자리가 적은 것부터, cap 개까지.
int SeqKb_FuzzyKeys(const wchar_t *key, wchar_t out[][SEQ_MAX_READING + 1], int cap) {
    if (cap <= 0) return 0;
    lstrcpynW(out[0], key, SEQ_MAX_READING + 1);
    int n = 1;
    // 바꿀 수 있는 자리: (위치, 지울 길이, 넣을 글자)
    struct { int at, del; const wchar_t *ins; } ch[24];
    int nc = 0;
    int len = (int)wcslen(key);
    for (int i = 0; i < len && nc < 24; i++) {
        wchar_t c = key[i], nx = i + 1 < len ? key[i + 1] : 0;
        if (c == L'z' || c == L'c' || c == L's') {          // 성모로만 쓰이는 글자
            if (nx == L'h') { ch[nc].at = i + 1; ch[nc].del = 1; ch[nc].ins = L""; nc++; }        // zh → z
            else if (IsVowel(nx)) { ch[nc].at = i + 1; ch[nc].del = 0; ch[nc].ins = L"h"; nc++; } // z → zh
        } else if ((c == L'n' || c == L'l') && IsVowel(nx)) {   // 뒤가 모음이면 성모 자리다
            ch[nc].at = i; ch[nc].del = 1; ch[nc].ins = c == L'n' ? L"l" : L"n"; nc++;
        }
        // 운모 an/en/in ↔ ang/eng/ing: 모음 다음의 n 이 음절 끝일 법한 자리 (뒤가 끝이거나 자음)
        if (c == L'n' && i > 0 && (key[i - 1] == L'a' || key[i - 1] == L'e' || key[i - 1] == L'i')) {
            if (nx == L'g' && (i + 2 >= len || !IsVowel(key[i + 2]))) { ch[nc].at = i + 1; ch[nc].del = 1; ch[nc].ins = L""; nc++; }   // ang → an
            else if (nx == 0 || (!IsVowel(nx) && nx != L'g')) { ch[nc].at = i + 1; ch[nc].del = 0; ch[nc].ins = L"g"; nc++; }     // an → ang
        }
    }
    // 한 자리 바꾼 것, 그다음 두 자리 (cap 까지)
    for (int depth = 1; depth <= 2 && n < cap; depth++) {
        for (int a = 0; a < nc && n < cap; a++) {
            for (int b = depth == 1 ? a : a + 1; b < (depth == 1 ? a + 1 : nc) && n < cap; b++) {
                wchar_t buf[SEQ_MAX_READING + 1]; int o = 0;
                for (int i = 0; i <= len && o < SEQ_MAX_READING; i++) {
                    for (int z = 0; z < 2; z++) {
                        int e = z == 0 ? a : b;
                        if (z == 1 && depth == 1) break;
                        if (ch[e].at == i) for (const wchar_t *q = ch[e].ins; *q && o < SEQ_MAX_READING; q++) buf[o++] = *q;
                    }
                    if (i == len) break;
                    bool skip = false;
                    for (int z = 0; z < 2; z++) {
                        int e = z == 0 ? a : b;
                        if (z == 1 && depth == 1) break;
                        if (ch[e].del && i >= ch[e].at && i < ch[e].at + ch[e].del) skip = true;
                    }
                    if (!skip) buf[o++] = key[i];
                }
                buf[o] = L'\0';
                bool dup = false;
                for (int k = 0; k < n; k++) if (!wcscmp(out[k], buf)) dup = true;
                if (!dup) lstrcpynW(out[n++], buf, SEQ_MAX_READING + 1);
            }
        }
    }
    return n;
}

// ── 사용자 구: 사용자 사전 폴더의 chinese-phrases.txt (키 + 공백·탭 + 구, # 주석, UTF-8) ─────────────
#define SEQ_PHRASES_MAX 4000
typedef struct SeqPhrases {
    FILETIME stamp;
    int      n;
    wchar_t  key[SEQ_PHRASES_MAX][24];
    wchar_t *val[SEQ_PHRASES_MAX];
} SeqPhrases;
static void PhrasesClear(SeqPhrases *p) { for (int i = 0; i < p->n; i++) free(p->val[i]); p->n = 0; }
static void PhrasesLoad(SeqPhrases *p, const wchar_t *path, bool anyKey) {
    PhrasesClear(p);
    FILE *f = _wfopen(path, L"rb");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof line, f) && p->n < SEQ_PHRASES_MAX) {
        char *q = line;
        if ((unsigned char)q[0] == 0xEF && (unsigned char)q[1] == 0xBB && (unsigned char)q[2] == 0xBF) q += 3;   // BOM
        size_t L = strlen(q);
        while (L && (q[L - 1] == '\n' || q[L - 1] == '\r')) q[--L] = 0;
        if (!q[0] || q[0] == '#') continue;
        char *sep = q;
        while (*sep && *sep != ' ' && *sep != '\t') sep++;
        if (!*sep || sep == q || sep - q >= 72) continue;
        *sep++ = 0;
        while (*sep == ' ' || *sep == '\t') sep++;
        if (!*sep) continue;
        bool ok = true;
        // 키: 중국어는 소문자 로마자, 일본어(anyKey)는 가나 읽기 (UTF-8 그대로, 23글자까지)
        for (char *k = q; *k && !anyKey; k++) if (!(*k >= 'a' && *k <= 'z')) ok = false;
        if (!ok) continue;
        wchar_t w[SEQ_MAX_OUT + 1], kw[24];
        int wn = MultiByteToWideChar(CP_UTF8, 0, sep, -1, w, SEQ_MAX_OUT + 1);
        if (wn <= 1) continue;
        int kn = MultiByteToWideChar(CP_UTF8, 0, q, -1, kw, 24);
        if (kn <= 1) continue;
        lstrcpynW(p->key[p->n], kw, 24);
        p->val[p->n] = _wcsdup(w);
        if (p->val[p->n]) p->n++;
    }
    fclose(f);
}
static wchar_t g_phrasesPath[MAX_PATH];   // 시험이 정한 자리 (비면 사용자 사전 폴더)
void SeqKb_SetPhrasesPath(const wchar_t *path) { if (path) lstrcpynW(g_phrasesPath, path, MAX_PATH); else g_phrasesPath[0] = 0; }
static int PhrasesFor(SeqLayout *sl, const wchar_t *key, wchar_t items[][SEQ_MAX_OUT + 1], int cap) {
    if (!sl->phrases) {
        sl->phrases = (SeqPhrases *)calloc(1, sizeof(SeqPhrases));
        if (!sl->phrases) return 0;
    }
    SeqPhrases *p = sl->phrases;
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    bool have = false;
    if (g_phrasesPath[0]) { lstrcpynW(path, g_phrasesPath, MAX_PATH); have = true; }
    else if (Config_UserDictDir(dir, MAX_PATH)) {
        _snwprintf(path, MAX_PATH, L"%ls\\%ls", dir, sl->ja ? SEQ_JA_PHRASES_FILE : SEQ_PHRASES_FILE);
        path[MAX_PATH - 1] = 0;
        have = true;
    }
    if (have) {   // 파일이 바뀌었으면 다시 읽는다 (설정의 "Edit custom phrases" 로 고친 뒤)
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) {
            if (fad.ftLastWriteTime.dwLowDateTime != p->stamp.dwLowDateTime || fad.ftLastWriteTime.dwHighDateTime != p->stamp.dwHighDateTime) {
                PhrasesLoad(p, path, sl->ja != 0);
                p->stamp = fad.ftLastWriteTime;
            }
        } else if (p->n) { PhrasesClear(p); memset(&p->stamp, 0, sizeof p->stamp); }
    }
    int got = 0;
    for (int i = 0; i < p->n && got < cap; i++)
        if (!wcscmp(p->key[i], key)) lstrcpynW(items[got++], p->val[i], SEQ_MAX_OUT + 1);
    return got;
}

int SeqLayout_SelectScheme(SeqLayout *sl, const wchar_t *name) {
    if (!sl) return 0;
    sl->scheme = 0;
    if (name && name[0])
        for (int i = 0; i < sl->nScheme; i++) if (!wcscmp(sl->schemeName[i], name)) { sl->scheme = i + 1; break; }   // 이름으로 (쓸 때 열기 — 표는 아직 안 열렸을 수 있다)
    return sl->scheme;
}

bool SeqKb_IsEmoji(const wchar_t *c) {
    if (!c || !c[0]) return false;
    unsigned u = c[0];
    if ((u >= 0xD800 && u <= 0xDBFF) || u >= 0x10000) return true;    // BMP 밖 (이모지 대부분 — 서로게이트 쌍이든 한 글자든)
    return (u >= 0x2190 && u <= 0x2BFF) || (u >= 0x2300 && u <= 0x23FF) || u == 0x00A9 || u == 0x00AE
        || u == 0x203C || u == 0x2049 || u == 0x2122 || u == 0x2139 || (u >= 0x3297 && u <= 0x3299);   // 화살표·기호·딩뱃
}
bool SeqLayout_ToneOf(const SeqLayout *sl, const wchar_t *cand, wchar_t *out, int cap) {
    if (!sl || !sl->tones || !cand || !cand[0]) return false;
    int first = 0, count = 0;
    const jdchar *v = NULL; int vn = 0;
    if (!JDict_Candidates(sl->tones, cand, &first, &count) || !JDict_CandidateAt(sl->tones, first, &v, &vn)) return false;
    return JDict_CopyValue(v, vn, out, cap) >= 0;
}
static bool g_noEmoji;   // 이번 변환이 이모지를 빼는가 (SeqKb_ConvertEx 가 정한다 — 입력 스레드 하나가 쓴다)
static const wchar_t *g_jaReading;   // 일본어: 읽기 그대로의 가나 항목은 목록에서 빼고 정한 자리에 둔다 (NULL = 아님)

// 후보 하나를 붙인다 — 이미 있는 글자와 같으면 붙이지 않는다(문장 후보와 낱말 후보가 같은 때 등).
static bool AddCand(SeqCandidates *out, const JDict *d, int index, int consumed) {
    if (out->count >= SEQ_MAX_CANDS) return false;
    const jdchar *v = NULL; int vn = 0;
    if (!JDict_CandidateAt(d, index, &v, &vn)) return false;
    if (JDict_CopyValue(v, vn, out->items[out->count], SEQ_MAX_OUT + 1) < 0) return false;
    if (g_noEmoji && SeqKb_IsEmoji(out->items[out->count])) return false;
    if (g_jaReading && JaKanaPenalty(out->items[out->count], g_jaReading, (int)wcslen(g_jaReading))) return false;
    for (int j = 0; j < out->count; j++) if (!wcscmp(out->items[j], out->items[out->count])) return false;
    out->consumed[out->count++] = consumed;
    return true;
}
// 읽기 전체의 후보를 *next 부터 upto 번째 전까지 (n = 읽기 길이)
static void AddWhole(SeqCandidates *out, const SeqLayout *sl, int first, int count, int *next, int upto, int n, int minChars) {
    for (; *next < count && out->count < SEQ_MAX_CANDS; (*next)++) {
        if (minChars > 0 && CandChars(sl->cand, first + *next) < minChars) { upto++; continue; }   // 끊기를 못 채운다 — 세지 않는다
        if (*next >= upto) break;
        AddCand(out, sl->cand, first + *next, n);
    }
}
// 추천 단어를 *next 부터 upto 번째 전까지
static void AddPredict(SeqCandidates *out, const SeqLayout *sl, const int *idx, int np, int *next, int upto, int n, int minChars) {
    for (; *next < np && out->count < SEQ_MAX_CANDS; (*next)++) {
        if (minChars > 0 && CandChars(sl->cand, idx[*next]) < minChars) { upto++; continue; }   // 끊기를 못 채운다
        if (*next >= upto) break;
        AddCand(out, sl->cand, idx[*next], n);
    }
}

// ── 일본어 래티스 (RFC-0022) ─────────────────────────────────────────────────────────────
//   끝 자리마다 가장 싼 노드 JA_BEAM 개. 노드 = (지금까지 비용, 오른쪽 품사, 시작, 앞 노드, 사전 자리). 낱말은 JA_MAXSEG 글자까지,
//   한 조각에서 앞의 JA_SEG_CANDS 개(싼 차례)만 본다. 사전에 없는 한 글자는 일반 명사(Mozc id 1851)로 비싸게 지나간다.
//   실제 사전으로 맞춘 값: 50문장에서 빔 10·1바이트 연결표로도 무제한과 같다(tools/ja/ja_lattice_proto.py).
#define JA_BEAM       10
#define JA_MAXSEG     16
#define JA_SEG_CANDS  48
#define JA_UNK_ID     1851
#define JA_UNK_COST   10000
typedef struct { int cost, rid, lid, start, prev, idx; } JaNode;
static JaNode g_jaNodes[(SEQ_MAX_READING + 1) * JA_BEAM];   // 입력 스레드 하나가 쓴다
static int g_jaCount[SEQ_MAX_READING + 1];
static void JaPut(int j, int cost, int lid, int rid, int start, int prev, int idx) {
    JaNode *b = &g_jaNodes[j * JA_BEAM];
    int k = g_jaCount[j];
    if (k < JA_BEAM) { g_jaCount[j] = k + 1; }
    else {
        int w = 0;
        for (int i = 1; i < JA_BEAM; i++) if (b[i].cost > b[w].cost) w = i;
        if (cost >= b[w].cost) return;
        k = w;
    }
    b[k].cost = cost; b[k].lid = lid; b[k].rid = rid; b[k].start = start; b[k].prev = prev; b[k].idx = idx;
}
// 가장 싼 길의 낱말들 (앞에서부터). leftRid = 앞 문맥의 오른쪽 품사(문장 앞이면 0 = BOS). 낱말 수, 못 가르면 0.
typedef struct { int start, end, idx, lid, rid; } JaWord;   // idx < 0 = 사전에 없는 한 글자
static int JaLatticeWords(const SeqLayout *sl, const wchar_t *r, int n, int leftRid, JaWord *words, int cap) {
    if (n <= 0 || n > SEQ_MAX_READING) return 0;
    for (int j = 0; j <= n; j++) g_jaCount[j] = 0;
    JaPut(0, 0, 0, leftRid, 0, -1, -2);                       // 문장 앞 (BOS, id 0) 또는 앞 문맥
    for (int i = 0; i < n; i++) {
        if (!g_jaCount[i]) continue;
        for (int j = i + 1; j <= n && j - i <= JA_MAXSEG; j++) {
            wchar_t seg[JA_MAXSEG + 1];
            wmemcpy(seg, r + i, (size_t)(j - i)); seg[j - i] = 0;
            int first = 0, count = 0;
            bool have = JDict_Candidates(sl->cand, seg, &first, &count);
            if (!have && j != i + 1) continue;
            int m = have ? (count < JA_SEG_CANDS ? count : JA_SEG_CANDS) : 1;
            for (int e = 0; e < m; e++) {
                int lid = JA_UNK_ID, rid = JA_UNK_ID, c = JA_UNK_COST, idx = -1;
                if (have) {
                    idx = first + e;
                    if (!JDict_PosAt(sl->cand, idx, &lid, &rid)) continue;
                    c = JDict_CostAt(sl->cand, idx);
                    if (c < 0) c = JA_UNK_COST;
                }
                int best = -1, bestCost = 0x3FFFFFFF;
                for (int k = 0; k < g_jaCount[i]; k++) {
                    const JaNode *p = &g_jaNodes[i * JA_BEAM + k];
                    int t = p->cost + JConn_Cost(sl->conn, p->rid, lid) + c;
                    if (t < bestCost) { bestCost = t; best = i * JA_BEAM + k; }
                }
                if (best >= 0) JaPut(j, bestCost, lid, rid, i, best, idx);
            }
        }
    }
    int end = -1, endCost = 0x7FFFFFFF;                       // 문장 끝 (EOS, id 0)
    for (int k = 0; k < g_jaCount[n]; k++) {
        const JaNode *p = &g_jaNodes[n * JA_BEAM + k];
        int t = p->cost + JConn_Cost(sl->conn, p->rid, 0);
        if (t < endCost) { endCost = t; end = n * JA_BEAM + k; }
    }
    if (end < 0) return 0;
    int chain[SEQ_MAX_READING + 1], nc = 0;
    for (int x = end; x >= 0 && g_jaNodes[x].idx != -2 && nc <= SEQ_MAX_READING; x = g_jaNodes[x].prev) chain[nc++] = x;
    if (nc == 0 || nc > cap) return 0;
    for (int k = nc - 1, w = 0; k >= 0; k--, w++) {
        const JaNode *p = &g_jaNodes[chain[k]];
        words[w].start = p->start; words[w].end = chain[k] / JA_BEAM;
        words[w].idx = p->idx; words[w].lid = p->lid; words[w].rid = p->rid;
    }
    return words[nc - 1].end == n ? nc : 0;
}
// 그 낱말의 표기 (사전에 없는 글자는 읽기 그대로)
static bool JaWordText(const SeqLayout *sl, const wchar_t *r, const JaWord *w, wchar_t *val) {
    if (w->idx >= 0) {
        const jdchar *v = NULL; int vn = 0;
        return JDict_CandidateAt(sl->cand, w->idx, &v, &vn) && JDict_CopyValue(v, vn, val, SEQ_MAX_OUT + 1) >= 0;
    }
    int len = w->end - w->start;
    if (len > SEQ_MAX_OUT) return false;
    wmemcpy(val, r + w->start, (size_t)len); val[len] = 0;
    return true;
}
static bool JaLattice(const SeqLayout *sl, const wchar_t *r, int n, wchar_t *out, int *firstLen, int *segs) {
    JaWord words[SEQ_MAX_READING + 1];
    int nc = JaLatticeWords(sl, r, n, 0, words, SEQ_MAX_READING + 1);
    if (nc == 0) return false;
    out[0] = 0;
    for (int k = 0; k < nc; k++) {
        wchar_t val[SEQ_MAX_OUT + 1];
        if (!JaWordText(sl, r, &words[k], val)) return false;
        size_t have = wcslen(out), add = wcslen(val);
        if (have + add > SEQ_MAX_OUT) return false;
        wcscpy(out + have, val);
    }
    *firstLen = words[0].end < n ? words[0].end : 0;
    *segs = nc;
    return out[0] != 0;
}

// ── 문절 편집 (0.73.0, RFC-0022 P2) ──────────────────────────────────────────────────────
//   래티스의 낱말을 문절로 묶는다: 조사·조동사·비자립 낱말·접미는 앞 낱말에, 접두사 뒤의 낱말은 그 접두사에 붙는다
//   (.jdc 판 2 의 품사 종류). 종류가 없는 연결 표면 히라가나뿐인 낱말을 앞에 붙인다. 사전에 없는 글자는 이어서 한 문절.
static bool JaAllHiragana(const wchar_t *s) {
    if (!s[0]) return false;
    for (; *s; s++) if (!((*s >= 0x3041 && *s <= 0x309F) || *s == 0x30FC)) return false;
    return true;
}
static bool JaAttaches(const SeqLayout *sl, const JaWord *prev, const JaWord *w, const wchar_t *text) {
    if (prev->idx < 0 && w->idx < 0) return true;
    if (w->idx < 0) return false;
    if (JConn_HasClass(sl->conn))
        return (JConn_Class(sl->conn, w->lid) & JCONN_CLASS_FUNC) || (prev->idx >= 0 && (JConn_Class(sl->conn, prev->rid) & JCONN_CLASS_PREFIX));
    return JaAllHiragana(text);
}
// r[pos..n) 를 갈라 문절 at 부터 채운다 (leftRid = 앞 문절의 오른쪽 품사). 못 가르면 false.
static bool JaFillFrom(const SeqLayout *sl, const wchar_t *r, int n, SeqSegments *sg, int at, int pos, int leftRid) {
    sg->count = at;
    if (pos >= n) return at > 0;
    JaWord words[SEQ_MAX_READING + 1];
    int nw = JaLatticeWords(sl, r + pos, n - pos, leftRid, words, SEQ_MAX_READING + 1);
    if (nw == 0) return false;
    for (int k = 0; k < nw; k++) {
        wchar_t val[SEQ_MAX_OUT + 1];
        if (!JaWordText(sl, r + pos, &words[k], val)) return false;
        bool attach = sg->count > at && (JaAttaches(sl, &words[k - 1], &words[k], val) || sg->count >= SEQ_MAX_SEGS);
        if (attach && wcslen(sg->text[sg->count - 1]) + wcslen(val) > SEQ_MAX_OUT) attach = false;
        if (!attach) {
            if (sg->count >= SEQ_MAX_SEGS) return false;
            int i = sg->count++;
            sg->start[i] = pos + words[k].start; sg->len[i] = 0; sg->text[i][0] = 0;
        }
        int i = sg->count - 1;
        sg->len[i] += words[k].end - words[k].start;
        sg->rid[i] = words[k].rid;
        wcscat(sg->text[i], val);
    }
    return true;
}
bool SeqKb_JaSegments(SeqState *st, const SeqLayout *sl, SeqSegments *out) {
    if (!st || !out || !SeqLayout_EnsureOpen((SeqLayout *)sl) || !sl->ja || !sl->cand || !sl->conn || !JDict_HasPos(sl->cand)) return false;
    SeqState before = *st;
    if (st->pending[0]) {   // 보류를 먼저 읽기로 (SeqKb_ConvertEx 와 같다)
        wchar_t buf[SEQ_MAX_IN + 2];
        lstrcpynW(buf, st->pending, SEQ_MAX_IN + 2);
        SeqResult tmp; memset(&tmp, 0, sizeof tmp);
        FlushBuf(st, sl, buf, &tmp, true);
    }
    const int n = (int)wcslen(st->reading);
    wchar_t ph[1][SEQ_MAX_OUT + 1];
    memset(out, 0, sizeof *out);
    if (n == 0 || PhrasesFor((SeqLayout *)sl, st->reading, ph, 1) > 0   // 문구 치환이 있는 읽기는 예전 목록으로
        || !JaFillFrom(sl, st->reading, n, out, 0, 0, 0) || out->count < 2) { *st = before; return false; }
    out->generation = st->generation;
    out->focus = 0;
    st->candOpen = true;   // 후보를 내놓은 상태 — 접으면(SeqKb_CancelCandidates) 읽기로 돌아간다
    return true;
}
bool SeqKb_JaSegResize(const SeqState *st, const SeqLayout *sl, SeqSegments *sg, int delta) {
    if (!st || !sl || !sg || sg->generation != st->generation || sg->focus < 0 || sg->focus >= sg->count) return false;
    const int n = (int)wcslen(st->reading), f = sg->focus;
    int len = sg->len[f] + delta;
    if (len < 1 || sg->start[f] + len > n || len == sg->len[f]) return false;
    static SeqSegments t;   // 크다 — 입력 스레드 하나가 쓴다
    t = *sg;
    // 정한 구간은 그 안에서만 다시 가른 것을 한 문절로, 그 뒤는 새로 가른다. 앞 문절들은 그대로.
    JaWord words[SEQ_MAX_READING + 1];
    int leftRid = f > 0 ? t.rid[f - 1] : 0;
    int nw = JaLatticeWords(sl, st->reading + t.start[f], len, leftRid, words, SEQ_MAX_READING + 1);
    if (nw == 0) return false;
    t.len[f] = len; t.text[f][0] = 0;
    for (int k = 0; k < nw; k++) {
        wchar_t val[SEQ_MAX_OUT + 1];
        if (!JaWordText(sl, st->reading + t.start[f], &words[k], val) || wcslen(t.text[f]) + wcslen(val) > SEQ_MAX_OUT) return false;
        wcscat(t.text[f], val);
        t.rid[f] = words[k].rid;
    }
    if (t.start[f] + len >= n) t.count = f + 1;
    else if (!JaFillFrom(sl, st->reading, n, &t, f + 1, t.start[f] + len, t.rid[f])) return false;
    *sg = t;
    return true;
}
bool SeqKb_JaSegMove(SeqSegments *sg, int delta) {
    if (!sg || sg->count <= 0) return false;
    int f = sg->focus + delta;
    if (f < 0 || f >= sg->count) return false;
    sg->focus = f;
    return true;
}
static void JaSegAdd(SeqCandidates *out, const wchar_t *s, int len) {
    if (out->count >= SEQ_MAX_CANDS || !s[0] || wcslen(s) > SEQ_MAX_OUT) return;
    for (int j = 0; j < out->count; j++) if (!wcscmp(out->items[j], s)) return;
    wcscpy(out->items[out->count], s);
    out->consumed[out->count++] = len;
}
bool SeqKb_JaSegCands(const SeqState *st, const SeqLayout *sl, const SeqSegments *sg, SeqCandidates *out) {
    if (!st || !sl || !sg || !out || sg->generation != st->generation || sg->focus < 0 || sg->focus >= sg->count) return false;
    const int f = sg->focus, len = sg->len[f];
    wchar_t span[SEQ_MAX_READING + 1];
    wmemcpy(span, st->reading + sg->start[f], (size_t)len); span[len] = 0;
    memset(out, 0, sizeof *out);
    out->generation = st->generation;
    JaSegAdd(out, sg->text[f], len);                           // 지금 것이 맨 앞
    // 구간 전체가 낱말인 것: 낱말 비용 + 앞 문절과의 연결 비용이 싼 차례
    int first = 0, count = 0;
    if (len <= JA_MAXSEG && JDict_Candidates(sl->cand, span, &first, &count)) {
        enum { TOP = 40 };
        int pick[TOP], cost[TOP], np = 0;
        int leftRid = f > 0 ? sg->rid[f - 1] : 0;
        for (int e = 0; e < count && e < 400; e++) {
            int lid = 0, rid = 0, c = JDict_CostAt(sl->cand, first + e);
            if (!JDict_PosAt(sl->cand, first + e, &lid, &rid)) continue;
            c = (c < 0 ? JA_UNK_COST : c) + JConn_Cost(sl->conn, leftRid, lid);
            int k;
            if (np < TOP) k = np++;
            else if (c < cost[TOP - 1]) k = TOP - 1;   // 가장 비싼 것을 밀어낸다
            else continue;
            while (k > 0 && cost[k - 1] > c) { pick[k] = pick[k - 1]; cost[k] = cost[k - 1]; k--; }
            pick[k] = first + e; cost[k] = c;
        }
        for (int k = 0; k < np; k++) {
            wchar_t val[SEQ_MAX_OUT + 1];
            const jdchar *v = NULL; int vn = 0;
            if (JDict_CandidateAt(sl->cand, pick[k], &v, &vn) && JDict_CopyValue(v, vn, val, SEQ_MAX_OUT + 1) >= 0) JaSegAdd(out, val, len);
        }
    }
    // 낱말 여럿인 문절: 첫 낱말(뜻을 지는 말)만 바꾸고 뒤(조사·어미)는 그대로
    JaWord words[SEQ_MAX_READING + 1];
    int nw = JaLatticeWords(sl, span, len, f > 0 ? sg->rid[f - 1] : 0, words, SEQ_MAX_READING + 1);
    if (nw >= 2) {
        wchar_t tail[SEQ_MAX_OUT + 1] = L"", head[JA_MAXSEG + 1];
        bool ok = true;
        for (int k = 1; k < nw && ok; k++) {
            wchar_t val[SEQ_MAX_OUT + 1];
            ok = JaWordText(sl, span, &words[k], val) && wcslen(tail) + wcslen(val) <= SEQ_MAX_OUT;
            if (ok) wcscat(tail, val);
        }
        int hl = words[0].end;
        if (ok && hl <= JA_MAXSEG) {
            wmemcpy(head, span, (size_t)hl); head[hl] = 0;
            if (JDict_Candidates(sl->cand, head, &first, &count))
                for (int e = 0; e < count && e < 24; e++) {
                    wchar_t val[SEQ_MAX_OUT * 2 + 2];
                    const jdchar *v = NULL; int vn = 0;
                    if (!JDict_CandidateAt(sl->cand, first + e, &v, &vn) || JDict_CopyValue(v, vn, val, SEQ_MAX_OUT + 1) < 0) continue;
                    wcscat(val, tail);
                    JaSegAdd(out, val, len);
                }
        }
    }
    for (int form = SEQ_KANA_HIRAGANA; form <= SEQ_KANA_KATAKANA; form++) {   // 히라가나·가타카나 그대로
        wchar_t kf[SEQ_MAX_OUT + 1];
        if (SeqKb_KanaForm(span, form, kf, SEQ_MAX_OUT + 1)) JaSegAdd(out, kf, len);
    }
    return out->count > 0;
}
bool SeqKb_JaSegChoose(const SeqState *st, SeqSegments *sg, const SeqCandidates *cands, int index) {
    if (!st || !sg || !cands || sg->generation != st->generation || cands->generation != st->generation
        || index < 0 || index >= cands->count || sg->focus < 0 || sg->focus >= sg->count) return false;
    lstrcpynW(sg->text[sg->focus], cands->items[index], SEQ_MAX_OUT + 1);
    return true;
}
void SeqKb_JaSegText(const SeqSegments *sg, bool mark, wchar_t *out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = 0;
    if (!sg) return;
    int k = 0;
    for (int i = 0; i < sg->count; i++) {
        bool f = mark && i == sg->focus;
        if (f && k + 1 < cap) out[k++] = L'[';
        for (const wchar_t *p = sg->text[i]; *p && k + 1 < cap; p++) out[k++] = *p;
        if (f && k + 1 < cap) out[k++] = L']';
    }
    out[k] = 0;
}
void SeqKb_JaSegSync(SeqState *st, const SeqSegments *sg) {
    if (!st) return;
    st->conv[0] = 0;
    if (!sg || sg->generation != st->generation) return;
    SeqKb_JaSegText(sg, false, st->conv, (int)(sizeof st->conv / sizeof st->conv[0]));
    st->convGen = st->generation;
}

bool SeqKb_Convert(SeqState *st, const SeqLayout *sl, SeqCandidates *out) {
    return SeqKb_ConvertEx(st, sl, SEQ_CONV_ALL, out);
}

bool SeqKb_ConvertEx(SeqState *st, const SeqLayout *sl, unsigned flags, SeqCandidates *out) {
    if (!st || !SeqLayout_EnsureOpen((SeqLayout *)sl) || !sl->cand || !out) return false;
    // 후보가 없으면 **아무것도 바꾸지 않는다**. 바꿔 놓고 실패하면 그 글쇠(사이띄개)는 응용으로
    // 가는데 보류는 이미 읽기로 옮겨져, 사이띄개가 친 글자보다 먼저 문서에 들어간다.
    SeqState before = *st;
    if ((flags & SEQ_CONV_LIVE) && !st->reading[0]) return false;   // 치는 동안: 보류만 있으면 아직 후보가 없다
    // 보류한 글자를 먼저 읽기로 정착시킨다. `nihon` 의 끝 `n` 처럼 **더 자랄 수 있는 항목**은
    // 변환 글쇠를 누른 순간 경계로 보고 확정해야 한다 — 아니면 `にほん` 이 영영 서지 않는다.
    if (st->pending[0] && !(flags & SEQ_CONV_LIVE)) {   // 치는 동안에는 쌍병의 반쯤 친 음절을 그대로 둔다
        wchar_t buf[SEQ_MAX_IN + 2];
        lstrcpynW(buf, st->pending, SEQ_MAX_IN + 2);
        SeqResult tmp; memset(&tmp, 0, sizeof tmp);
        FlushBuf(st, sl, buf, &tmp, true);
        // 읽기에 못 들어가고 밖으로 나간 글자가 있으면(읽기가 꽉 찼을 때) 그건 이미 문서의 것이다.
    }
    if (!st->reading[0]) { *st = before; return false; }
    // 이어 치기 변환 (2026-10-02, 중국어 병음의 문장):
    //   1) 읽기를 사전의 낱말로 가르는 **가장 그럴듯한 길**을 찾는다 — 낱말 비용(판 3 사전의 항목 비용, 작을수록
    //      흔하다)의 합에 낱말마다 작은 벌점. 비용이 없는 사전은 낱말 수만 세므로 가장 긴 일치와 비슷하게 간다.
    //   2) 그 길이 낱말 둘 이상이면 **문장 하나**(각 낱말의 첫 후보를 이은 것)를 첫 후보로 낸다.
    //   3) 그다음 읽기 전체의 후보, 길의 첫 낱말의 후보, 나머지 앞부분의 후보를 긴 것부터.
    //   앞부분만 쓰는 후보를 고르면 나머지는 읽기에 남는다(SeqKb_Choose) — 입력기가 이어서 다시 연다.
    memset(out, 0, sizeof *out);
    out->generation = st->generation;
    g_noEmoji = (flags & SEQ_CONV_NOEMOJI) != 0;
    g_jaReading = sl->ja ? st->reading : NULL;
    const int n = (int)wcslen(st->reading);
    const bool bare = ReadingBare(sl, st->reading);   // 첫 글자 줄임만 친 읽기 (zg, wsm)
    if (sl->zh && SeqKb_IsVMode(sl, st->reading)) {     // V 모드: 숫자·식만 — 사전은 보지 않는다
        out->count = SeqKb_VModeCands(sl, st->reading, out->items, SEQ_MAX_CANDS);
        for (int i = 0; i < out->count; i++) out->consumed[i] = n;
        if (out->count == 0) { *st = before; return false; }
        st->candOpen = true;
        return true;
    }
    // 0) 날짜·시간 (중국어 방식의 rq·sj·xq) — 맨 앞
    if (sl->zh) {
        SYSTEMTIME now; GetLocalTime(&now);
        wchar_t dt[8][SEQ_MAX_OUT + 1];
        int nd = SeqKb_DateTimeCands(sl, st->reading, &now, dt, 8);
        for (int i = 0; i < nd && out->count < SEQ_MAX_CANDS; i++) {
            lstrcpynW(out->items[out->count], dt[i], SEQ_MAX_OUT + 1);
            out->consumed[out->count++] = n;
        }
    }
    if (sl->ja) {   // 문구 치환 (일본어): 읽기와 꼭 같은 키, 맨 앞 쪽에
        out->count += PhrasesFor((SeqLayout *)sl, st->reading, out->items + out->count, SEQ_MAX_CANDS - out->count);
        for (int i = 0; i < out->count; i++) out->consumed[i] = n;
    }
    if (sl->zh) {   // 사용자 구 — 읽기(끊기를 뺀 것)와 꼭 같은 키, 맨 앞 쪽에
        wchar_t key[SEQ_MAX_READING + 1];
        StripKey(st->reading, n, key, SEQ_MAX_READING + 1, NULL);
        out->count += PhrasesFor((SeqLayout *)sl, key, out->items + out->count, SEQ_MAX_CANDS - out->count);
        for (int i = 0; i < out->count; i++) out->consumed[i] = n;
    }
    int firstSegLen = 0;   // 가장 그럴듯한 길의 첫 낱말이 쓰는 읽기 길이 (길이 없으면 0)
    if (sl->ja && sl->conn && JDict_HasPos(sl->cand)) {
        // 일본어 + 연결 비용 (RFC-0022): Mozc 처럼 품사 사이의 비용까지 더한 래티스. 길이 낱말 하나여도 그 낱말을 맨 앞에 —
        //   읽기 전체의 후보 차례(낱말 비용만)보다 문맥(문장 앞뒤)을 본 고름이 낫다.
        wchar_t sent[SEQ_MAX_OUT + 1];
        int segs = 0;
        if (JaLattice(sl, st->reading, n, sent, &firstSegLen, &segs)
            && (segs == 1 || (flags & SEQ_CONV_SENTENCE)) && out->count < SEQ_MAX_CANDS) {
            bool dup = false;
            for (int i = 0; i < out->count; i++) if (!wcscmp(out->items[i], sent)) dup = true;
            if (!dup) { lstrcpynW(out->items[out->count], sent, SEQ_MAX_OUT + 1); out->consumed[out->count++] = n; }
        }
    } else {
        enum { SEG_PENALTY = 100, NOCOST_SEG = 1000, INF = 0x3FFFFFFF };
        int best[SEQ_MAX_READING + 1], back[SEQ_MAX_READING + 1], pick[SEQ_MAX_READING + 1];
        best[0] = 0;
        for (int j = 1; j <= n; j++) {
            best[j] = INF; back[j] = -1; pick[j] = -1;
            if (sl->zh && st->reading[j - 1] == SEQ_SEPARATOR) {   // 끊기는 값 없이 건넌다
                best[j] = best[j - 1]; back[j] = j - 1; pick[j] = -2;
                continue;
            }
            for (int i = 0; i < j; i++) {
                if (best[i] >= INF) continue;
                int idx = SegPick(sl, st->reading + i, j - i, bare, flags);
                if (idx < 0) continue;
                int c = sl->ja ? g_jaPickCost : JDict_CostAt(sl->cand, idx);
                if (c < 0) c = NOCOST_SEG;
                int total = best[i] + c + (sl->ja ? JA_SEG_PENALTY : SEG_PENALTY);
                if (total < best[j]) { best[j] = total; back[j] = i; pick[j] = idx; }
            }
        }
        if (best[n] < INF) {
            int cutEnd[SEQ_MAX_READING + 1], cutIdx[SEQ_MAX_READING + 1], nc = 0;
            for (int j = n; j > 0; j = back[j]) if (pick[j] != -2) { cutEnd[nc] = j; cutIdx[nc] = pick[j]; nc++; }
            if (nc > 0) firstSegLen = cutEnd[nc - 1];
            if (nc >= 2 && (flags & SEQ_CONV_SENTENCE) && out->count < SEQ_MAX_CANDS) {   // 2) 문장 후보
                wchar_t *sent = out->items[out->count];
                sent[0] = L'\0';
                bool fits = true;
                for (int k = nc - 1; k >= 0 && fits; k--) {
                    wchar_t val[SEQ_MAX_OUT + 1];
                    const jdchar *v = NULL; int vn = 0;
                    fits = JDict_CandidateAt(sl->cand, cutIdx[k], &v, &vn)
                           && JDict_CopyValue(v, vn, val, SEQ_MAX_OUT + 1) >= 0
                           && wcslen(sent) + wcslen(val) <= SEQ_MAX_OUT;
                    if (fits) wcscat(sent, val);
                }
                if (fits && sent[0]) out->consumed[out->count++] = n;
            }
        }
    }
    // 3) 그다음: 읽기 전체의 후보 앞의 몇 개 → 추천 단어 몇 개(첫 쪽에 보이게) → 읽기 전체의 나머지 → 남은 추천 단어
    //    → 길의 첫 낱말 → 나머지 앞부분을 긴 것부터.
    //   추천 단어(2026-10-02)는 읽기로 **시작하는** 더 긴 낱말·성어를 싼 것부터 — 고르면 읽기 전체가 그 낱말이 된다
    //   (`yijian` → 一箭双雕, 사전에 줄임 키가 있으면 `wsm` → 为什么). 읽기 그대로의 후보가 많아도(병음 한 음절은 수백)
    //   첫 쪽에 들도록 사이에 끼운다. 중국어 방식에서 끊기가 든 읽기는 끊기를 뺀 키로 찾고, 끊기 수보다 글자가 많은 것만.
    wchar_t wkey[SEQ_MAX_READING + 1];
    int wmin = 0;
    if (sl->zh) { int k = StripKey(st->reading, n, wkey, SEQ_MAX_READING + 1, NULL); wmin = k ? k + 1 : 0; }
    else lstrcpynW(wkey, st->reading, SEQ_MAX_READING + 1);
    int pidx[SEQ_PREDICT_CANDS], np = 0, pnext = 0;
    if ((flags & SEQ_CONV_PREDICT) && wcslen(wkey) >= 2)
        np = JDict_Completions(sl->cand, wkey, pidx, SEQ_PREDICT_CANDS, 20000);
    int wfirst = 0, wcount = 0, wnext = 0;
    bool whole = (!sl->zh || (n > 0 && st->reading[0] != SEQ_SEPARATOR && st->reading[n - 1] != SEQ_SEPARATOR))
                 && JDict_Candidates(sl->cand, wkey, &wfirst, &wcount);
    AddWhole(out, sl, wfirst, whole ? wcount : 0, &wnext, SEQ_WHOLE_FIRST, n, wmin);
    // 모호음: 비슷한 철자의 범위 (한 음절은 제 철자의 후보만 수백이다 — 뒤에 붙이면 첫 쪽에 못 든다)
    int fFirst[16], fCount[16], fNext[16], nfr = 0, fuzzyShown = 0;
    if (flags & SEQ_CONV_FUZZY) {
        wchar_t fk[16][SEQ_MAX_READING + 1];
        int nf = SeqKb_FuzzyKeys(wkey, fk, 16);
        for (int v = 1; v < nf; v++)
            if (JDict_Candidates(sl->cand, fk[v], &fFirst[nfr], &fCount[nfr])) { fNext[nfr] = 0; nfr++; }
        for (int r = 0; r < nfr && fuzzyShown < SEQ_FUZZY_PAGE1; r++) {   // 첫 쪽: 철자마다 하나씩, 둘까지
            int before = out->count;
            AddWhole(out, sl, fFirst[r], fCount[r], &fNext[r], 1, n, wmin);
            fuzzyShown += out->count - before;
        }
    }
    AddPredict(out, sl, pidx, np, &pnext, SEQ_PREDICT_FIRST - fuzzyShown, n, wmin);
    if (sl->ja) {   // 히라가나·가타카나 그대로 (일본어 입력기의 기본 후보) — 첫 쪽 끝에
        for (int form = SEQ_KANA_HIRAGANA; form <= SEQ_KANA_KATAKANA && out->count < SEQ_MAX_CANDS; form++) {
            wchar_t kf[SEQ_MAX_OUT + 1];
            if (!SeqKb_KanaForm(st->reading, form, kf, SEQ_MAX_OUT + 1)) continue;
            bool dup = false;
            for (int j = 0; j < out->count; j++) if (!wcscmp(out->items[j], kf)) dup = true;
            if (!dup) { lstrcpynW(out->items[out->count], kf, SEQ_MAX_OUT + 1); out->consumed[out->count++] = n; }
        }
    }
    for (int r = 0; r < nfr; r++) AddWhole(out, sl, fFirst[r], fCount[r], &fNext[r], SEQ_FUZZY_FIRST, n, wmin);
    AddWhole(out, sl, wfirst, whole ? wcount : 0, &wnext, SEQ_MAX_CANDS, n, wmin);
    AddPredict(out, sl, pidx, np, &pnext, SEQ_PREDICT_CANDS, n, wmin);
    int order[SEQ_MAX_READING + 1], no = 0;
    if (firstSegLen > 0 && firstSegLen < n) order[no++] = firstSegLen;
    for (int len = n - 1; len >= 1; len--) if (len != firstSegLen) order[no++] = len;
    for (int k = 0; k < no && out->count < SEQ_MAX_CANDS; k++) {
        int len = order[k];
        int first = 0, count = 0, minChars = 0;
        if (!SegRange(sl, st->reading, len, bare, &first, &count, &minChars)) continue;
        int take = SEQ_PREFIX_CANDS;
        for (int i = 0; i < count && take > 0 && out->count < SEQ_MAX_CANDS; i++) {
            if (minChars > 0 && CandChars(sl->cand, first + i) < minChars) continue;
            const jdchar *v = NULL; int vn = 0;
            if (!JDict_CandidateAt(sl->cand, first + i, &v, &vn)) break;
            if (JDict_CopyValue(v, vn, out->items[out->count], SEQ_MAX_OUT + 1) >= 0
                && !(g_noEmoji && SeqKb_IsEmoji(out->items[out->count]))) {
                out->consumed[out->count] = len;
                out->count++;
                take--;
            }
        }
    }
    if (out->count == 0) { *st = before; return false; }
    st->candOpen = true;
    return true;
}

SeqResult SeqKb_Choose(SeqState *st, const SeqLayout *sl, const SeqCandidates *cands, int index) {
    (void)sl;
    SeqResult r; memset(&r, 0, sizeof r);
    if (!st || !cands || index < 0 || index >= cands->count) return r;
    if (cands->generation != st->generation) return r;   // 늦게 온 결과·이전 읽기의 것은 버린다
    Emit(&r, cands->items[index]);
    // 앞부분만 쓰는 후보면 나머지 읽기는 남는다 — 이어서 바꾼다(입력기가 후보창을 다시 연다).
    int used = cands->consumed[index];
    int n = (int)wcslen(st->reading);
    while (used > 0 && used < n && st->reading[used] == SEQ_SEPARATOR) used++;   // 남은 읽기 앞의 끊기는 버린다
    if (used > 0 && used < n) {
        memmove(st->reading, st->reading + used, (size_t)(n - used + 1) * sizeof(wchar_t));
        lstrcpynW(r.composing, st->reading, (int)(sizeof r.composing / sizeof r.composing[0]));
    } else {
        st->reading[0] = L'\0';
        st->pending[0] = L'\0';
    }
    st->generation++;
    st->candOpen = false;
    r.eaten = true;
    return r;
}

SeqResult SeqKb_ChoosePart(SeqState *st, const SeqLayout *sl, const SeqCandidates *cands, int index, bool last) {
    SeqResult r; memset(&r, 0, sizeof r);
    if (!st || !cands || index < 0 || index >= cands->count || cands->generation != st->generation) return r;
    const wchar_t *c = cands->items[index];
    size_t len = wcslen(c);
    if (len == 0) return r;
    wchar_t one[3] = {0};
    if (!last) {
        one[0] = c[0];
        if (c[0] >= 0xD800 && c[0] <= 0xDBFF && len > 1) one[1] = c[1];
    } else {
        if (len > 1 && c[len - 1] >= 0xDC00 && c[len - 1] <= 0xDFFF) { one[0] = c[len - 2]; one[1] = c[len - 1]; }
        else one[0] = c[len - 1];
    }
    // 그 후보가 쓰는 읽기는 지운다 — 고른 것과 같은 길로 읽기를 줄이고, 낼 글자만 바꾼다
    SeqCandidates tmp = *cands;
    lstrcpynW(tmp.items[index], one, SEQ_MAX_OUT + 1);
    return SeqKb_Choose(st, sl, &tmp, index);
}

SeqResult SeqKb_CommitBest(SeqState *st, const SeqLayout *sl, unsigned flags) {
    SeqResult r; memset(&r, 0, sizeof r);
    if (!st || !sl) return r;
    static SeqCandidates c;   // 크다(약 10KB) — 입력 스레드 하나가 쓴다
    for (int guard = 0; guard < SEQ_MAX_READING + 1 && (st->reading[0] || st->pending[0]); guard++) {
        if (!sl->cand || !SeqKb_ConvertEx(st, sl, flags, &c)) {   // 후보가 없는 꼬리는 친 그대로
            SeqResult f = SeqKb_Flush(st, sl);
            Emit(&r, f.committed);
            break;
        }
        SeqResult one = SeqKb_Choose(st, sl, &c, 0);
        Emit(&r, one.committed);
        if (!one.eaten) break;
    }
    r.eaten = true;
    return r;
}

void SeqKb_NoteCommitted(SeqState *st, const wchar_t *text) {
    if (st && text && text[0]) st->lastCommit = text[wcslen(text) - 1];
}

// 중국어 문장부호 (搜狗·Microsoft 병음의 기본과 같다). 번체는 따옴표가 「」『』.
static const wchar_t *PunctFor(const SeqLayout *sl, wchar_t ch) {
    switch (ch) {
        case L',': return L"\xFF0C";  case L'.': return L"\x3002";  case L'?': return L"\xFF1F";
        case L'!': return L"\xFF01";  case L';': return L"\xFF1B";  case L':': return L"\xFF1A";
        case L'\\': return L"\x3001"; case L'(': return L"\xFF08";  case L')': return L"\xFF09";
        case L'[': return L"\x3010";  case L']': return L"\x3011";  case L'<': return L"\x300A";
        case L'>': return L"\x300B";  case L'^': return L"\x2026\x2026";  case L'_': return L"\x2014\x2014";
        case L'~': return L"\xFF5E";  case L'`': return L"\x00B7";
        case L'$': return sl->zh == SEQ_ZH_TRADITIONAL ? NULL : L"\xFFE5";
        case L'"': case L'\'': return L"";   // 따옴표 — 짝은 SeqKb_Punct 가 정한다
        default: return NULL;
    }
}
bool SeqKb_IsPunct(const SeqLayout *sl, wchar_t ch) { return sl && sl->zh && PunctFor(sl, ch) != NULL; }

bool SeqKb_Punct(SeqState *st, const SeqLayout *sl, wchar_t ch, wchar_t *out, int cap) {
    if (!st || !SeqKb_IsPunct(sl, ch) || cap < 3) return false;
    // 숫자 바로 뒤의 . , : 는 그대로 — 3.14, 1,000, 12:30
    if ((ch == L'.' || ch == L',' || ch == L':') && st->lastCommit >= L'0' && st->lastCommit <= L'9') return false;
    const bool trad = sl->zh == SEQ_ZH_TRADITIONAL;
    if (ch == L'"') {
        out[0] = trad ? (st->dquoteOpen ? L'\x300D' : L'\x300C') : (st->dquoteOpen ? L'\x201D' : L'\x201C');
        out[1] = L'\0';
        st->dquoteOpen = !st->dquoteOpen;
        return true;
    }
    if (ch == L'\'') {
        out[0] = trad ? (st->squoteOpen ? L'\x300F' : L'\x300E') : (st->squoteOpen ? L'\x2019' : L'\x2018');
        out[1] = L'\0';
        st->squoteOpen = !st->squoteOpen;
        return true;
    }
    lstrcpynW(out, PunctFor(sl, ch), cap);
    return true;
}

// 날짜·시간: rq = 날짜, sj = 시각, xq = 요일 (搜狗·Rime 의 관례). 번체는 時·週·禮拜.
static void ZhNum(int v, wchar_t *o, int cap) {   // 1..99 를 한자 수로 (十, 十一, 二十, 三十一)
    static const wchar_t d[] = L"\x3007\x4E00\x4E8C\x4E09\x56DB\x4E94\x516D\x4E03\x516B\x4E5D";
    wchar_t b[8]; int n = 0;
    if (v >= 20) b[n++] = d[v / 10];
    if (v >= 10) b[n++] = L'\x5341';
    if (v % 10 || v < 10) b[n++] = d[v % 10];
    b[n] = L'\0';
    lstrcpynW(o, b, cap);
}
int SeqKb_DateTimeCands(const SeqLayout *sl, const wchar_t *reading, const SYSTEMTIME *t,
                        wchar_t items[][SEQ_MAX_OUT + 1], int cap) {
    if (!sl || !sl->zh || !reading || !t || cap <= 0) return 0;
    const bool trad = sl->zh == SEQ_ZH_TRADITIONAL;
    int n = 0;
    #define ADD(...) do { if (n < cap) { _snwprintf(items[n], SEQ_MAX_OUT + 1, __VA_ARGS__); items[n][SEQ_MAX_OUT] = 0; n++; } } while (0)
    if (!wcscmp(reading, L"rq")) {
        ADD(L"%d\x5E74%d\x6708%d\x65E5", t->wYear, t->wMonth, t->wDay);
        ADD(L"%04d-%02d-%02d", t->wYear, t->wMonth, t->wDay);
        ADD(L"%d/%d/%d", t->wYear, t->wMonth, t->wDay);
        static const wchar_t d[] = L"\x3007\x4E00\x4E8C\x4E09\x56DB\x4E94\x516D\x4E03\x516B\x4E5D";
        wchar_t y[8], m[8], dd[8];
        _snwprintf(y, 8, L"%lc%lc%lc%lc", d[t->wYear / 1000 % 10], d[t->wYear / 100 % 10], d[t->wYear / 10 % 10], d[t->wYear % 10]);
        ZhNum(t->wMonth, m, 8); ZhNum(t->wDay, dd, 8);
        ADD(L"%ls\x5E74%ls\x6708%ls\x65E5", y, m, dd);
    } else if (!wcscmp(reading, L"sj")) {
        ADD(L"%02d:%02d", t->wHour, t->wMinute);
        ADD(L"%d%lc%02d\x5206", t->wHour, trad ? L'\x6642' : L'\x65F6', t->wMinute);
        ADD(L"%02d:%02d:%02d", t->wHour, t->wMinute, t->wSecond);
    } else if (!wcscmp(reading, L"xq")) {
        static const wchar_t *wd[] = { L"\x65E5", L"\x4E00", L"\x4E8C", L"\x4E09", L"\x56DB", L"\x4E94", L"\x516D" };
        const wchar_t *w = wd[t->wDayOfWeek % 7];
        ADD(L"\x661F\x671F%ls", w);
        ADD(L"%lc%ls", trad ? L'\x9031' : L'\x5468', w);
        ADD(L"%ls\x62DC%ls", trad ? L"\x79AE" : L"\x793C", w);
    }
    #undef ADD
    return n;
}

// ── 일본어 방식: 가나 꼴 바꾸기 (F6~F10) ─────────────────────────────────────────────────
bool SeqKb_IsJaPunctKey(wchar_t ch) {   // '-' 는 장음 ー 라 글자다 — 여기에 넣지 않는다
    return ch == L',' || ch == L'.' || ch == L'[' || ch == L']' || ch == L'/' || ch == L'~' || ch == L'!' || ch == L'?';
}
static wchar_t KanaPunctAscii(wchar_t c) {   // 읽기 안의 일본어 문장부호 → 친 글쇠
    switch (c) {
        case L'\x3002': return L'.';  case L'\x3001': return L',';  case L'\x300C': return L'[';  case L'\x300D': return L']';
        case L'\x30FB': return L'/';  case L'\x30FC': return L'-';  case L'\x301C': return L'~';  case L'\xFF01': return L'!';
        case L'\xFF1F': return L'?';
        default: return 0;
    }
}
static bool PutW(wchar_t *out, int cap, int *o, wchar_t c) { if (*o + 1 >= cap) return false; out[(*o)++] = c; return true; }
bool SeqKb_KanaForm(const wchar_t *r, int form, wchar_t *out, int cap) {
    if (!r || !r[0] || !out || cap < 2) return false;
    int o = 0;
    if (form == SEQ_KANA_HIRAGANA || form == SEQ_KANA_KATAKANA || form == SEQ_KANA_HALF) {
        for (const wchar_t *p = r; *p; p++) {
            wchar_t c = *p;
            if (form != SEQ_KANA_HIRAGANA && c >= 0x3041 && c <= 0x3096) c = (wchar_t)(c + 0x60);   // ひらがな → カタカナ
            else if (form == SEQ_KANA_HIRAGANA && c >= 0x30A1 && c <= 0x30F6) c = (wchar_t)(c - 0x60);
            if (form == SEQ_KANA_HALF) {
                if (c >= 0x30A1 && c <= 0x30F6) {
                    const unsigned short *h = g_kanaHalf[c - 0x30A1];
                    if (!PutW(out, cap, &o, (wchar_t)h[0]) || (h[1] && !PutW(out, cap, &o, (wchar_t)h[1]))) return false;
                    continue;
                }
                static const wchar_t from[] = L"\x3002\x3001\x300C\x300D\x30FB\x30FC", to[] = L"\xFF61\xFF64\xFF62\xFF63\xFF65\xFF70";
                const wchar_t *f = wcschr(from, c);
                if (f && c) c = to[f - from];
            }
            if (!PutW(out, cap, &o, c)) return false;
        }
        out[o] = 0;
        return true;
    }
    // 로마자 (F9 전각, F10 반각): 두 가나 음절 먼저, 촉음 っ 는 다음 자음을 겹친다
    const wchar_t *p = r;
    bool sokuon = false;
    while (*p) {
        wchar_t h[3] = { p[0], p[1], 0 };
        for (int k = 0; k < 2; k++) if (h[k] >= 0x30A1 && h[k] <= 0x30F6) h[k] = (wchar_t)(h[k] - 0x60);   // カタカナ도
        const char *rom = NULL; int used = 0;
        if (h[0] == 0x3063 && h[1]) { sokuon = true; p++; continue; }   // っ
        for (size_t i = 0; i < sizeof g_kanaRomaji / sizeof g_kanaRomaji[0] && !rom; i++) {
            size_t kl = wcslen(g_kanaRomaji[i].kana);
            if (kl <= 2 && !wcsncmp(h, g_kanaRomaji[i].kana, kl) && (kl == 1 || h[1])) { rom = g_kanaRomaji[i].romaji; used = (int)kl; }
        }
        char one[2] = { 0, 0 };
        if (!rom) {
            wchar_t a = KanaPunctAscii(p[0]);
            if (!a && p[0] >= 0x21 && p[0] <= 0x7E) a = p[0];
            if (!a) return false;   // 가나도 문장부호도 아닌 글자 — 로마자로 못 쓴다
            one[0] = (char)a; rom = one; used = 1;
        }
        if (sokuon && rom[0] >= 'a' && rom[0] <= 'z' && !(rom[0] == 'a' || rom[0] == 'i' || rom[0] == 'u' || rom[0] == 'e' || rom[0] == 'o')) {
            wchar_t d = (wchar_t)(rom[0] == 'c' ? 't' : rom[0]);   // っち → tchi
            if (!PutW(out, cap, &o, form == SEQ_KANA_ROMAJI_FULL ? (wchar_t)(d + 0xFEE0) : d)) return false;
        } else if (sokuon) {
            for (const char *x = "ltu"; *x; x++) if (!PutW(out, cap, &o, form == SEQ_KANA_ROMAJI_FULL ? (wchar_t)(*x + 0xFEE0) : (wchar_t)*x)) return false;
        }
        sokuon = false;
        for (const char *x = rom; *x; x++) {
            wchar_t c = (wchar_t)(unsigned char)*x;
            if (form == SEQ_KANA_ROMAJI_FULL && c >= 0x21 && c <= 0x7E) c = (wchar_t)(c + 0xFEE0);
            if (!PutW(out, cap, &o, c)) return false;
        }
        p += used;
    }
    if (sokuon) for (const char *x = "ltu"; *x; x++) if (!PutW(out, cap, &o, form == SEQ_KANA_ROMAJI_FULL ? (wchar_t)(*x + 0xFEE0) : (wchar_t)*x)) return false;
    out[o] = 0;
    return true;
}

// ── V 모드: 한자 수·금액·날짜·계산 ─────────────────────────────────────────────────────
bool SeqKb_ZhNumber(unsigned long long v, bool upper, bool trad, wchar_t *out, int cap) {
    static const wchar_t *lowD = L"\x96F6\x4E00\x4E8C\x4E09\x56DB\x4E94\x516D\x4E03\x516B\x4E5D";   // 零一二三四五六七八九
    static const wchar_t *upD  = L"\x96F6\x58F9\x8D30\x53C1\x8086\x4F0D\x9646\x67D2\x634C\x7396";   // 零壹贰叁肆伍陆柒捌玖
    static const wchar_t *upDT = L"\x96F6\x58F9\x8CB3\x53C3\x8086\x4F0D\x9678\x67D2\x634C\x7396";   // 번체 貳參陸
    const wchar_t *D = upper ? (trad ? upDT : upD) : lowD;
    const wchar_t unit[4] = { 0, upper ? L'\x62FE' : L'\x5341', upper ? L'\x4F70' : L'\x767E', upper ? L'\x4EDF' : L'\x5343' };   // 拾佰仟 / 十百千
    const wchar_t big[4] = { 0, trad ? L'\x842C' : L'\x4E07', trad ? L'\x5104' : L'\x4EBF', trad ? L'\x5146' : L'\x5146' };   // 万亿兆 (번체 萬億)
    if (v >= 10000000000000000ULL || cap < 2) return false;
    if (v == 0) { out[0] = D[0]; out[1] = 0; return true; }
    wchar_t buf[96]; int o = 0;
    int groups[4], ng = 0;
    for (unsigned long long t = v; t; t /= 10000) groups[ng++] = (int)(t % 10000);
    bool zeroPending = false;
    for (int g = ng - 1; g >= 0; g--) {
        int x = groups[g];
        if (x == 0) { zeroPending = true; continue; }
        if (zeroPending || (o > 0 && x < 1000)) { buf[o++] = D[0]; }
        zeroPending = false;
        int d[4] = { x / 1000, x / 100 % 10, x / 10 % 10, x % 10 };
        bool z = false;
        for (int k = 0; k < 4; k++) {
            int pos = 3 - k;
            if (d[k] == 0) { if (o > 0 && buf[o - 1] != D[0]) z = true; continue; }
            if (z) { buf[o++] = D[0]; z = false; }
            if (!(d[k] == 1 && pos == 1 && o == 0 && !upper)) buf[o++] = D[d[k]];   // 十二 (一十二 이 아니라) — 소写 맨 앞만
            if (pos) buf[o++] = unit[pos];
        }
        if (g) buf[o++] = big[g];
    }
    if (o >= 1 && buf[o - 1] == D[0]) o--;
    buf[o] = 0;
    if (o + 1 > cap) return false;
    wcscpy(out, buf);
    return true;
}
// 아주 작은 식 계산기: + - * / ( ) 와 소수. 실패하면 false.
typedef struct { const wchar_t *p; bool bad; } Calc;
static double CalcExpr(Calc *c);
static double CalcAtom(Calc *c) {
    if (*c->p == L'(') { c->p++; double v = CalcExpr(c); if (*c->p == L')') c->p++; else c->bad = true; return v; }
    if (*c->p == L'-') { c->p++; return -CalcAtom(c); }
    if (!((*c->p >= L'0' && *c->p <= L'9') || *c->p == L'.')) { c->bad = true; return 0; }
    double v = 0, frac = 0, scale = 1; bool dot = false; int digits = 0;
    for (; (*c->p >= L'0' && *c->p <= L'9') || *c->p == L'.'; c->p++) {
        if (*c->p == L'.') { if (dot) { c->bad = true; return 0; } dot = true; continue; }
        digits++;
        if (!dot) v = v * 10 + (*c->p - L'0'); else { scale /= 10; frac += (*c->p - L'0') * scale; }
    }
    if (!digits) c->bad = true;
    return v + frac;
}
static double CalcTerm(Calc *c) {
    double v = CalcAtom(c);
    while (!c->bad && (*c->p == L'*' || *c->p == L'/')) {
        wchar_t op = *c->p++; double r = CalcAtom(c);
        if (op == L'*') v *= r; else { if (r == 0) { c->bad = true; return 0; } v /= r; }
    }
    return v;
}
static double CalcExpr(Calc *c) {
    double v = CalcTerm(c);
    while (!c->bad && (*c->p == L'+' || *c->p == L'-')) { wchar_t op = *c->p++; double r = CalcTerm(c); v = op == L'+' ? v + r : v - r; }
    return v;
}
static void FormatNum(double v, wchar_t *out, int cap) {   // 열 자리 남짓, 꼬리 0 은 뗀다
    _snwprintf(out, cap, L"%.10g", v);
    out[cap - 1] = 0;
}
int SeqKb_VModeCands(const SeqLayout *sl, const wchar_t *reading, wchar_t items[][SEQ_MAX_OUT + 1], int cap) {
    if (!SeqKb_IsVMode(sl, reading) || !reading[1] || cap <= 0) return 0;
    const wchar_t *e = reading + 1;
    const bool trad = sl->zh == SEQ_ZH_TRADITIONAL;
    int n = 0, len = (int)wcslen(e), dots = 0, ops = 0;
    for (const wchar_t *q = e; *q; q++) { if (*q == L'.') dots++; else if (!(*q >= L'0' && *q <= L'9')) ops++; }
    #define PUT(...) do { if (n < cap) { _snwprintf(items[n], SEQ_MAX_OUT + 1, __VA_ARGS__); items[n][SEQ_MAX_OUT] = 0; n++; } } while (0)
    if (ops == 0 && dots == 0 && len <= 16) {                     // 정수: 소写 · 大写 · 금액 · 한 자리씩
        unsigned long long v = wcstoull(e, NULL, 10);
        wchar_t lo[96], up[96];
        if (SeqKb_ZhNumber(v, false, trad, lo, 96)) PUT(L"%ls", lo);
        if (SeqKb_ZhNumber(v, true, trad, up, 96)) { PUT(L"%ls", up); PUT(L"%ls\x5143\x6574", up); }   // 元整
        wchar_t each[64]; int o = 0;
        for (const wchar_t *q = e; *q && o < 63; q++) each[o++] = *q == L'0' ? L'\x3007' : L"\x3007\x4E00\x4E8C\x4E09\x56DB\x4E94\x516D\x4E03\x516B\x4E5D"[*q - L'0'];
        each[o] = 0;
        if (len > 1) PUT(L"%ls", each);
    } else if (ops == 0 && dots == 1 && e[0] != L'.' && e[len - 1] != L'.') {   // 소수: 三点一四 · 叁点壹肆
        const wchar_t *dot = wcschr(e, L'.');
        unsigned long long ip = wcstoull(e, NULL, 10);
        for (int up = 0; up < 2; up++) {
            wchar_t a[96], b[64]; int o = 0;
            if (!SeqKb_ZhNumber(ip, up, trad, a, 96)) break;
            const wchar_t *D = up ? (trad ? L"\x96F6\x58F9\x8CB3\x53C3\x8086\x4F0D\x9678\x67D2\x634C\x7396" : L"\x96F6\x58F9\x8D30\x53C1\x8086\x4F0D\x9646\x67D2\x634C\x7396")
                                  : L"\x96F6\x4E00\x4E8C\x4E09\x56DB\x4E94\x516D\x4E03\x516B\x4E5D";
            for (const wchar_t *q = dot + 1; *q && o < 63; q++) b[o++] = D[*q - L'0'];
            b[o] = 0;
            PUT(L"%ls%lc%ls", a, trad ? L'\x9EDE' : L'\x70B9', b);   // 点 / 點
        }
    } else if (ops == 0 && dots == 2) {                           // 날짜: 2026.10.3 → 2026年10月3日
        int y = 0, m = 0, d = 0;
        if (swscanf(e, L"%d.%d.%d", &y, &m, &d) == 3 && y > 0 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
            PUT(L"%d\x5E74%d\x6708%d\x65E5", y, m, d);
            PUT(L"%04d-%02d-%02d", y, m, d);
        }
    }
    if (ops > 0) {                                                // 계산: 결과, 식=결과
        Calc c = { e, false };
        double v = CalcExpr(&c);
        if (!c.bad && *c.p == 0) {
            wchar_t r[48]; FormatNum(v, r, 48);
            PUT(L"%ls", r);
            PUT(L"%ls=%ls", e, r);
        }
    }
    #undef PUT
    return n;
}

SeqResult SeqKb_CancelCandidates(SeqState *st) {
    SeqResult r; memset(&r, 0, sizeof r);
    if (!st || !st->candOpen) return r;
    st->candOpen = false;
    st->generation++;          // 접은 뒤에 도착한 선택은 남의 것이다
    r.eaten = true;
    SeqComposing(st, &r);
    return r;
}



// 사전을 찾는다: 자판 파일 옆 → 사용자 사전 폴더 → 기계 전체 사전 폴더 → DLL 옆.
//   이름은 이미 안전 검사를 지난 것이다(경로 구분자·장치 이름 없음).
static bool ResolveDict(const wchar_t *layoutPath, const wchar_t *file, wchar_t *out, int cch) {
    wchar_t cand[MAX_PATH], dir[MAX_PATH];
    if (layoutPath && layoutPath[0]) {
        lstrcpynW(dir, layoutPath, MAX_PATH);
        wchar_t *slash = wcsrchr(dir, L'\\');
        wchar_t *fwd = wcsrchr(dir, L'/');
        if (fwd && (!slash || fwd > slash)) slash = fwd;
        if (slash) { *slash = L'\0'; _snwprintf(cand, MAX_PATH, L"%ls\\%ls", dir, file); }
        else       { _snwprintf(cand, MAX_PATH, L"%ls", file); }
        cand[MAX_PATH - 1] = L'\0';
        if (GetFileAttributesW(cand) != INVALID_FILE_ATTRIBUTES) { lstrcpynW(out, cand, cch); return true; }
    }
    if (Config_UserDictDir(dir, MAX_PATH)) {
        _snwprintf(cand, MAX_PATH, L"%ls\\%ls", dir, file);
        cand[MAX_PATH - 1] = L'\0';
        if (GetFileAttributesW(cand) != INVALID_FILE_ATTRIBUTES) { lstrcpynW(out, cand, cch); return true; }
    }
    if (Config_MachineDictDir(dir, MAX_PATH)) {
        _snwprintf(cand, MAX_PATH, L"%ls\\%ls", dir, file);
        cand[MAX_PATH - 1] = L'\0';
        if (GetFileAttributesW(cand) != INVALID_FILE_ATTRIBUTES) { lstrcpynW(out, cand, cch); return true; }
    }
    return false;
}

// 사전을 찾아 열고 전수 점검한다. 자판 파일(.jmt)과 구운 자판(.jmb)이 같은 길을 쓴다 —
// 사전은 자판 밖에 있으므로, 어느 쪽으로 자판이 들어오든 여기서 한 번 본다 (RFC-0016 P5).
static bool OpenDictNow(SeqLayout *sl, const wchar_t *layoutPath, KlayDiag *diag);
static bool g_lazyOpen;
void SeqLayout_SetLazyOpen(bool on) { g_lazyOpen = on; }

// 쓸 때 열기: 자판을 읽을 때는 이름이 안전하고 파일이 있는지만 본다 (없으면 예전처럼 그 자판을 세우지 않는다).
static bool CheckPresent(const wchar_t *layoutPath, const wchar_t *file, KlayDiag *diag) {
    wchar_t full[MAX_PATH];
    if (Config_IsSafeDictFileName(file) && ResolveDict(layoutPath, file, full, MAX_PATH)) return true;
    wchar_t msg[200];
    _snwprintf(msg, 200, L"the dictionary '%ls' was not found", file);
    msg[199] = L'\0';
    KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT-MISSING", msg, L"put it beside the layout file or in the dictionary folder");
    return false;
}
// 연결 비용 파일 이름 (.jdc) — 사전 이름과 같은 규칙(경로 구분자·장치명 금지)
static bool IsSafeConnName(const wchar_t *name) {
    size_t n = name ? wcslen(name) : 0;
    if (n < 5 || n >= 64 || _wcsicmp(name + n - 4, L".jdc") != 0) return false;
    wchar_t alt[64];
    wcscpy(alt, name);
    wcscpy(alt + n - 4, L".jdb");
    return Config_IsSafeDictFileName(alt);
}
static bool OpenConn(SeqLayout *sl, const wchar_t *layoutPath, KlayDiag *diag) {
    if (!sl->connFile[0]) return true;
    wchar_t full[MAX_PATH];
    JDictError e = JDICT_OK;
    JConn *c = NULL;
    if (IsSafeConnName(sl->connFile) && ResolveDict(layoutPath, sl->connFile, full, MAX_PATH)) c = JConn_Open(full, !g_lazyOpen, &e);
    if (!c) {
        wchar_t msg[200];
        _snwprintf(msg, 200, L"the connection table '%ls' cannot be used", sl->connFile);
        msg[199] = L'\0';
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT-BAD", msg, L"build it with 'jamotong --build-conn'");
        return false;
    }
    if (sl->conn) JConn_Close(sl->conn);
    sl->conn = c;
    return true;
}
bool SeqLayout_OpenDict(SeqLayout *sl, const wchar_t *layoutPath, KlayDiag *diag) {
    if (!sl || !sl->dictFile[0]) return false;
    if (!g_lazyOpen) return OpenDictNow(sl, layoutPath, diag);
    lstrcpynW(sl->basePath, layoutPath ? layoutPath : L"", 260);
    if (!CheckPresent(layoutPath, sl->dictFile, diag)) return false;
    if (sl->candFile[0] && !CheckPresent(layoutPath, sl->candFile, diag)) return false;
    if (sl->toneFile[0] && !CheckPresent(layoutPath, sl->toneFile, diag)) return false;
    for (int i = 0; i < sl->nScheme; i++) if (!CheckPresent(layoutPath, sl->schemeFile[i], diag)) return false;
    if (sl->connFile[0]) {   // 연결 비용 파일도 있는지만 본다 (쓸 때 연다)
        wchar_t full[MAX_PATH];
        if (!IsSafeConnName(sl->connFile) || !ResolveDict(layoutPath, sl->connFile, full, MAX_PATH)) {
            KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT-MISSING", L"the connection table was not found",
                         L"put it beside the layout file or in the dictionary folder");
            return false;
        }
    }
    return true;
}
bool SeqLayout_EnsureOpen(SeqLayout *sl) {
    if (!sl) return false;
    if (sl->dict) return true;
    if (!g_lazyOpen || sl->openFailed) return false;
    if (OpenDictNow(sl, sl->basePath[0] ? sl->basePath : NULL, NULL)) return true;
    sl->openFailed = true;   // 상한 사전 — 그 자판은 글쇠를 먹지 않는다(응용이 그대로 받는다)
    return false;
}

static bool OpenDictNow(SeqLayout *sl, const wchar_t *layoutPath, KlayDiag *diag) {
    if (!sl || !sl->dictFile[0]) return false;
    // 이름 검사는 **여는 곳**에서 한다. 자판 파일뿐 아니라 구운 자판(.jmb)도 남이 준 파일일 수
    // 있어서, 그 안에 `..\..\어딘가.jdb` 가 들어 있으면 폴더 밖을 가리키게 된다.
    if (!Config_IsSafeDictFileName(sl->dictFile)) {
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT",
                     L"the dictionary must be a plain file name ending in .jdb",
                     L"no folders in the name - the file lives beside the layout or in the dictionary folder");
        return false;
    }
    wchar_t full[MAX_PATH];
    if (!ResolveDict(layoutPath, sl->dictFile, full, MAX_PATH)) {
        wchar_t msg[200];
        _snwprintf(msg, 200, L"the dictionary '%ls' was not found", sl->dictFile);
        msg[199] = L'\0';
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT-MISSING", msg,
                     L"put it beside the layout file or in the dictionary folder");
        return false;
    }
    JDictError derr = JDICT_OK;
    JDict *d = JDict_Open(full, &derr);
    if (!d) {
        wchar_t msg[200];
        _snwprintf(msg, 200, L"the dictionary '%ls' cannot be used: %ls", sl->dictFile, JDict_ErrorText(derr));
        msg[199] = L'\0';
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT-BAD", msg,
                     L"build it again with 'jamotong --build-dict'");
        return false;
    }
    if (JDict_Kind(d) != JDICT_KIND_SEQUENCE) {
        JDict_Close(d);
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT-KIND",
                     L"this dictionary is not a sequence dictionary", L"build it with 'Type = sequence'");
        return false;
    }
    if (sl->dict) JDict_Close(sl->dict);
    sl->dict = d;
    lstrcpynW(sl->dictPath, full, 260);
    sl->maxIn = JDict_MaxKeyLen(d);

    if (sl->candFile[0]) {   // 후보 사전 (§6.4) — 같은 규칙으로 찾고 열고 본다
        if (!Config_IsSafeDictFileName(sl->candFile) || !ResolveDict(layoutPath, sl->candFile, full, MAX_PATH)) {
            wchar_t msg[200];
            _snwprintf(msg, 200, L"the candidate dictionary '%ls' was not found", sl->candFile);
            msg[199] = L'\0';
            KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT-MISSING", msg,
                         L"put it beside the layout file or in the dictionary folder");
            return false;
        }
        JDictError cerr = JDICT_OK;
        JDict *cd = JDict_Open(full, &cerr);
        if (!cd) {
            wchar_t msg[200];
            _snwprintf(msg, 200, L"the candidate dictionary '%ls' cannot be used: %ls", sl->candFile, JDict_ErrorText(cerr));
            msg[199] = L'\0';
            KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT-BAD", msg,
                         L"build it again with 'jamotong --build-dict'");
            return false;
        }
        if (JDict_Kind(cd) != JDICT_KIND_CANDIDATES) {
            JDict_Close(cd);
            KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT-KIND",
                         L"this dictionary is not a candidate dictionary",
                         L"build it with 'Type = candidates'");
            return false;
        }
        if (sl->cand) JDict_Close(sl->cand);
        sl->cand = cd;
    }
    if (sl->toneFile[0]) {   // 성조 병음 (후보 사전 꼴) — 없거나 상했으면 자판을 세우지 않는다
        JDictError terr = JDICT_OK;
        JDict *td = NULL;
        if (Config_IsSafeDictFileName(sl->toneFile) && ResolveDict(layoutPath, sl->toneFile, full, MAX_PATH)) td = JDict_Open(full, &terr);
        if (!td || JDict_Kind(td) != JDICT_KIND_CANDIDATES || (!g_lazyOpen && !JDict_Verify(td, &terr))) {
            if (td) JDict_Close(td);
            wchar_t msg[200];
            _snwprintf(msg, 200, L"the tones dictionary '%ls' cannot be used", sl->toneFile);
            msg[199] = L'\0';
            KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT-BAD", msg, L"build it as 'Type = candidates' with 'jamotong --build-dict'");
            return false;
        }
        if (sl->tones) JDict_Close(sl->tones);
        sl->tones = td;
    }
    for (int i = 0; i < sl->nScheme; i++) {   // 쌍병 글쇠 표 — Dictionary 와 같은 종류(순차)여야 한다
        if (!Config_IsSafeDictFileName(sl->schemeFile[i]) || !ResolveDict(layoutPath, sl->schemeFile[i], full, MAX_PATH)) {
            wchar_t msg[200];
            _snwprintf(msg, 200, L"the key table '%ls' was not found", sl->schemeFile[i]);
            msg[199] = L'\0';
            KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT-MISSING", msg, L"put it beside the layout file or in the dictionary folder");
            return false;
        }
        JDictError serr = JDICT_OK;
        JDict *sd = JDict_Open(full, &serr);
        if (!sd || JDict_Kind(sd) != JDICT_KIND_SEQUENCE || (!g_lazyOpen && !JDict_Verify(sd, &serr))) {
            if (sd) JDict_Close(sd);
            wchar_t msg[200];
            _snwprintf(msg, 200, L"the key table '%ls' cannot be used", sl->schemeFile[i]);
            msg[199] = L'\0';
            KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT-BAD", msg, L"build it as 'Type = sequence' with 'jamotong --build-dict'");
            return false;
        }
        if (sl->schemeDict[i]) JDict_Close(sl->schemeDict[i]);
        sl->schemeDict[i] = sd;
    }
    if (!OpenConn(sl, layoutPath, diag)) return false;
    // 내용까지 본다 — 성한 사전만 자판을 세운다. 입력기(쓸 때 열기)는 전수 점검을 하지 않는다(RFC-0020 F3): CRC·순서를 보려고
    //   사전 전체(중국어 12MB)를 앱마다 메모리로 끌어올렸다. 항목의 범위는 읽을 때마다 본다. 전수 점검은 도구가 한다.
    if (g_lazyOpen) return true;
    return SeqLayout_Verify(sl, diag);
}


bool SeqLayout_Verify(const SeqLayout *sl, KlayDiag *diag) {
    if (sl && sl->cand) {   // 후보 사전도 전수로 본다
        JDictError cerr = JDICT_OK;
        if (!JDict_Verify(sl->cand, &cerr)) {
            wchar_t msg[200];
            _snwprintf(msg, 200, L"the candidate dictionary '%ls' did not pass its check: %ls",
                       sl->candFile, JDict_ErrorText(cerr));
            msg[199] = L'\0';
            KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT-BAD", msg,
                         L"build it again with 'jamotong --build-dict'");
            return false;
        }
    }
    if (!sl || !sl->dict) {
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT-BAD", L"this layout has no usable dictionary", NULL);
        return false;
    }
    JDictError err = JDICT_OK;
    if (JDict_Verify(sl->dict, &err)) return true;
    wchar_t msg[200];
    _snwprintf(msg, 200, L"the dictionary '%ls' did not pass its check: %ls", sl->dictFile, JDict_ErrorText(err));
    msg[199] = L'\0';
    KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT-BAD", msg, L"build it again with 'jamotong --build-dict'");
    return false;
}


void SeqLayout_Free(SeqLayout *sl) {
    if (!sl) return;
    if (sl->dict) JDict_Close(sl->dict);
    if (sl->cand) JDict_Close(sl->cand);
    if (sl->chord) ChordLayout_Free((ChordLayout *)sl->chord);
    for (int i = 0; i < sl->nScheme; i++) if (sl->schemeDict[i]) JDict_Close(sl->schemeDict[i]);
    if (sl->tones) JDict_Close(sl->tones);
    if (sl->conn) JConn_Close(sl->conn);
    if (sl->phrases) { PhrasesClear(sl->phrases); free(sl->phrases); }
    free(sl);
}
