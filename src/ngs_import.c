// ngs_import.c — 날개셋 자판 파일(`.key`/`.ist`)을 우리 자판 원본 `.jmt` 로 (RFC-0011b). 도구에만 들어간다.
//
// 파일 꼴 (공개 파일을 읽어 확인한 것; 날개셋은 형식을 공개하지 않는다):
//   `.key`  "NKB5\x1a" <이름길이 1바이트> <이름 UTF-16LE> <글쇠 배열 토막>
//   `.ist`  "NgsIME set v5.0\x1a" … <이름 UTF-16LE> <글쇠 배열 토막> … (뒤에 설정이 더 있다)
//   글쇠 배열 토막:  <00|01> <lo> <hi> 00 00 01 <2바이트> <글쇠값 × (hi-lo+1)> 00
//     lo..hi 는 글쇠가 내는 US 문자의 범위(늘 '!'..'~' = 94자리)이고, 그 차례대로 값이 온다.
//   글쇠값 하나:
//     00 <a> <b>   낱자·글자 한 개.  종류 = a>>3,  번호 = (b<<3) | (a&7)
//                    종류 0=글자(유니코드 코드값) 2=초성 3=중성 4=종성 (10=우리가 모르는 것, 한 바이트 더)
//     79 <a> <b>   뜻을 모르는 값 하나 (세고 넘긴다)
//     01·02 …      날개셋 수식 (조건에 따라 값이 달라지는 자리). 토큰열이다:
//                    01 <2바이트>=값 넣기, 02 <1바이트>=상태 넣기, 그 밖의 한 바이트=셈.
//                    59·66 으로 끝나며, 바로 뒤에 다음 글쇠값(00/01/02)이나 마침표(00)가 온다.
// 마지막 값 뒤의 00 을 확인해야 토막을 제대로 읽은 것이다 — 어긋나면 아무것도 옮기지 않고 거절한다
// (E-NGS-PARSE). 잘못 읽은 자판을 조용히 내놓느니 거절하는 편이 낫다.
//
// 낱자 번호표: 날개셋은 초성과 종성에 **같은 자음 표**를 쓰고(ㄱ=1, ㄴ=12, ㄷ=24 …), 그 사이에
// 옛한글 겹자음이 끼어 있다. 아래 표는 배열이 공개된 자판 파일(세벌식 최종·3-2012)에서 글쇠와
// 낱자를 맞대 확인한 값만 담는다. 표에 없는 코드는 옮기지 않고 세어서 알려 준다.
#include "ngs_import.h"
#include "version.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NGS_MAX_FILE     (4u * 1024u * 1024u)
#define NGS_MAX_KEYS     96
#define NGS_MAX_FORMULA  256        // 수식 한 개가 이보다 길면 잘못 읽은 것이다

// 값 종류 (a>>3)
#define NGS_T_CHAR 0
#define NGS_T_CHO  2
#define NGS_T_JUNG 3
#define NGS_T_JONG 4
#define NGS_T_WIDE 10               // 한 바이트 더 쓰는 값 — 무엇인지 모른다. 세고 넘긴다.
#define NGS_TAG_OTHER 0x79          // 세 바이트짜리 다른 값 — 뜻을 모른다. 세고 넘긴다.

static void Fail(NgsImportResult *r, const wchar_t *why) { lstrcpynW(r->error, why, 200); }

// 자음 코드 → 우리 번호 (초성 0..18 / 종성 1..27). cho < 0 = 초성으로 쓸 수 없는 겹자음.
static const struct { short code; signed char cho; signed char jong; } kCons[] = {
    {  1,  0,  1},   // ㄱ
    {  2,  1,  2},   // ㄲ
    {  7, -1,  3},   // ㄳ
    { 12,  2,  4},   // ㄴ
    { 20, -1,  5},   // ㄵ
    { 23, -1,  6},   // ㄶ
    { 24,  3,  7},   // ㄷ
    { 36,  5,  8},   // ㄹ
    { 37, -1,  9},   // ㄺ
    { 47, -1, 10},   // ㄻ
    { 51, -1, 11},   // ㄼ
    { 58, -1, 12},   // ㄽ
    { 64, -1, 13},   // ㄾ
    { 65, -1, 14},   // ㄿ
    { 66, -1, 15},   // ㅀ
    { 70,  6, 16},   // ㅁ
    { 86,  7, 17},   // ㅂ
    { 94, -1, 18},   // ㅄ
    {109,  9, 19},   // ㅅ
    {118, 10, 20},   // ㅆ
    {138, 11, 21},   // ㅇ
    {161, 12, 22},   // ㅈ
    {171, 14, 23},   // ㅊ
    {176, 15, 24},   // ㅋ
    {177, 16, 25},   // ㅌ
    {179, 17, 26},   // ㅍ
    {185, 18, 27},   // ㅎ
};
// 모음 코드 → 우리 중성 번호 (0..20). 자음과는 다른 표다 (같은 64 라도 ㄾ 와 ㅡ).
static const struct { short code; signed char jung; } kVowel[] = {
    {  1,  0},   // ㅏ
    {  5,  4},   // ㅓ
    {  6,  2},   // ㅑ
    { 10,  3},   // ㅒ
    { 11,  1},   // ㅐ
    { 15,  5},   // ㅔ
    { 16,  6},   // ㅕ
    { 20,  7},   // ㅖ
    { 21,  8},   // ㅗ
    { 34, 12},   // ㅛ
    { 43, 13},   // ㅜ
    { 54, 17},   // ㅠ
    { 64, 18},   // ㅡ
    { 71, 19},   // ㅢ
    { 73, 20},   // ㅣ
};

static int ConsIdx(int code) {
    for (size_t i = 0; i < sizeof kCons / sizeof kCons[0]; i++) if (kCons[i].code == code) return (int)i;
    return -1;
}
static int VowelIdx(int code) {
    for (size_t i = 0; i < sizeof kVowel / sizeof kVowel[0]; i++) if (kVowel[i].code == code) return (int)i;
    return -1;
}

// 수식 한 개의 끝. 못 찾으면 0.
static size_t FormulaEnd(const unsigned char *d, size_t p, size_t n) {
    size_t q = p;
    while (q < n && q - p < NGS_MAX_FORMULA) {
        unsigned char b = d[q];
        if (b == 0x01) { q += 3; continue; }        // 값 넣기
        if (b == 0x02) { q += 2; continue; }        // 상태 넣기
        q++;
        // 끝맺는 셈 뒤에는 다음 글쇠값이나 토막의 마침표가 온다
        if ((b == 0x59 || b == 0x66) && q < n &&
            (d[q] == 0x00 || d[q] == 0x01 || d[q] == 0x02 || d[q] == NGS_TAG_OTHER)) return q;
    }
    return 0;
}

// 글쇠 배열 토막 하나를 읽는다. 끝까지 읽고 마침표(00)까지 확인해야 참.
//   ty/idx 는 자리마다 값 종류와 번호 (수식은 종류 -1).
static bool ReadBlock(const unsigned char *d, size_t n, size_t off, int *lo, int *count, int *ty, int *idx) {
    if (off + 8 > n) return false;
    int l = d[off + 1], h = d[off + 2];
    if (l < 0x20 || h <= l || h > 0x7e || h - l + 1 > NGS_MAX_KEYS) return false;
    size_t p = off + 8;                       // 00 lo hi 00 00 01 + 2바이트(뜻 모름)
    int cnt = h - l + 1;
    for (int k = 0; k < cnt; k++) {
        if (p >= n) return false;
        unsigned char tag = d[p];
        if (tag == 0x00) {
            if (p + 3 > n) return false;
            int t = d[p + 1] >> 3;
            if (t != NGS_T_CHAR && t != NGS_T_CHO && t != NGS_T_JUNG && t != NGS_T_JONG && t != NGS_T_WIDE) return false;
            ty[k] = t;
            idx[k] = (d[p + 2] << 3) | (d[p + 1] & 7);
            p += (t == NGS_T_WIDE) ? 4 : 3;
        } else if (tag == NGS_TAG_OTHER) {
            if (p + 3 > n) return false;
            ty[k] = NGS_T_WIDE; idx[k] = 0;      // 뜻을 모르는 값 — 옮기지 않고 센다
            p += 3;
        } else if (tag == 0x01 || tag == 0x02) {
            size_t e = FormulaEnd(d, p, n);
            if (!e) return false;
            ty[k] = -1; idx[k] = 0;
            p = e;
        } else return false;
    }
    if (p >= n || d[p] != 0x00) return false;  // 마침표가 없으면 잘못 읽은 것이다
    *lo = l; *count = cnt;
    return true;
}

// 토막 바로 앞의 자판 이름 (길이 1바이트 + UTF-16LE). 없으면 거짓.
static bool NameBefore(const unsigned char *d, size_t off, wchar_t *out, size_t cch) {
    // 짧은 쪽부터 본다 — 길게 잡으면 이름 앞의 다른 바이트까지 이름으로 읽는 파일이 있다.
    for (int len = 1; len <= 63; len++) {
        size_t head = (size_t)(2 * len + 1);
        if (head > off) continue;
        size_t at = off - head;
        if (d[at] != (unsigned char)len) continue;
        bool ok = true;
        for (int i = 0; i < len && ok; i++) {
            unsigned w = (unsigned)d[at + 1 + 2 * i] | ((unsigned)d[at + 2 + 2 * i] << 8);
            if (w < 0x20 || w == 0xfeff) ok = false;
        }
        if (!ok) continue;
        int m = len < (int)cch - 1 ? len : (int)cch - 1;
        for (int i = 0; i < m; i++) out[i] = (wchar_t)((unsigned)d[at + 1 + 2 * i] | ((unsigned)d[at + 2 + 2 * i] << 8));
        out[m] = L'\0';
        return true;
    }
    return false;
}

// 넓은 글을 UTF-8 로 적는다 — 자판 파일은 UTF-8 로 읽힌다(klay_src.c). fprintf("%ls") 는
// 한글 이름에서 C 로캘에 걸려 멈추므로 쓰지 않고, 직접 옮긴다.
static bool W8(FILE *f, const wchar_t *fmt, ...) {
    wchar_t w[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vswprintf(w, 512, fmt, ap);
    va_end(ap);
    if (n < 0) return false;
    char u8[2048];
    size_t o = 0;
    for (int i = 0; i < n && o + 4 < sizeof u8; i++) {
        unsigned c = (unsigned)w[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && (unsigned)w[i+1] >= 0xDC00 && (unsigned)w[i+1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + ((unsigned)w[++i] - 0xDC00);
        }
        if (c < 0x80) u8[o++] = (char)c;
        else if (c < 0x800) { u8[o++] = (char)(0xC0 | (c >> 6)); u8[o++] = (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) {
            u8[o++] = (char)(0xE0 | (c >> 12));
            u8[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            u8[o++] = (char)(0x80 | (c & 0x3F));
        } else {
            u8[o++] = (char)(0xF0 | (c >> 18));
            u8[o++] = (char)(0x80 | ((c >> 12) & 0x3F));
            u8[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            u8[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    return fwrite(u8, 1, o, f) == o;
}

bool NgsImport_Run(const wchar_t *srcPath, const wchar_t *outPath, NgsImportResult *res) {
    NgsImportResult dummy;
    if (!res) res = &dummy;
    memset(res, 0, sizeof *res);

    FILE *in = _wfopen(srcPath, L"rb");
    if (!in) { Fail(res, L"cannot open the layout file"); return false; }
    unsigned char *d = (unsigned char*)malloc(NGS_MAX_FILE);
    if (!d) { fclose(in); Fail(res, L"out of memory"); return false; }
    size_t n = fread(d, 1, NGS_MAX_FILE, in);
    fclose(in);
    if (n < 16) { free(d); Fail(res, L"this file is too small to be a Nalgaeset layout"); return false; }

    bool isKey = !memcmp(d, "NKB5\x1a", 5);
    bool isIst = !memcmp(d, "NgsIME set", 10);
    if (!isKey && !isIst) {
        free(d);
        Fail(res, L"not a Nalgaeset layout file (expected a .key or .ist saved by Nalgaeset)");
        return false;
    }

    // 글쇠 배열 토막을 찾는다: 00 <lo> <hi> 00 00 01. 앞에서부터 보되, 끝까지 읽히는 첫 토막을 쓴다.
    int ty[NGS_MAX_KEYS], idx[NGS_MAX_KEYS], lo = 0, cnt = 0;
    bool found = false;
    for (size_t i = 0; i + 8 < n && !found; i++) {
        // 첫 바이트는 파일마다 00 이거나 01 이다 (뜻은 모른다; 나머지 다섯은 늘 같다).
        if (d[i] > 0x01 || d[i + 3] != 0x00 || d[i + 4] != 0x00 || d[i + 5] != 0x01) continue;
        if (ReadBlock(d, n, i, &lo, &cnt, ty, idx)) {
            found = true;
            if (isKey && d[5] && (size_t)(6 + 2 * d[5]) <= n) {
                int len = d[5] < 63 ? d[5] : 63;
                for (int k = 0; k < len; k++) res->name[k] = (wchar_t)((unsigned)d[6 + 2 * k] | ((unsigned)d[7 + 2 * k] << 8));
                res->name[len] = L'\0';
            }
            if (!res->name[0]) NameBefore(d, i, res->name, 64);
        }
    }
    if (!found) {
        free(d);
        Fail(res, L"E-NGS-PARSE: cannot read the key table of this file - it may be from a newer Nalgaeset");
        return false;
    }
    free(d);
    res->keys = cnt;

    if (!res->name[0]) {
        const wchar_t *b = srcPath;
        for (const wchar_t *s = srcPath; *s; s++) if (*s == L'\\' || *s == L'/') b = s + 1;
        lstrcpynW(res->name, b, 64);
    }

    // 낱자가 하나라도 있으면 한글 자판, 아니면 글자만 있는 정적 자판으로 적는다.
    int nJamo = 0, nJong = 0;
    for (int k = 0; k < cnt; k++) {
        if (ty[k] == NGS_T_CHO || ty[k] == NGS_T_JUNG || ty[k] == NGS_T_JONG) nJamo++;
        if (ty[k] == NGS_T_JONG) nJong++;
    }
    res->hangul = nJamo > 0;
    res->sebeol = nJong > 0;

    FILE *f = _wfopen(outPath, L"wb");
    if (!f) { Fail(res, L"cannot write the layout file"); return false; }
    const wchar_t *sbase = srcPath;
    for (const wchar_t *s = srcPath; *s; s++) if (*s == L'\\' || *s == L'/') sbase = s + 1;

    bool ok = true;
    ok = ok && W8(f, L"FormatVersion    = 3\n");
    ok = ok && W8(f, L"Type             = %ls\n", res->hangul ? L"hangul" : L"static");
    if (res->hangul) ok = ok && W8(f, L"Composition      = %ls\n", res->sebeol ? L"sebeol" : L"dubeol");
    // 이 도구의 판을 적는다 — 앞선 판을 요구해 제 손으로 만든 파일을 못 읽는 일이 없게.
    ok = ok && W8(f, L"RequiresJamotong = %ls\n", JAMOTONG_VERSION);
    ok = ok && W8(f, L"Name             = %ls\n", res->name);
    ok = ok && W8(f, L"# Converted from %ls by jamotong --import-ngs.\n", sbase);
    ok = ok && W8(f, L"# Only the key table is carried over: Nalgaeset's automata, options and\n");
    ok = ok && W8(f, L"# formula key values are not part of a jamotong layout.\n");

    wchar_t unk[120] = L"";
    for (int k = 0; k < cnt && ok; k++) {
        wchar_t key = (wchar_t)(lo + k);
        if (ty[k] < 0) { res->formulas++; continue; }
        if (ty[k] == NGS_T_WIDE) { res->unknown++; continue; }
        if (ty[k] == NGS_T_CHAR) {
            if (!res->hangul) {
                wchar_t ch = (wchar_t)idx[k];
                if (ch > L' ' && ch != key) { ok = W8(f, L"Map %lc = %lc\n", key, ch); res->mapped++; }
            } else res->chars++;
            continue;
        }
        int our = -1;
        wchar_t tc = 0;
        if (ty[k] == NGS_T_JUNG) {
            int v = VowelIdx(idx[k]);
            if (v >= 0) { our = kVowel[v].jung; tc = L'M'; }
        } else {
            int c = ConsIdx(idx[k]);
            if (c >= 0) {
                our = (ty[k] == NGS_T_CHO) ? kCons[c].cho : kCons[c].jong;
                tc = (ty[k] == NGS_T_CHO) ? L'C' : L'T';
            }
        }
        if (our < 0) {
            res->unknown++;
            wchar_t one[16];
            swprintf(one, 16, L"%lc%d ", ty[k] == NGS_T_CHO ? L'C' : ty[k] == NGS_T_JUNG ? L'M' : L'T', idx[k]);
            if (wcslen(unk) + wcslen(one) < 119) wcscat(unk, one);
            continue;
        }
        ok = W8(f, L"Key %lc = %lc%d\n", key, tc, our);
        res->mapped++;
    }
    lstrcpynW(res->unknownList, unk, 120);
    if (fclose(f) != 0 || !ok) { Fail(res, L"cannot write the layout file"); _wremove(outPath); return false; }
    if (res->mapped == 0) {
        _wremove(outPath);
        Fail(res, L"nothing could be carried over - every key of this layout is a Nalgaeset formula");
        return false;
    }
    return true;
}
