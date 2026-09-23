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
            if (JDict_CopyValue(v, vn, out, SEQ_MAX_OUT + 1) >= 0) Emit(r, out);
            buf += kn;
        } else {
            wchar_t lit[2] = { buf[0], 0 };
            Emit(r, lit);                                  // 사전에 없는 글자는 친 그대로
            buf++;
        }
    }
}

void SeqKb_Init(SeqState *st) { st->pending[0] = L'\0'; }

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
        Emit(&r, out);
        st->pending[0] = L'\0';
    } else if (sl->onUnmatched == SEQ_UNMATCHED_CANCEL) {
        st->pending[0] = L'\0';                    // 오류 없이 취소 — 보류와 그 글쇠가 사라진다
    } else {
        FlushBuf(st, sl, buf, &r, false);          // 확정 + 한 번의 재처리
    }
    lstrcpynW(r.composing, st->pending, SEQ_MAX_IN + 1);
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
    lstrcpynW(r.composing, st->pending, SEQ_MAX_IN + 1);
    return r;
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
    if (!st->pending[0] || !sl || !sl->dict) return r;
    wchar_t buf[SEQ_MAX_IN + 2];
    lstrcpynW(buf, st->pending, SEQ_MAX_IN + 2);
    r.eaten = true;
    FlushBuf(st, sl, buf, &r, true);
    lstrcpynW(r.composing, st->pending, SEQ_MAX_IN + 1);
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
    return SeqLayout_Verify(sl, diag);   // 내용까지 본다 — 성한 사전만 자판을 세운다
}


bool SeqLayout_Verify(const SeqLayout *sl, KlayDiag *diag) {
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
    if (sl->chord) ChordLayout_Free((ChordLayout *)sl->chord);
    free(sl);
}
