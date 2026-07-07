#include "hangul_layout.h"
#include <stdio.h>
#include <string.h>

static JamoType TypeFromChar(wchar_t c) {
    switch (c) {
        case L'C': case L'c': return JAMO_CHO;
        case L'M': case L'm': return JAMO_JUNG;
        case L'T': case L't': return JAMO_JONG;
        default: return JAMO_NONE;
    }
}

static void TrimCrLf(wchar_t *s) {
    size_t n = wcslen(s);
    while (n > 0 && (s[n-1] == L'\n' || s[n-1] == L'\r' || s[n-1] == L' ' || s[n-1] == L'\t')) s[--n] = L'\0';
}

HangulLayout *HangulLayout_LoadFromFile(const wchar_t *path) {
    FILE *fp = _wfopen(path, L"r, ccs=UTF-8");
    if (!fp) return NULL;

    HangulLayout *hl = (HangulLayout*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(HangulLayout));
    if (!hl) { fclose(fp); return NULL; }
    wcscpy_s(hl->name, 64, L"custom");

    wchar_t line[256];
    while (fgetws(line, 256, fp)) {
        TrimCrLf(line);
        // 앞 공백 스킵
        wchar_t *p = line;
        while (*p == L' ' || *p == L'\t') p++;
        if (*p == L'\0' || *p == L'#') continue;   // 빈 줄/주석

        wchar_t keyc = 0, typec = 0;
        int a = 0, b = 0, idx = 0, val = 0;

        if (swscanf(p, L"Name = %63l[^\n]", hl->name) == 1) {
            TrimCrLf(hl->name);
        } else if (swscanf(p, L"Moachigi = %d", &val) == 1) {
            hl->moachigi = (val != 0);
        } else if (swscanf(p, L"Key %lc = %lc%d", &keyc, &typec, &idx) == 3) {
            JamoType t = TypeFromChar(typec);
            if (t != JAMO_NONE && (unsigned)keyc < 128) {
                hl->keymap[(int)keyc].type = t;
                hl->keymap[(int)keyc].index = idx;
            }
        } else if (swscanf(p, L"Combine %lc %d %d = %d", &typec, &a, &b, &val) == 4) {
            JamoType t = TypeFromChar(typec);
            if (t != JAMO_NONE && hl->combineCount < HL_MAX_COMBINE) {
                HangulCombine *c = &hl->combines[hl->combineCount++];
                c->type = t; c->a = a; c->b = b; c->result = val;
            }
        }
        // 알 수 없는 줄은 무시 (향후 확장 여지)
    }
    fclose(fp);
    return hl;
}

void HangulLayout_Free(HangulLayout *hl) {
    if (hl) HeapFree(GetProcessHeap(), 0, hl);
}

int HangulLayout_Combine(const HangulLayout *hl, JamoType type, int a, int b) {
    if (!hl) return -1;
    for (int i = 0; i < hl->combineCount; i++) {
        const HangulCombine *c = &hl->combines[i];
        if (c->type != type) continue;
        if (c->a == a && c->b == b) return c->result;
        if (hl->moachigi && c->a == b && c->b == a) return c->result;   // 모아치기: 순서 무관
    }
    return -1;
}
