// seq_layout.c — 순차 변환 **런타임**: 사전을 열고 친 글자열을 바꾼다 (RFC-0016 §6.3).
//   파일을 읽는 일은 도구 쪽(seq_parse.c)이다 — 입력기는 구운 자판만 읽는다.
#include "seq_layout.h"
#include "config.h"      // Config_IsSafeDictFileName / 사전 폴더
#include "chord_layout.h"   // 앞단 조합 인식기 (RFC-0016 §6.3)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ── 사전에 묻는 두 가지 (엔진이 쓰는 말로) ─────────────────────────────────────────
static bool HasLonger(const SeqLayout *sl, const wchar_t *s) { return JDict_HasLonger(sl->dict, s); }
static bool Exact(const SeqLayout *sl, const wchar_t *s, wchar_t *out, int cap) {
    const jdchar *v = NULL; int n = 0;
    if (!JDict_Exact(sl->dict, s, &v, &n)) return false;
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
        if (JDict_LongestPrefix(sl->dict, buf, &kn, &v, &vn)) {
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

bool SeqKb_WouldEat(const SeqState *st, const SeqLayout *sl, wchar_t ch) {
    if (!sl || !sl->dict || ch < 0x21 || ch > 0x7E) return false;
    if (st->pending[0]) return true;
    wchar_t one[2] = { ch, 0 };
    return JDict_HasLonger(sl->dict, one) || JDict_Exact(sl->dict, one, NULL, NULL);
}

SeqResult SeqKb_Key(SeqState *st, const SeqLayout *sl, wchar_t ch) {
    SeqResult r; memset(&r, 0, sizeof r);
    if (!sl || !sl->dict) return r;
    if (ch < 0x21 || ch > 0x7E) {
        // 사이띄개·글쇠 아닌 문자는 엔진이 만지지 않는다 (단추·단축키·낱말 나누기를 응용에 맡긴다).
        // 보류가 있었으면 잃지 않도록 먼저 확정하고, 글쇠 자체는 응용으로 보낸다.
        if (st->pending[0]) { r = SeqKb_Flush(st, sl); r.eaten = false; }
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
    if (!sl || !sl->dict || !sym) return r;
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
        Emit(&r, st->reading);
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
    return st && sl && sl->cand && (st->reading[0] || st->pending[0]);
}

bool SeqKb_Convert(SeqState *st, const SeqLayout *sl, SeqCandidates *out) {
    if (!st || !sl || !sl->cand || !out) return false;
    // 후보가 없으면 **아무것도 바꾸지 않는다**. 바꿔 놓고 실패하면 그 글쇠(사이띄개)는 응용으로
    // 가는데 보류는 이미 읽기로 옮겨져, 사이띄개가 친 글자보다 먼저 문서에 들어간다.
    SeqState before = *st;
    // 보류한 글자를 먼저 읽기로 정착시킨다. `nihon` 의 끝 `n` 처럼 **더 자랄 수 있는 항목**은
    // 변환 글쇠를 누른 순간 경계로 보고 확정해야 한다 — 아니면 `にほん` 이 영영 서지 않는다.
    if (st->pending[0]) {
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
    const int n = (int)wcslen(st->reading);
    enum { SEG_PENALTY = 100, NOCOST_SEG = 1000, INF = 0x3FFFFFFF };
    int best[SEQ_MAX_READING + 1], back[SEQ_MAX_READING + 1];
    best[0] = 0;
    for (int j = 1; j <= n; j++) {
        best[j] = INF; back[j] = -1;
        for (int i = 0; i < j; i++) {
            if (best[i] >= INF) continue;
            wchar_t seg[SEQ_MAX_READING + 1];
            lstrcpynW(seg, st->reading + i, j - i + 1);
            int first = 0, count = 0;
            if (!JDict_Candidates(sl->cand, seg, &first, &count)) continue;
            int c = JDict_CostAt(sl->cand, first);
            if (c < 0) c = NOCOST_SEG;
            int total = best[i] + c + SEG_PENALTY;
            if (total < best[j]) { best[j] = total; back[j] = i; }
        }
    }
    int firstSegLen = 0;   // 가장 그럴듯한 길의 첫 낱말 길이 (길이 없으면 0)
    if (best[n] < INF) {
        int cuts[SEQ_MAX_READING + 1], nc = 0;
        for (int j = n; j > 0; j = back[j]) cuts[nc++] = j;   // 낱말 끝 자리들 (뒤에서부터)
        firstSegLen = cuts[nc - 1];
        if (nc >= 2) {   // 2) 문장 후보
            wchar_t *sent = out->items[0];
            sent[0] = L'\0';
            bool fits = true;
            for (int k = nc - 1, from = 0; k >= 0 && fits; from = cuts[k], k--) {
                wchar_t seg[SEQ_MAX_READING + 1], val[SEQ_MAX_OUT + 1];
                lstrcpynW(seg, st->reading + from, cuts[k] - from + 1);
                int first = 0, count = 0;
                const jdchar *v = NULL; int vn = 0;
                fits = JDict_Candidates(sl->cand, seg, &first, &count) && JDict_CandidateAt(sl->cand, first, &v, &vn)
                       && JDict_CopyValue(v, vn, val, SEQ_MAX_OUT + 1) >= 0
                       && wcslen(sent) + wcslen(val) <= SEQ_MAX_OUT;
                if (fits) wcscat(sent, val);
            }
            if (fits && sent[0]) { out->consumed[0] = n; out->count = 1; }
        }
    }
    // 3) 앞부분별 후보: 읽기 전체 → 길의 첫 낱말 → 나머지 긴 것부터
    int order[SEQ_MAX_READING + 1], no = 0;
    order[no++] = n;
    if (firstSegLen > 0 && firstSegLen < n) order[no++] = firstSegLen;
    for (int len = n - 1; len >= 1; len--) if (len != firstSegLen) order[no++] = len;
    for (int k = 0; k < no && out->count < SEQ_MAX_CANDS; k++) {
        int len = order[k];
        wchar_t prefix[SEQ_MAX_READING + 1];
        lstrcpynW(prefix, st->reading, len + 1);
        int first = 0, count = 0;
        if (!JDict_Candidates(sl->cand, prefix, &first, &count)) continue;
        int take = len == n ? SEQ_MAX_CANDS : SEQ_PREFIX_CANDS;
        for (int i = 0; i < count && take > 0 && out->count < SEQ_MAX_CANDS; i++) {
            const jdchar *v = NULL; int vn = 0;
            if (!JDict_CandidateAt(sl->cand, first + i, &v, &vn)) break;
            if (JDict_CopyValue(v, vn, out->items[out->count], SEQ_MAX_OUT + 1) >= 0) {
                if (out->count > 0 && out->consumed[0] == n && len == n && !wcscmp(out->items[0], out->items[out->count]))
                    continue;   // 문장 후보와 같은 글자면 두 번 보이지 않는다
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
bool SeqLayout_OpenDict(SeqLayout *sl, const wchar_t *layoutPath, KlayDiag *diag) {
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
    return SeqLayout_Verify(sl, diag);   // 내용까지 본다 — 성한 사전만 자판을 세운다
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
    free(sl);
}
