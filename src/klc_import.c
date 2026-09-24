// klc_import.c — MSKLC `.klc` 를 정적 `.jmt` 로 (RFC-0011a). 도구에만 들어간다.
//   .klc 는 탭으로 나눈 표다:
//     KBD   kbdxx   "이름"
//     SHIFTSTATE 0 1 2 6 7            ← 뒤 LAYOUT 의 각 칸이 어느 면인지
//     LAYOUT
//     02    1    0    1    0021   -1   -1   -1      ← 스캔코드 VK Cap 면들…
//   면 값: `-1` 없음, 네 자리 16진(코드포인트), 글자 하나, `%%` 합자, 끝의 `@` 는 데드키.
#include "klc_import.h"
#include "klay.h"
#include "version.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KLC_MAX_LINE 1024

static void Fail(KlcImportResult *r, const wchar_t *why) { lstrcpynW(r->error, why, 200); }

// 탭/공백으로 나눈 다음 칸. 없으면 NULL.
static wchar_t *NextField(wchar_t **p) {
    wchar_t *s = *p;
    while (*s == L' ' || *s == L'\t') s++;
    if (!*s || *s == L'\n' || *s == L'\r') return NULL;
    wchar_t *b = s;
    while (*s && *s != L' ' && *s != L'\t' && *s != L'\n' && *s != L'\r') s++;
    if (*s) { *s = L'\0'; s++; }
    *p = s;
    return b;
}

// 면 한 칸을 글자로. 못 옮기면 0 이고 왜인지 알린다.
static wchar_t CellChar(const wchar_t *cell, bool *dead, bool *liga) {
    *dead = false; *liga = false;
    if (!cell || !cell[0]) return 0;
    if (!wcscmp(cell, L"-1")) return 0;
    if (!wcscmp(cell, L"%%")) { *liga = true; return 0; }
    size_t n = wcslen(cell);
    wchar_t buf[16];
    lstrcpynW(buf, cell, 16);
    if (n > 1 && buf[n-1] == L'@') { *dead = true; buf[n-1] = L'\0'; n--; }
    if (n == 1) return buf[0];
    // 16진 코드포인트 (보통 네 자리)
    wchar_t *end = NULL;
    unsigned long v = wcstoul(buf, &end, 16);
    if (end && !*end && v > 0 && v < 0x10000) return (wchar_t)v;
    return 0;
}

bool KlcImport_Run(const wchar_t *srcPath, const wchar_t *outPath, KlcImportResult *res) {
    KlcImportResult dummy;
    if (!res) res = &dummy;
    memset(res, 0, sizeof *res);

    FILE *in = _wfopen(srcPath, L"rt, ccs=UTF-8");
    if (!in) in = _wfopen(srcPath, L"rt");          // UTF-16/ANSI 로 저장된 파일도 있다
    if (!in) { Fail(res, L"cannot open the .klc file"); return false; }

    // 기본 면과 Shift 면이 LAYOUT 의 몇 번째 칸인지 (SHIFTSTATE 줄이 정한다)
    int colBase = 0, colShift = 1, nStates = 0;
    bool inLayout = false, sawLayout = false;
    wchar_t line[KLC_MAX_LINE];
    // Map 을 모아 둔다: 인덱스 = US 글자, 값 = 이 자판이 낼 글자
    static wchar_t out[256];
    memset(out, 0, sizeof out);

    while (fgetws(line, KLC_MAX_LINE, in)) {
        wchar_t *p = line;
        while (*p == L' ' || *p == L'\t') p++;
        if (*p == L'/' && p[1] == L'/') continue;
        if (!wcsncmp(p, L"KBD", 3)) {
            wchar_t *q = p + 3;
            NextField(&q);                      // 내부 이름 (kbdxx)
            wchar_t *desc = q;
            while (*desc && *desc != L'"') desc++;
            if (*desc == L'"') {
                desc++;
                wchar_t *e = desc;
                while (*e && *e != L'"') e++;
                *e = L'\0';
                lstrcpynW(res->name, desc, 64);
            }
            continue;
        }
        if (!wcsncmp(p, L"SHIFTSTATE", 10)) { nStates = 0; continue; }
        if (!wcsncmp(p, L"LAYOUT", 6)) { inLayout = true; sawLayout = true; continue; }
        if (!wcsncmp(p, L"DEADKEY", 7) || !wcsncmp(p, L"KEYNAME", 7) || !wcsncmp(p, L"LIGATURE", 8) ||
            !wcsncmp(p, L"ENDKBD", 6) || !wcsncmp(p, L"ATTRIBUTES", 10)) { inLayout = false; continue; }

        if (!inLayout) {
            // SHIFTSTATE 목록의 숫자 줄: 0, 1, 2, 6, 7 … 기본(0)·Shift(1) 이 몇 번째인지 센다
            if (sawLayout) continue;
            wchar_t *q = p, *f = NextField(&q);
            if (f && ((f[0] >= L'0' && f[0] <= L'9'))) {
                int st = (int)wcstol(f, NULL, 10);
                if (st == 0) colBase = nStates;
                else if (st == 1) colShift = nStates;
                nStates++;
            }
            continue;
        }

        wchar_t *q = p;
        wchar_t *scTok = NextField(&q);
        wchar_t *vkTok = NextField(&q);
        wchar_t *capTok = NextField(&q);
        if (!scTok || !vkTok || !capTok) continue;
        wchar_t *end = NULL;
        unsigned sc = (unsigned)wcstoul(scTok, &end, 16);
        if (!end || *end) continue;             // 스캔코드가 아니면 표가 끝난 것
        res->rows++;

        wchar_t base = Klay_UsFromScan(sc);     // 이 자리에서 US 자판이 내는 글자
        wchar_t cells[8][16];
        int nc = 0;
        for (; nc < 8; nc++) {
            wchar_t *c = NextField(&q);
            if (!c) break;
            lstrcpynW(cells[nc], c, 16);
        }
        if (!base) { res->skipped += (nc > 0); continue; }   // 우리가 모르는 자리(F키·키패드 등)

        for (int which = 0; which < 2; which++) {
            int col = which ? colShift : colBase;
            if (col < 0 || col >= nc) { res->skipped++; continue; }
            bool dead = false, liga = false;
            wchar_t ch = CellChar(cells[col], &dead, &liga);
            if (dead) { res->deadKeys++; continue; }
            if (liga) { res->ligatures++; continue; }
            wchar_t key = which ? Klay_UsShiftOf(base) : base;
            if (!key || !ch) { if (!ch && !dead && !liga) res->skipped++; continue; }
            if ((unsigned)key < 256) { out[key] = ch; res->mapped++; }
            else res->skipped++;
        }
    }
    fclose(in);

    if (!sawLayout) { Fail(res, L"this file has no LAYOUT table - is it a .klc?"); return false; }
    if (res->mapped == 0) { Fail(res, L"nothing could be mapped from this file"); return false; }
    if (!res->name[0]) {
        const wchar_t *b = srcPath;
        for (const wchar_t *s = srcPath; *s; s++) if (*s == L'\\' || *s == L'/') b = s + 1;
        lstrcpynW(res->name, b, 64);
    }

    FILE *f = _wfopen(outPath, L"wb");
    if (!f) { Fail(res, L"cannot write the layout file"); return false; }
    const wchar_t *sbase = srcPath;
    for (const wchar_t *s = srcPath; *s; s++) if (*s == L'\\' || *s == L'/') sbase = s + 1;
    fprintf(f, "FormatVersion    = 3\n");
    fprintf(f, "Type             = static\n");
    // 지금 이 도구의 판을 적는다 — 앞선 판을 요구해 제 손으로 만든 파일을 못 읽는 일이 없게.
    fprintf(f, "RequiresJamotong = %ls\n", JAMOTONG_VERSION);
    fprintf(f, "Name             = %ls\n", res->name);
    fprintf(f, "# Converted from %ls by jamotong --import-klc.\n", sbase);
    fprintf(f, "# Dead keys, ligatures and the AltGr face are not carried over.\n");
    // 읽기 쉽게 여덟 글쇠씩 한 줄로 (좌변·우변 길이가 같아야 한다)
    wchar_t lhs[16], rhs[16];
    int n = 0;
    bool wrote = true;
    for (int i = 0; i < 256 && wrote; i++) {
        if (!out[i] || out[i] == (wchar_t)i) continue;   // 같은 글자면 적을 것이 없다
        lhs[n] = (wchar_t)i; rhs[n] = out[i]; n++;
        if (n == 8) {
            lhs[n] = rhs[n] = L'\0';
            wrote = fprintf(f, "Map %ls = %ls\n", lhs, rhs) > 0;
            n = 0;
        }
    }
    if (wrote && n > 0) { lhs[n] = rhs[n] = L'\0'; wrote = fprintf(f, "Map %ls = %ls\n", lhs, rhs) > 0; }
    if (fclose(f) != 0 || !wrote) { Fail(res, L"cannot write the layout file"); _wremove(outPath); return false; }
    return true;
}
