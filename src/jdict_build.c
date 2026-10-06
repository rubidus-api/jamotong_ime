// jdict_build.c — 사전 원본(.jdt)을 읽어 검사하고 이진(.jdb)으로 굽는다 (RFC-0016 P5).
//   이 파일은 도구(관리 앱·CLI)에만 들어간다. 입력기 DLL 은 구운 결과만 읽는다(jdict.c).
#include "jdict_build.h"
#include "jdict.h"
#include <windows.h>   // lstrcpynW / _wfopen (네이티브 시험은 shim)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define JD_HEADER_BYTES 64
#define JD_REC_BYTES    12
#define JD_MAX_ROWS     JDICT_MAX_ENTRIES
#define JD_MAX_LINE     1024

typedef struct { char key[JDICT_MAX_KEY_CANDIDATES + 1]; int klen;
                 wchar_t val[JDICT_MAX_VALUE + 1]; int vlen; int line;
                 int cost; int lid, rid; } Row;   // cost: 후보 사전의 셋째 칸 (판 3), 없으면 -1. lid·rid: 품사 (판 4), 없으면 -1

static void Fail(JDictBuildResult *r, int line, const wchar_t *code, const wchar_t *msg, const wchar_t *help) {
    r->line = line;
    lstrcpynW(r->code, code, JDICT_BUILD_CODE);
    lstrcpynW(r->message, msg, JDICT_BUILD_MSG);
    lstrcpynW(r->help, help ? help : L"", JDICT_BUILD_MSG);
}

// `\u{XXXX}` 만 푼다 (사전 값은 글자 그대로도, 이스케이프로도 쓸 수 있다).
//   UTF-8 원본 바이트를 UTF-16 으로 옮기면서 고립 서로게이트·NUL·범위 밖을 거른다.
static bool DecodeValue(const char *s, wchar_t *out, int cap, int *outLen, const wchar_t **why) {
    int n = 0;
    for (const char *p = s; *p; ) {
        unsigned long cp;
        if (p[0] == '\\' && p[1] == 'u' && p[2] == '{') {
            const char *q = p + 3; cp = 0; int digits = 0;
            while (digits < 7 && ((*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'f') || (*q >= 'A' && *q <= 'F'))) {
                cp = cp * 16 + (unsigned long)(*q <= '9' ? *q - '0' : (*q | 0x20) - 'a' + 10);
                q++; digits++;
            }
            if (!digits || *q != '}') { *why = L"a \\u{...} escape is not closed"; return false; }
            p = q + 1;
        } else {
            unsigned char c = (unsigned char)*p;
            int extra;
            if (c < 0x80) { cp = c; extra = 0; }
            else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; extra = 1; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; extra = 2; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; extra = 3; }
            else { *why = L"the value is not valid UTF-8"; return false; }
            p++;
            for (int i = 0; i < extra; i++, p++) {
                if (((unsigned char)*p & 0xC0) != 0x80) { *why = L"the value is not valid UTF-8"; return false; }
                cp = (cp << 6) | ((unsigned long)(unsigned char)*p & 0x3F);
            }
        }
        if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            *why = L"the value has a character that cannot be typed (NUL, a lone surrogate or out of range)";
            return false;
        }
        if (cp >= 0x10000) {
            if (n + 2 > cap) { *why = L"the value is longer than 64 characters"; return false; }
            cp -= 0x10000;
            out[n++] = (wchar_t)(0xD800 + (cp >> 10));
            out[n++] = (wchar_t)(0xDC00 + (cp & 0x3FF));
        } else {
            if (n + 1 > cap) { *why = L"the value is longer than 64 characters"; return false; }
            out[n++] = (wchar_t)cp;
        }
    }
    out[n] = L'\0';
    *outLen = n;
    return n > 0;
}

static int CmpKeyOnly(const Row *x, const Row *y) {
    int n = x->klen < y->klen ? x->klen : y->klen;
    int c = n ? memcmp(x->key, y->key, (size_t)n) : 0;
    if (c) return c;
    return x->klen == y->klen ? 0 : (x->klen < y->klen ? -1 : 1);
}
// 키로 정렬하되 같은 키는 **원본에 적힌 차례**를 지킨다 — 후보 사전의 차례가 곧 후보 차례다.
static int CmpRow(const void *a, const void *b) {
    const Row *x = (const Row *)a, *y = (const Row *)b;
    int c = CmpKeyOnly(x, y);
    if (c) return c;
    return x->line == y->line ? 0 : (x->line < y->line ? -1 : 1);
}

static void Wr32(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
static void Wr16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }

static void TrimEol(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = '\0';
}
// `Key = value` 꼴의 머리부 줄에서 값 (아니면 NULL)
static const char *HeadValue(const char *line, const char *key) {
    size_t k = strlen(key);
    if (strncmp(line, key, k) != 0) return NULL;
    const char *p = line + k;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    return p;
}
// 머리부 문자열이 자리에 들어가는가. 들어가지 않으면 **자르지 않고 거절한다** — 잘린 라이선스는
// 출처를 잃은 라이선스다(실기 2026-09-24: 111자 라이선스가 63자에서 잘려 "…publi" 로 실렸다).
static bool HeadFits(const char *s, int cap) {
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if ((*p & 0xC0) != 0x80 && ++n > cap - 1) return false;
    return true;
}

static void Utf8ToW(const char *s, wchar_t *out, int cap) {   // 머리부 문자열(이름 등)용, ASCII 밖은 그대로 두고 자른다
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && n < cap - 1; ) {
        unsigned long cp; int extra;
        if (*p < 0x80) { cp = *p; extra = 0; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1Fu; extra = 1; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0Fu; extra = 2; }
        else { cp = L'?'; extra = 0; }
        p++;
        for (int i = 0; i < extra; i++, p++) cp = (cp << 6) | ((unsigned long)*p & 0x3F);
        if (cp >= 0x10000 || (cp >= 0xD800 && cp <= 0xDFFF)) cp = L'?';
        out[n++] = (wchar_t)cp;
    }
    out[n] = L'\0';
}

bool JDict_Build(const wchar_t *srcPath, const wchar_t *outPath, JDictBuildResult *res) {
    JDictBuildResult dummy;
    if (!res) res = &dummy;
    memset(res, 0, sizeof(*res));

    FILE *fp = _wfopen(srcPath, L"rb");
    if (!fp) { Fail(res, 0, L"E-DICT-OPEN", L"cannot open the dictionary source", NULL); return false; }

    Row *rows = NULL;
    int nrows = 0, cap = 0, kind = 0, lineno = 0;
    bool sawMagic = false;
    wchar_t name[128] = L"", license[256] = L"", version[64] = L"";
    char line[JD_MAX_LINE];
    bool ok = true;

    while (ok && fgets(line, sizeof(line), fp)) {
        lineno++;
        size_t rawLen = strlen(line);
        if (rawLen == sizeof(line) - 1 && line[rawLen - 1] != '\n') {
            Fail(res, lineno, L"E-DICT-LINE", L"a line is longer than 1023 bytes", L"one entry per line");
            ok = false; break;
        }
        TrimEol(line);
        if (!sawMagic) {   // 첫 뜻있는 줄은 반드시 파일 표시다
            if (line[0] == '\0' || line[0] == '#') continue;
            int v = 0;
            if (sscanf(line, "JamotongData %d", &v) != 1) {
                Fail(res, lineno, L"E-DICT-MAGIC", L"this is not a Jamotong dictionary source",
                     L"the first line must be 'JamotongData 1'");
                ok = false; break;
            }
            if (v != 1) {
                Fail(res, lineno, L"E-DICT-VERSION", L"this source uses a newer dictionary format",
                     L"update Jamotong to build it");
                ok = false; break;
            }
            sawMagic = true;
            continue;
        }
        if (line[0] == '\0' || line[0] == '#') continue;

        const char *v;
        if ((v = HeadValue(line, "Type")) != NULL) {
            if (strcmp(v, "sequence") == 0) kind = JDICT_KIND_SEQUENCE;
            else if (strcmp(v, "candidates") == 0) kind = JDICT_KIND_CANDIDATES;
            else {
                Fail(res, lineno, L"E-DICT-KIND", L"unknown dictionary Type", L"Type = sequence | candidates");
                ok = false; break;
            }
            continue;
        }
        if ((v = HeadValue(line, "Name")) != NULL) {
            if (!HeadFits(v, 128)) { Fail(res, lineno, L"E-DICT-HEAD", L"Name is longer than 127 characters", L"shorten it"); ok = false; break; }
            Utf8ToW(v, name, 128); continue;
        }
        if ((v = HeadValue(line, "License")) != NULL) {
            if (!HeadFits(v, 256)) { Fail(res, lineno, L"E-DICT-HEAD", L"License is longer than 255 characters",
                                         L"name the licence here and keep its full text in a file beside the dictionary"); ok = false; break; }
            Utf8ToW(v, license, 256); continue;
        }
        if ((v = HeadValue(line, "Version")) != NULL) {
            if (!HeadFits(v, 64)) { Fail(res, lineno, L"E-DICT-HEAD", L"Version is longer than 63 characters", L"shorten it"); ok = false; break; }
            Utf8ToW(v, version, 64); continue;
        }
        if ((v = HeadValue(line, "Source")) != NULL)  { continue; }   // 출처는 원본에만 남는다

        // 자료 줄: 키<탭>값
        char *tab = strchr(line, '\t');
        if (!tab || tab == line || tab[1] == '\0') {
            Fail(res, lineno, L"E-DICT-ROW", L"a data row must be <keys> TAB <output>",
                 L"e.g. 'ka' then a tab then the letters it types");
            ok = false; break;
        }
        *tab = '\0';
        // 키 쪽도 값과 같은 표기를 쓴다: 글자를 그대로 적거나 `\u{...}` 로 적는다 (§6.4 의 읽기는
        // 가나·한글이라 이스케이프가 필요하다). 푼 뒤 다시 UTF-8 로 담는다.
        const int maxKey = JDict_MaxKeyBytes(kind);   // 후보 사전의 읽기는 96바이트까지 (판 2)
        wchar_t kw[JDICT_MAX_KEY_CANDIDATES + 2];
        int kwlen = 0;
        const wchar_t *kwhy = NULL;
        if (!DecodeValue(line, kw, maxKey, &kwlen, &kwhy)) {
            Fail(res, lineno, L"E-DICT-KEY", kwhy ? kwhy : L"the typed side cannot be used", NULL);
            ok = false; break;
        }
        char kbuf[JDICT_MAX_KEY_CANDIDATES + 1];
        int klen = 0;
        for (int i = 0; i < kwlen && klen >= 0; i++) {
            unsigned long cp = (unsigned long)kw[i];
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < kwlen && kw[i+1] >= 0xDC00 && kw[i+1] <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + ((unsigned long)kw[++i] - 0xDC00);
            }
            if (cp < 0x80) { if (klen + 1 > maxKey) { klen = -1; break; } kbuf[klen++] = (char)cp; }
            else if (cp < 0x800) { if (klen + 2 > maxKey) { klen = -1; break; }
                kbuf[klen++] = (char)(0xC0 | (cp >> 6)); kbuf[klen++] = (char)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { if (klen + 3 > maxKey) { klen = -1; break; }
                kbuf[klen++] = (char)(0xE0 | (cp >> 12)); kbuf[klen++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                kbuf[klen++] = (char)(0x80 | (cp & 0x3F)); }
            else { if (klen + 4 > maxKey) { klen = -1; break; }
                kbuf[klen++] = (char)(0xF0 | (cp >> 18)); kbuf[klen++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                kbuf[klen++] = (char)(0x80 | ((cp >> 6) & 0x3F)); kbuf[klen++] = (char)(0x80 | (cp & 0x3F)); }
        }
        if (klen < 0) {
            Fail(res, lineno, L"E-DICT-KEY",
                 kind == JDICT_KIND_CANDIDATES ? L"the reading is longer than 96 bytes"
                                               : L"the typed side is longer than 32 bytes", NULL);
            ok = false; break;
        }
        kbuf[klen] = '\0';
        memcpy(line, kbuf, (size_t)klen + 1);
        if (kind == JDICT_KIND_SEQUENCE) {   // 친 글쇠열이므로 ASCII 만 (후보 사전의 읽기는 아니다)
            for (int i = 0; i < klen; i++) {
                unsigned char c = (unsigned char)line[i];
                if (c < 0x21 || c > 0x7E) {
                    Fail(res, lineno, L"E-DICT-KEY", L"the typed side takes printable ASCII without spaces",
                         L"this is what is pressed on the keyboard");
                    ok = false; break;
                }
            }
            if (!ok) break;
        }
        if (nrows >= cap) {
            // 두 배씩 늘리되 **한도에서 멈춘다**. 넘겨 잡고 거절하면 한도의 절반(262144)에서 걸려,
            // 문서가 말하는 50만을 실제로는 못 굽는다 (2026-09-24 실측으로 드러난 결함).
            int ncap = cap ? cap * 2 : 1024;
            if (ncap > JD_MAX_ROWS) ncap = JD_MAX_ROWS;
            if (ncap <= nrows) { Fail(res, lineno, L"E-DICT-LIMIT", L"too many entries (max 500000)", NULL); ok = false; break; }
            Row *nr = (Row *)realloc(rows, (size_t)ncap * sizeof(Row));
            if (!nr) { Fail(res, lineno, L"E-DICT-MEMORY", L"out of memory", NULL); ok = false; break; }
            rows = nr; cap = ncap;
        }
        Row *row = &rows[nrows];
        memcpy(row->key, line, (size_t)klen + 1);
        row->klen = klen;
        row->line = lineno;
        const wchar_t *why = NULL;
        // 후보 사전은 셋째 칸에 비용을 둘 수 있다 (판 3): 읽기<탭>표기<탭>비용, 0..65535, 작을수록 흔하다
        row->cost = -1;
        row->lid = row->rid = -1;
        char *tab2 = strchr(tab + 1, '\t');
        if (tab2) {
            *tab2 = '\0';
            // 판 4 (RFC-0022): 비용 뒤에 왼쪽·오른쪽 품사 id 두 칸 (Mozc 의 lid·rid) — 연결 비용으로 문장을 가른다
            char *tab3 = strchr(tab2 + 1, '\t');
            if (tab3) {
                *tab3 = '\0';
                char *tab4 = strchr(tab3 + 1, '\t');
                long l = -1, r = -1;
                if (tab4) {
                    *tab4 = '\0';
                    char *e1 = NULL, *e2 = NULL;
                    l = strtol(tab3 + 1, &e1, 10); r = strtol(tab4 + 1, &e2, 10);
                    if (e1 == tab3 + 1 || *e1 || e2 == tab4 + 1 || *e2) l = r = -1;
                }
                if (kind != JDICT_KIND_CANDIDATES || l < 0 || l > 65535 || r < 0 || r > 65535) {
                    Fail(res, lineno, L"E-DICT-ROW", L"the fourth and fifth fields are part-of-speech ids: whole numbers 0..65535",
                         L"candidates: <reading> TAB <output> TAB <cost> [TAB <left id> TAB <right id>]");
                    ok = false; break;
                }
                row->lid = (int)l; row->rid = (int)r;
            }
            const char *c = tab2 + 1;
            long cv = 0; int digits = 0;
            for (; *c >= '0' && *c <= '9' && cv <= JDICT_MAX_COST; c++, digits++) cv = cv * 10 + (*c - '0');
            if (kind != JDICT_KIND_CANDIDATES || digits == 0 || *c != '\0' || cv > JDICT_MAX_COST) {
                Fail(res, lineno, L"E-DICT-ROW",
                     kind != JDICT_KIND_CANDIDATES ? L"a sequence row has two fields: <keys> TAB <output>"
                                                   : L"the third field is a cost: a whole number 0..65535",
                     L"candidates: <reading> TAB <output> [TAB <cost>]");
                ok = false; break;
            }
            row->cost = (int)cv;
        }
        if (!DecodeValue(tab + 1, row->val, JDICT_MAX_VALUE, &row->vlen, &why)) {
            Fail(res, lineno, L"E-DICT-VALUE", why ? why : L"the output side cannot be used", NULL);
            ok = false; break;
        }
        nrows++;
    }
    fclose(fp);

    if (ok && !sawMagic) Fail(res, 0, L"E-DICT-MAGIC", L"the source is empty",
                              L"the first line must be 'JamotongData 1'"), ok = false;
    if (ok && kind == 0) Fail(res, 0, L"E-DICT-KIND", L"the source does not say its Type", L"Type = sequence"), ok = false;
    if (ok && nrows == 0) Fail(res, 0, L"E-DICT-EMPTY", L"the source has no entries",
                               L"write one 'keys TAB output' line per entry"), ok = false;

    if (ok) {
        qsort(rows, (size_t)nrows, sizeof(Row), CmpRow);
        if (kind != JDICT_KIND_CANDIDATES)   // 후보 사전은 같은 키가 여러 줄인 것이 정상이다
            for (int i = 1; i < nrows; i++)
                if (CmpKeyOnly(&rows[i - 1], &rows[i]) == 0) {
                    Fail(res, rows[i].line, L"E-DICT-DUP", L"this key is defined twice", L"keep one of the two rows");
                    ok = false; break;
                }
    }

    // 비용은 전부 있거나 전부 없거나 — 반만 있으면 엔진이 고르는 길이 엉뚱해진다
    bool costs = false;
    if (ok && kind == JDICT_KIND_CANDIDATES && nrows > 0) {
        costs = rows[0].cost >= 0;
        for (int i = 1; i < nrows; i++)
            if ((rows[i].cost >= 0) != costs) {
                Fail(res, rows[i].line, L"E-DICT-ROW", L"some rows have a cost and some do not",
                     L"give every row a cost, or none");
                ok = false; break;
            }
    }
    // 품사도 전부 있거나 전부 없거나, 그리고 비용과 함께만 (판 4)
    bool pos = false;
    if (ok && costs && nrows > 0) {
        pos = rows[0].lid >= 0;
        for (int i = 1; i < nrows; i++)
            if ((rows[i].lid >= 0) != pos) {
                Fail(res, rows[i].line, L"E-DICT-ROW", L"some rows have part-of-speech ids and some do not",
                     L"give every row both ids, or none");
                ok = false; break;
            }
    } else if (ok && nrows > 0 && rows[0].lid >= 0) {
        Fail(res, rows[0].line, L"E-DICT-ROW", L"part-of-speech ids need a cost", L"<reading> TAB <output> TAB <cost> TAB <left id> TAB <right id>");
        ok = false;
    }
    unsigned keyBytes = 0, valBytes = 0, maxK = 0, maxV = 0;
    if (ok) {
        for (int i = 0; i < nrows; i++) {
            keyBytes += (unsigned)rows[i].klen;
            valBytes += (unsigned)rows[i].vlen * 2u;
            if ((unsigned)rows[i].klen > maxK) maxK = (unsigned)rows[i].klen;
            if ((unsigned)rows[i].vlen > maxV) maxV = (unsigned)rows[i].vlen;
        }
    }

    if (!ok) { free(rows); return false; }

    // ── 이진 조립 (메모리에 다 만들고 한 번에 쓴다 — 반쯤 구운 파일을 남기지 않는다) ──
    unsigned offIndex = JD_HEADER_BYTES;
    unsigned offKeys  = offIndex + (unsigned)nrows * JD_REC_BYTES;
    unsigned offVals  = (offKeys + keyBytes + 1u) & ~1u;
    unsigned offMeta  = offVals + valBytes;
    unsigned metaLen  = 2u + (unsigned)wcslen(name) * 2u + 2u + (unsigned)wcslen(license) * 2u + 2u + (unsigned)wcslen(version) * 2u;
    unsigned offCosts = offMeta + metaLen;            // 판 3: 꼬리 뒤 (짝수 자리 — 위가 모두 2바이트 단위다)
    unsigned offPos   = offCosts + (costs ? (unsigned)nrows * 2u : 0u);   // 판 4: 비용 뒤에 항목마다 lid·rid
    unsigned total    = offPos + (pos ? (unsigned)nrows * 4u : 0u);

    unsigned char *buf = (unsigned char *)calloc(1, total);
    if (!buf) { free(rows); Fail(res, 0, L"E-DICT-MEMORY", L"out of memory", NULL); return false; }

    memcpy(buf, "JMTDICT\0", 8);
    // 그 파일이 정말 필요로 하는 판만 적는다 — 32바이트를 넘는 키가 없으면 판 1 이라, 옛 자모통도
    // 이 사전을 그대로 읽는다. 넘는 키가 있으면 판 2 이고 옛 자모통은 분명히 거절한다.
    //   비용을 실으면 판 3 이다.
    Wr32(buf + 8, pos ? 4u : costs ? 3u : maxK > (unsigned)JDICT_MAX_KEY ? 2u : 1u);
    Wr32(buf + 12, (unsigned)kind);
    Wr32(buf + 16, (unsigned)nrows);
    Wr32(buf + 20, offIndex);
    Wr32(buf + 24, offKeys);
    Wr32(buf + 28, offVals);
    Wr32(buf + 32, keyBytes);
    Wr32(buf + 36, valBytes);
    Wr32(buf + 40, maxK);
    Wr32(buf + 44, maxV);
    Wr32(buf + 48, (kind == JDICT_KIND_SEQUENCE ? JDICT_FLAG_ASCII_KEYS : 0u) | (costs ? JDICT_FLAG_COSTS : 0u) | (pos ? JDICT_FLAG_POS : 0u));
    Wr32(buf + 56, total);

    unsigned ko = 0, vo = 0;
    for (int i = 0; i < nrows; i++) {
        unsigned char *rec = buf + offIndex + (size_t)i * JD_REC_BYTES;
        Wr32(rec, ko);
        Wr32(rec + 4, vo);
        Wr16(rec + 8, (unsigned)rows[i].klen);
        Wr16(rec + 10, (unsigned)rows[i].vlen);
        memcpy(buf + offKeys + ko, rows[i].key, (size_t)rows[i].klen);
        for (int c = 0; c < rows[i].vlen; c++) Wr16(buf + offVals + vo + (unsigned)c * 2u, (unsigned)rows[i].val[c]);
        ko += (unsigned)rows[i].klen;
        vo += (unsigned)rows[i].vlen * 2u;
    }
    unsigned at = offMeta;
    const wchar_t *metas[3] = { name, license, version };
    for (int f = 0; f < 3; f++) {
        unsigned len = (unsigned)wcslen(metas[f]);
        Wr16(buf + at, len);
        at += 2;
        for (unsigned i = 0; i < len; i++) Wr16(buf + at + i * 2u, (unsigned)metas[f][i]);
        at += len * 2u;
    }
    if (costs)
        for (int i = 0; i < nrows; i++) Wr16(buf + offCosts + (unsigned)i * 2u, (unsigned)rows[i].cost);
    if (pos)
        for (int i = 0; i < nrows; i++) {
            Wr16(buf + offPos + (unsigned)i * 4u, (unsigned)rows[i].lid);
            Wr16(buf + offPos + (unsigned)i * 4u + 2u, (unsigned)rows[i].rid);
        }
    Wr32(buf + 52, JDict_Crc32(buf + JD_HEADER_BYTES, total - JD_HEADER_BYTES));

    FILE *out = _wfopen(outPath, L"wb");
    bool wrote = out && fwrite(buf, 1, total, out) == total;
    if (out) wrote = (fclose(out) == 0) && wrote;
    if (!wrote) {
        _wremove(outPath);
        Fail(res, 0, L"E-DICT-WRITE", L"cannot write the dictionary file",
             L"check that the folder exists and is writable");
    }
    res->count = nrows;
    res->maxKeyLen = (int)maxK;
    res->maxValLen = (int)maxV;
    free(buf);
    free(rows);
    return wrote;
}

// ── 연결 비용 파일 (.jdc, RFC-0022) ───────────────────────────────────────────────────
// id.def 한 줄의 품사("助詞,格助詞,…", UTF-8)가 어느 종류인가
static unsigned char ConnClassOf(const char *f) {
    static const char *const func[] = {
        "\xE5\x8A\xA9\xE8\xA9\x9E,",                                           // 助詞
        "\xE5\x8A\xA9\xE5\x8B\x95\xE8\xA9\x9E,",                               // 助動詞
        "\xE5\x8B\x95\xE8\xA9\x9E,\xE9\x9D\x9E\xE8\x87\xAA\xE7\xAB\x8B,",       // 動詞,非自立
        "\xE5\x8B\x95\xE8\xA9\x9E,\xE6\x8E\xA5\xE5\xB0\xBE,",                   // 動詞,接尾
        "\xE5\xBD\xA2\xE5\xAE\xB9\xE8\xA9\x9E,\xE9\x9D\x9E\xE8\x87\xAA\xE7\xAB\x8B,",   // 形容詞,非自立
        "\xE5\xBD\xA2\xE5\xAE\xB9\xE8\xA9\x9E,\xE6\x8E\xA5\xE5\xB0\xBE,",               // 形容詞,接尾
        "\xE5\x90\x8D\xE8\xA9\x9E,\xE6\x8E\xA5\xE5\xB0\xBE,",                   // 名詞,接尾
        "\xE8\xA8\x98\xE5\x8F\xB7,\xE5\x8F\xA5\xE7\x82\xB9,",                   // 記号,句点
        "\xE8\xA8\x98\xE5\x8F\xB7,\xE8\xAA\xAD\xE7\x82\xB9,",                   // 記号,読点
        "\xE8\xA8\x98\xE5\x8F\xB7,\xE6\x8B\xAC\xE5\xBC\xA7\xE9\x96\x89,",       // 記号,括弧閉
    };
    for (size_t i = 0; i < sizeof func / sizeof func[0]; i++) if (!strncmp(f, func[i], strlen(func[i]))) return 1;   // JCONN_CLASS_FUNC
    if (!strncmp(f, "\xE6\x8E\xA5\xE9\xA0\xAD\xE8\xA9\x9E,", 10)) return 2;      // 接頭詞 — JCONN_CLASS_PREFIX
    return 0;
}
bool JConn_Build(const wchar_t *srcPath, const wchar_t *outPath, int step, JDictBuildResult *res) {
    return JConn_BuildEx(srcPath, NULL, outPath, step, res);
}
bool JConn_BuildEx(const wchar_t *srcPath, const wchar_t *idDefPath, const wchar_t *outPath, int step, JDictBuildResult *res) {
    memset(res, 0, sizeof *res);
    if (step < 1 || step > 1024) { Fail(res, 0, L"E-CONN-STEP", L"the step must be 1..1024", NULL); return false; }
    FILE *fp = _wfopen(srcPath, L"rb");
    if (!fp) { Fail(res, 0, L"E-CONN-OPEN", L"cannot open the connection table", NULL); return false; }
    long n = -1;
    if (fscanf(fp, "%ld", &n) != 1 || n < 1 || n > 8192) {
        fclose(fp);
        Fail(res, 1, L"E-CONN-SIZE", L"the first line must be the number of ids (1..8192)", L"Mozc's connection_single_column.txt");
        return false;
    }
    size_t cells = (size_t)n * (size_t)n, body = cells + (idDefPath ? (size_t)n : 0), total = 32 + body;
    unsigned char *buf = (unsigned char *)calloc(1, total);
    if (!buf) { fclose(fp); Fail(res, 0, L"E-CONN-MEMORY", L"out of memory", NULL); return false; }
    for (size_t i = 0; i < cells; i++) {
        long v;
        if (fscanf(fp, "%ld", &v) != 1 || v < 0) {
            free(buf); fclose(fp);
            Fail(res, (int)(i + 2), L"E-CONN-ROW", L"the table ends early or holds a value that is not a cost", NULL);
            return false;
        }
        long q = v / step;
        buf[32 + i] = (unsigned char)(q > 255 ? 255 : q);
    }
    fclose(fp);
    if (idDefPath) {   // 품사 종류 (판 2)
        FILE *fd = _wfopen(idDefPath, L"rb");
        if (!fd) { free(buf); Fail(res, 0, L"E-CONN-IDDEF", L"cannot open the part-of-speech list", L"Mozc's id.def"); return false; }
        char line[512];
        long seen = 0;
        while (fgets(line, sizeof line, fd)) {
            char *end = NULL;
            long id = strtol(line, &end, 10);
            if (end == line || *end != ' ') continue;
            if (id < 0 || id >= n) { fclose(fd); free(buf); Fail(res, 0, L"E-CONN-IDDEF", L"the part-of-speech list names an id the table does not have", NULL); return false; }
            buf[32 + cells + (size_t)id] = ConnClassOf(end + 1);
            seen++;
        }
        fclose(fd);
        if (seen != n) { free(buf); Fail(res, 0, L"E-CONN-IDDEF", L"the part-of-speech list and the table differ in size", L"take id.def and connection_single_column.txt from the same Mozc"); return false; }
    }
    memcpy(buf, "JMTCONN\0", 8);
    Wr32(buf + 8, idDefPath ? 2u : 1u);
    Wr32(buf + 12, (unsigned)n);
    Wr32(buf + 16, (unsigned)step);
    Wr32(buf + 20, JDict_Crc32(buf + 32, (unsigned long)body));
    Wr32(buf + 24, (unsigned)total);
    FILE *out = _wfopen(outPath, L"wb");
    bool wrote = out && fwrite(buf, 1, total, out) == total;
    if (out) wrote = (fclose(out) == 0) && wrote;
    free(buf);
    if (!wrote) { _wremove(outPath); Fail(res, 0, L"E-CONN-WRITE", L"cannot write the connection file", NULL); return false; }
    res->count = (int)n;
    return true;
}
