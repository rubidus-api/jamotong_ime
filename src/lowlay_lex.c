// lowlay_lex.c — 자판 정의 언어 v4 의 어휘기 (RFC-0018 P1).
//   규칙의 출처는 로우엔트 조항 §6.1 이다. 다른 점은 둘뿐이다:
//     · 부동소수가 없다 — 이 언어에 소수가 쓰일 자리가 없고, 그러면 숫자 뒤의 점을 닫개로만 읽으면 된다.
//     · heredoc 은 P2 에서 넣는다(매크로의 긴 글월). 여기서는 주석 `note` 만 같은 꼴로 읽는다.
#include "lowlay_lex.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define FAIL(code, msg, help) do { KlayDiag_Add(diag, KLAY_SEV_ERROR, line, col, (code), (msg), (help)); \
    bad = true; } while (0)

static bool Push(LowTokens *t, const LowTok *tok) {
    if (t->n == t->cap) {
        int ncap = t->cap ? t->cap * 2 : 64;
        LowTok *nv = (LowTok*)realloc(t->v, (size_t)ncap * sizeof *nv);
        if (!nv) return false;
        t->v = nv; t->cap = ncap;
    }
    t->v[t->n++] = *tok;
    return true;
}

static bool IsNameStart(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_'; }
static bool IsNameCh(wchar_t c)    { return IsNameStart(c) || (c >= L'0' && c <= L'9'); }
static bool IsDigit(wchar_t c)     { return c >= L'0' && c <= L'9'; }
static bool IsSpace(wchar_t c)     { return c == L' ' || c == L'\t' || c == L'\r'; }

// 코드포인트 하나를 wchar_t 줄에 담는다. wchar_t 가 2바이트면 대리쌍으로 쪼갠다.
static int PutCp(wchar_t *out, int cap, int at, unsigned long cp) {
    if (sizeof(wchar_t) >= 4 || cp < 0x10000) {
        if (at + 1 > cap) return -1;
        out[at] = (wchar_t)cp;
        return at + 1;
    }
    if (at + 2 > cap) return -1;
    cp -= 0x10000;
    out[at]     = (wchar_t)(0xD800 + (cp >> 10));
    out[at + 1] = (wchar_t)(0xDC00 + (cp & 0x3FF));
    return at + 2;
}

// 이스케이프 하나. p 는 역슬래시 다음을 가리킨다. 성공하면 코드포인트와 먹은 길이.
static bool ReadEscape(const wchar_t *p, unsigned long *cp, int *eaten) {
    static const struct { wchar_t c; unsigned long v; } kSimple[] = {
        { L'\\', 0x5C }, { L'"', 0x22 }, { L'\'', 0x27 }, { L'a', 0x07 }, { L'b', 0x08 },
        { L'f', 0x0C },  { L'n', 0x0A }, { L'r', 0x0D },  { L't', 0x09 }, { L'v', 0x0B },
        { L'0', 0x00 },
    };
    for (size_t i = 0; i < sizeof kSimple / sizeof kSimple[0]; i++)
        if (p[0] == kSimple[i].c) { *cp = kSimple[i].v; *eaten = 1; return true; }
    int want = p[0] == L'x' ? 2 : p[0] == L'u' ? 4 : p[0] == L'U' ? 8 : 0;
    if (!want) return false;                      // 닫힌 집합 밖 — 팔진도 여기서 걸린다
    unsigned long v = 0;
    for (int i = 1; i <= want; i++) {
        wchar_t c = p[i];
        int d;
        if (c >= L'0' && c <= L'9') d = c - L'0';
        else if (c >= L'a' && c <= L'f') d = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') d = c - L'A' + 10;
        else return false;                        // 자리 수는 값에 따라 달라지지 않는다
        v = v * 16 + (unsigned long)d;
    }
    if (want > 2) {                               // \u \U 는 코드포인트다
        if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return false;
    }
    *cp = v; *eaten = want + 1;
    return true;
}

bool LowLex_Run(const wchar_t *src, LowTokens *out, KlayDiag *diag) {
    memset(out, 0, sizeof *out);
    if (!src) return false;
    bool bad = false;
    int line = 1, col = 1;
    const wchar_t *p = src;

    while (*p) {
        if (*p == L'\n') { p++; line++; col = 1; continue; }
        if (IsSpace(*p))  { p++; col++; continue; }

        LowTok tok;
        memset(&tok, 0, sizeof tok);
        tok.line = line; tok.col = col;

        // 구두점
        if (*p == L'.') { tok.kind = LOW_DOT; p++; col++; if (!Push(out, &tok)) return false; continue; }
        if (*p == L'(') { tok.kind = LOW_LP;  p++; col++; if (!Push(out, &tok)) return false; continue; }
        if (*p == L')') { tok.kind = LOW_RP;  p++; col++; if (!Push(out, &tok)) return false; continue; }

        // 이름 · 주석 · 리터럴 접두사
        if (IsNameStart(*p)) {
            wchar_t w[LOW_NAME_MAX];
            int k = 0;
            while (IsNameCh(*p) && k + 1 < LOW_NAME_MAX) { w[k++] = *p++; col++; }
            w[k] = L'\0';
            if (IsNameCh(*p)) { FAIL(L"E-LOW-NAME", L"name is too long (32)", NULL); while (IsNameCh(*p)) { p++; col++; } continue; }

            if (!wcscmp(w, L"rem")) {                       // 줄 끝까지 주석
                while (*p && *p != L'\n') p++;
                continue;
            }
            if (!wcscmp(w, L"note")) {                      // 태그로 여는 여러 줄 주석
                while (IsSpace(*p)) { p++; col++; }
                wchar_t tag[LOW_NAME_MAX];
                int t = 0;
                while (IsNameCh(*p) && t + 1 < LOW_NAME_MAX) { tag[t++] = *p++; col++; }
                tag[t] = L'\0';
                if (!t) { FAIL(L"E-LOW-NOTE", L"note needs a tag", L"note DOC ... DOC"); continue; }
                while (*p && *p != L'\n') p++;              // 태그 줄의 나머지는 버린다
                bool closed = false;
                while (*p && !closed) {
                    p++; line++; col = 1;                   // 줄 넘김
                    const wchar_t *s = p;
                    while (IsSpace(*s)) s++;
                    if (!wcsncmp(s, tag, (size_t)t)) {
                        const wchar_t *e = s + t;
                        while (IsSpace(*e)) e++;
                        if (*e == L'\n' || *e == L'\0') { p = e; closed = true; break; }
                    }
                    while (*p && *p != L'\n') p++;
                }
                if (!closed) FAIL(L"E-LOW-NOTE", L"note is never closed by its tag", NULL);
                continue;
            }
            // 접두사: u"…" · U'…' — 따옴표가 **붙어 있을 때만** 접두사다
            if ((*p == L'"' || *p == L'\'')) {
                if (k == 1 && (w[0] == L'u' || w[0] == L'U')) {
                    tok.prefix = w[0];
                } else {
                    FAIL(L"E-LOW-PREFIX", L"only u and U are literal prefixes", L"write u\"...\" or U'...'");
                    while (*p && *p != L'\n') { p++; col++; }
                    continue;
                }
            } else {
                tok.kind = LOW_NAME;
                wcscpy_s(tok.name, LOW_NAME_MAX, w);
                if (!Push(out, &tok)) return false;
                continue;
            }
        }

        // 문자·문자열
        if (*p == L'"' || *p == L'\'') {
            wchar_t quote = *p++;
            col++;
            wchar_t buf[LOW_STR_MAX];
            int at = 0;
            bool ok = true;
            while (*p && *p != quote && *p != L'\n') {
                unsigned long cp;
                if (*p == L'\\') {
                    int eaten = 0;
                    if (!ReadEscape(p + 1, &cp, &eaten)) {
                        FAIL(L"E-LOW-ESCAPE", L"unknown or malformed escape", L"the escape set is closed: \\\\ \\\" \\' \\a \\b \\f \\n \\r \\t \\v \\0 \\xNN \\uXXXX \\UXXXXXXXX");
                        ok = false;
                        break;
                    }
                    p += 1 + eaten; col += 1 + eaten;
                } else {
                    cp = (unsigned long)(unsigned)*p;
                    // 원본이 대리쌍이면 코드포인트로 되돌린다 (wchar_t 2바이트 환경)
                    if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF &&
                        (unsigned)p[1] >= 0xDC00 && (unsigned)p[1] <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + ((unsigned long)p[1] - 0xDC00);
                        p++; col++;
                    }
                    p++; col++;
                }
                int nat = PutCp(buf, LOW_STR_MAX - 1, at, cp);
                if (nat < 0) { FAIL(L"E-LOW-STR", L"literal is too long (512)", NULL); ok = false; break; }
                at = nat;
            }
            if (!ok) { while (*p && *p != quote && *p != L'\n') p++; if (*p == quote) { p++; col++; } continue; }
            if (*p != quote) { FAIL(L"E-LOW-STR", L"literal is not closed", NULL); continue; }
            p++; col++;
            buf[at] = L'\0';

            if (quote == L'\'') {                            // 문자 리터럴 — 원소 하나
                tok.kind = LOW_CHAR;
                int elems = at;                              // wchar_t 개수
                unsigned long cp = at > 0 ? (unsigned long)(unsigned)buf[0] : 0;
                if (sizeof(wchar_t) == 2 && at == 2 && (unsigned)buf[0] >= 0xD800 && (unsigned)buf[0] <= 0xDBFF)
                    cp = 0x10000 + (((unsigned long)buf[0] - 0xD800) << 10) + ((unsigned long)buf[1] - 0xDC00);
                if (at == 0) { FAIL(L"E-LOW-CHAR", L"a character literal holds exactly one element", NULL); continue; }
                if (tok.prefix == 0 && (cp > 0xFF || elems > 1)) {
                    FAIL(L"E-LOW-CHAR", L"without a prefix a character literal is one byte", L"write U'...' for a code point");
                    continue;
                }
                if (tok.prefix == L'u' && cp > 0xFFFF) {
                    FAIL(L"E-LOW-CHAR", L"u'...' is one UTF-16 unit", L"write U'...' for a code point");
                    continue;
                }
                if (tok.prefix == L'U' && elems > (sizeof(wchar_t) == 2 ? 2 : 1)) {
                    FAIL(L"E-LOW-CHAR", L"U'...' is one code point", NULL);
                    continue;
                }
                tok.num = (long long)cp;
                if (!Push(out, &tok)) return false;
                continue;
            }
            tok.kind = LOW_STR;                              // 문자열 리터럴
            tok.str = (wchar_t*)malloc((size_t)(at + 1) * sizeof(wchar_t));
            if (!tok.str) return false;
            memcpy(tok.str, buf, (size_t)(at + 1) * sizeof(wchar_t));
            tok.strLen = at;
            if (!Push(out, &tok)) { free(tok.str); return false; }
            continue;
        }

        // 수 — 십진·십육진·이진, 자리 사이 밑줄. 팔진은 없다(앞의 0 은 십진).
        //   숫자에 **붙은** 빼기표는 음수 리터럴의 일부다(`-12`). 떨어져 있으면 이 언어에 없는 기호다 —
        //   연산자를 들여오지 않으면서 포인터 이동 같은 음수를 적을 수 있다.
        if (IsDigit(*p) || (*p == L'-' && IsDigit(p[1]))) {
            bool neg = false;
            if (*p == L'-') { neg = true; p++; col++; }
            int base = 10;
            if (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) { base = 16; p += 2; col += 2; }
            else if (p[0] == L'0' && (p[1] == L'b' || p[1] == L'B')) { base = 2; p += 2; col += 2; }
            long long v = 0;
            int digits = 0;
            bool over = false;
            while (*p == L'_' || IsDigit(*p) || (base == 16 && ((*p >= L'a' && *p <= L'f') || (*p >= L'A' && *p <= L'F')))) {
                if (*p == L'_') { p++; col++; continue; }
                int d = IsDigit(*p) ? *p - L'0' : (*p >= L'a' ? *p - L'a' + 10 : *p - L'A' + 10);
                if (d >= base) break;
                if (v > (0x7FFFFFFFFFFFFFFFLL - d) / base) over = true;
                else v = v * base + d;
                digits++; p++; col++;
            }
            if (!digits) { FAIL(L"E-LOW-NUM", L"a number needs at least one digit", NULL); continue; }
            if (over)    { FAIL(L"E-LOW-NUM", L"number is too large", NULL); continue; }
            if (IsNameStart(*p)) { FAIL(L"E-LOW-NUM", L"a number cannot be followed by a letter", NULL); while (IsNameCh(*p)) { p++; col++; } continue; }
            tok.kind = LOW_INT; tok.num = neg ? -v : v;
            if (!Push(out, &tok)) return false;
            continue;
        }

        {   // 그 밖의 글자는 이 언어에 없다 — 기호는 점과 괄호뿐이다
            wchar_t msg[160];
            swprintf(msg, 160, L"this language has no '%lc' - punctuation is only . ( )", *p);
            FAIL(L"E-LOW-CHARSET", msg, L"words replace symbols: and or not eq ne lt le gt ge");
            p++; col++;
        }
    }

    LowTok eof;
    memset(&eof, 0, sizeof eof);
    eof.kind = LOW_EOF; eof.line = line; eof.col = col;
    if (!Push(out, &eof)) return false;
    return !bad;
}

void LowTokens_Free(LowTokens *t) {
    if (!t) return;
    for (int i = 0; i < t->n; i++) if (t->v[i].kind == LOW_STR) free(t->v[i].str);
    free(t->v);
    memset(t, 0, sizeof *t);
}
