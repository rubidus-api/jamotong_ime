// dict_import.c — 남의 사전 자료를 우리 사전 원본(.jdt)으로 바꾼다. 도구(관리 앱·CLI)에만 들어간다.
//   자료 자체는 배포하지 않는다 — 사용자가 고른 파일을 형식만 바꿔 준다 (RFC-0016 §6.4).
#include "dict_import.h"
#include "jdict.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMP_MAX_LINE 4096

// 바이트 문자열 복사 (CRT 마다 이름이 달라 여기서 한 번 만든다)
static char *DupStr(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

typedef struct { char *key; char *val; long cost; int order; } Entry;

static int CmpEntry(const void *a, const void *b) {   // 키로 묶고, 같은 키 안에서는 싼 것부터
    const Entry *x = (const Entry *)a, *y = (const Entry *)b;
    int c = strcmp(x->key, y->key);
    if (c) return c;
    if (x->cost != y->cost) return x->cost < y->cost ? -1 : 1;
    return x->order - y->order;
}
static int CmpCost(const void *a, const void *b) {    // 한도를 줄 때: 싼 것부터 남긴다
    const Entry *x = (const Entry *)a, *y = (const Entry *)b;
    if (x->cost != y->cost) return x->cost < y->cost ? -1 : 1;
    return x->order - y->order;
}

static bool IsInt(const char *s) {
    if (!*s) return false;
    for (const char *p = s; *p; p++) if (*p < '0' || *p > '9') return false;
    return true;
}
static void Fail(DictImportResult *r, const wchar_t *why) { lstrcpynW(r->error, why, 200); }

// UTF-8 한 줄이 사전 안에서 차지할 길이(UTF-16 단위) — 값 쪽 한도가 이 단위다. 잘못된 UTF-8 은 -1.
// 키 쪽 한도는 컴파일러와 똑같이 **UTF-8 바이트 수**로 잰다: 변환기가 컴파일러가 거절할 줄을 내놓으면
// 안 된다(그 줄에서 `--build-dict` 가 통째로 멈춘다).
static int Utf16Len(const char *s) {
    int units = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; ) {
        int n; unsigned cp;
        if (*p < 0x80)        { n = 1; cp = *p; }
        else if ((*p & 0xE0) == 0xC0) { n = 2; cp = *p & 0x1Fu; }
        else if ((*p & 0xF0) == 0xE0) { n = 3; cp = *p & 0x0Fu; }
        else if ((*p & 0xF8) == 0xF0) { n = 4; cp = *p & 0x07u; }
        else return -1;
        for (int i = 1; i < n; i++) { if ((p[i] & 0xC0) != 0x80) return -1; cp = (cp << 6) | (p[i] & 0x3Fu); }
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return -1;
        units += cp > 0xFFFF ? 2 : 1;
        p += n;
    }
    return units;
}

// 머리부 한 줄에 쓸 수 있게 다듬는다: 줄바꿈·탭은 사이띄개로, 길이는 한 줄 안에.
static void OneLine(const wchar_t *in, wchar_t *out, int cap) {
    int n = 0;
    for (const wchar_t *p = in; *p && n < cap - 1; p++)
        out[n++] = (*p == L'\n' || *p == L'\r' || *p == L'\t') ? L' ' : *p;
    out[n] = 0;
}

bool DictImport_Run(const wchar_t *srcPath, const wchar_t *outPath, int limit,
                    const DictImportMeta *meta, DictImportResult *res) {
    DictImportResult dummy;
    if (!res) res = &dummy;
    memset(res, 0, sizeof *res);

    // 컴파일러가 거절할 머리부를 쓰지 않는다 — 사전이 실을 수 있는 이름/라이선스는 63글자까지다.
    if (meta && meta->name && wcslen(meta->name) > 127) { Fail(res, L"the name is longer than 127 characters"); return false; }
    if (meta && meta->license && wcslen(meta->license) > 255) {
        Fail(res, L"the licence line is longer than 255 characters - keep the full text in a file beside the dictionary");
        return false;
    }

    FILE *in = _wfopen(srcPath, L"rb");
    if (!in) { Fail(res, L"cannot open the source file"); return false; }

    Entry *v = NULL;
    int n = 0, cap = 0;
    char line[IMP_MAX_LINE];
    bool ok = true;

    while (fgets(line, sizeof line, in)) {
        size_t len = strlen(line);
        while (len && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        if (!len || line[0] == '#') continue;
        res->rows++;

        char *f[8]; int nf = 0;
        for (char *p = line; nf < 8; ) {
            f[nf++] = p;
            char *t = strchr(p, '\t');
            if (!t) break;
            *t = 0; p = t + 1;
        }
        const char *key = NULL, *val = NULL;
        long cost = 0;
        if (nf >= 5 && IsInt(f[1]) && IsInt(f[2]) && IsInt(f[3])) {   // mozc 꼴
            key = f[0]; cost = strtol(f[3], NULL, 10); val = f[4];
        } else if (nf == 2) {                                        // 두 칸짜리
            key = f[0]; val = f[1]; cost = 0;
        } else { res->skipped++; continue; }

        // 컴파일러가 받는 한도 안에서만: 후보 사전의 읽기는 UTF-8 96바이트, 값은 64글자.
        // 깨진 UTF-8 도 여기서 걸린다(Utf16Len 이 -1).
        int kl = Utf16Len(key), vl = Utf16Len(val);
        if (kl <= 0 || vl <= 0 || strlen(key) > (size_t)JDict_MaxKeyBytes(JDICT_KIND_CANDIDATES)
            || vl > JDICT_MAX_VALUE) { res->skipped++; continue; }
        if (strchr(val, '\t')) { res->skipped++; continue; }

        if (n >= cap) {
            int nc = cap ? cap * 2 : 4096;
            Entry *nv = (Entry *)realloc(v, (size_t)nc * sizeof(Entry));
            if (!nv) { Fail(res, L"out of memory"); ok = false; break; }
            v = nv; cap = nc;
        }
        v[n].key = DupStr(key); v[n].val = DupStr(val);
        v[n].cost = cost; v[n].order = n;
        if (!v[n].key || !v[n].val) { Fail(res, L"out of memory"); ok = false; break; }
        n++;
    }
    fclose(in);
    if (ok && n == 0) { Fail(res, L"the source has no usable rows"); ok = false; }

    if (ok) qsort(v, (size_t)n, sizeof(Entry), CmpEntry);
    // 같은 (읽기, 표기)가 여러 번 오는 자료가 많다 (품사·연결비용만 다른 줄). 후보창은 16칸뿐이라
    // 중복을 두면 쓸 후보가 밀려난다. 정렬이 싼 것을 앞에 두었으므로 **읽기 묶음 안에서 처음 나온
    // 표기만** 남긴다 — 같은 읽기라도 비용이 다르면 서로 떨어져 있어 이웃 비교로는 못 잡는다.
    if (ok && n > 1) {
        int w = 0;
        for (int i = 0; i < n; ) {
            int j = i;
            while (j < n && strcmp(v[j].key, v[i].key) == 0) j++;   // [i, j) = 읽기 하나
            int keptHere = 0;
            for (int k = i; k < j; k++) {
                bool dup = false;
                for (int m = 0; m < keptHere; m++)
                    if (strcmp(v[k].val, v[w - keptHere + m].val) == 0) { dup = true; break; }
                if (dup) { free(v[k].key); free(v[k].val); res->skipped++; }
                else { v[w++] = v[k]; keptHere++; }
            }
            i = j;
        }
        n = w;
    }

    // 한도는 **중복을 지운 뒤** 건다. 먼저 걸면 버려질 중복이 자리를 차지해 요청한 수보다 적게 남는다.
    bool trimmed = false;
    if (ok && limit > 0 && n > limit) {     // 싼 것부터 남긴다
        qsort(v, (size_t)n, sizeof(Entry), CmpCost);
        for (int i = limit; i < n; i++) { free(v[i].key); free(v[i].val); }
        n = limit;
        trimmed = true;
    }
    if (ok && n > JDICT_MAX_ENTRIES) {
        qsort(v, (size_t)n, sizeof(Entry), CmpCost);
        for (int i = JDICT_MAX_ENTRIES; i < n; i++) { free(v[i].key); free(v[i].val); }
        n = JDICT_MAX_ENTRIES;
        trimmed = true;
    }
    if (ok && trimmed) qsort(v, (size_t)n, sizeof(Entry), CmpEntry);   // 한도를 건 뒤 다시 키 차례로

    if (ok) {
        FILE *out = _wfopen(outPath, L"wb");
        if (!out) { Fail(res, L"cannot write the dictionary source"); ok = false; }
        else {
            const wchar_t *base = srcPath;
            for (const wchar_t *q = srcPath; *q; q++) if (*q == L'\\' || *q == L'/') base = q + 1;
            wchar_t nameLine[128], licLine[256];
            OneLine(meta && meta->name ? meta->name : L"imported", nameLine, 128);
            OneLine(meta && meta->license ? meta->license : L"", licLine, 256);
            fprintf(out, "JamotongData 1\nType = candidates\n");
            fprintf(out, "Name = %ls\n", nameLine[0] ? nameLine : L"imported");
            if (licLine[0]) fprintf(out, "License = %ls\n", licLine);
            fprintf(out, "Source = %ls (converted by jamotong --import-dict)\n", base);
            fprintf(out, "# The data keeps the licence of the file it came from - write it here before sharing.\n");
            for (int i = 0; i < n; i++) fprintf(out, "%s\t%s\n", v[i].key, v[i].val);
            if (fclose(out) != 0) { Fail(res, L"cannot write the dictionary source"); ok = false; }
            if (!ok) _wremove(outPath);
        }
    }
    res->kept = ok ? n : 0;
    for (int i = 0; i < n; i++) { free(v[i].key); free(v[i].val); }
    free(v);
    return ok;
}
