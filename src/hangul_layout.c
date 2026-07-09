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

// jamo 인덱스 유효 범위 (RFC-0004 P1-2): 초성 0..18, 중성 0..20, 종성 1..27(0=없음은 배정 불가).
// 범위 밖 값은 invalid UTF-16/깨진 음절을 만들므로 "로드 성공인데 입력이 깨짐" 대신 파일을 거부한다.
static bool ValidIdx(JamoType t, int idx) {
    switch (t) {
        case JAMO_CHO:  return idx >= 0 && idx <= 18;
        case JAMO_JUNG: return idx >= 0 && idx <= 20;
        case JAMO_JONG: return idx >= 1 && idx <= 27;
        default: return false;
    }
}

// 첫 오류만 기록(diag NULL 허용). bad=true로 표시.
#define FAIL(msg) do { if (diag && !bad) { diag->line = lineno; \
    lstrcpynW(diag->message, (msg), 160); } bad = true; } while (0)

HangulLayout *HangulLayout_LoadFromFile(const wchar_t *path, KlayDiag *diag) {
    FILE *fp = _wfopen(path, L"r, ccs=UTF-8");
    if (!fp) { if (diag) { diag->line = 0; lstrcpynW(diag->message, L"cannot open file", 160); } return NULL; }

    HangulLayout *hl = (HangulLayout*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(HangulLayout));
    if (!hl) { fclose(fp); return NULL; }
    wcscpy_s(hl->name, 64, L"custom");
    bool bad = false;   // 잘못된 Key/Combine 발견 시 파일 전체 거부
    int lineno = 0;

    wchar_t line[256];
    while (fgetws(line, 256, fp)) {
        lineno++;
        TrimCrLf(line);
        // 앞 공백 스킵
        wchar_t *p = line;
        while (*p == L' ' || *p == L'\t') p++;
        if (*p == L'\0' || *p == L'#') continue;   // 빈 줄/주석

        wchar_t typec = 0;
        int a = 0, b = 0, idx = 0, val = 0;

        if (swscanf(p, L"Name = %63l[^\n]", hl->name) == 1) {
            TrimCrLf(hl->name);
        } else if (swscanf(p, L"Moachigi = %d", &val) == 1) {
            hl->moachigi = (val != 0);
        } else if (!wcsncmp(p, L"Key ", 4)) {
            // Key <키…> = <타입인덱스 …>  — 좌변 키 나열 = 배열 지정(키 수 = 스펙 수, 위치 대응).
            //   예: Key khj = C0 C2 C11.  단건(Key k = C0)은 길이 1의 특수형. 꼬리 '#' 주석 허용.
            wchar_t lhs[64] = {0};
            int consumed = 0;
            swscanf(p, L"Key %63ls = %n", lhs, &consumed);
            if (consumed <= 0) { FAIL(L"malformed Key line (missing '=')"); continue; }
            const wchar_t *q = p + consumed;
            size_t nk = wcslen(lhs), ki = 0;
            for (; ki < nk; ki++) {
                while (*q == L' ' || *q == L'\t') q++;
                if (*q == L'\0' || *q == L'#') break;   // 스펙이 키 수보다 적음 → 아래에서 bad
                int adv = 0;
                if (swscanf(q, L"%lc%d%n", &typec, &idx, &adv) != 2) break;
                JamoType t = TypeFromChar(typec);
                if (t == JAMO_NONE) { FAIL(L"Key: type must be C, M or T"); break; }
                if ((unsigned)lhs[ki] >= 128) { FAIL(L"Key: key must be an ASCII character"); break; }
                if (!ValidIdx(t, idx)) { FAIL(L"Key: jamo index out of range (C 0..18 / M 0..20 / T 1..27)"); break; }
                hl->keymap[(int)lhs[ki]].type = t;
                hl->keymap[(int)lhs[ki]].index = idx;
                q += adv;
            }
            while (*q == L' ' || *q == L'\t') q++;
            if (!bad && (ki != nk || (*q != L'\0' && *q != L'#')))
                FAIL(L"Key: number of specs must equal number of keys");
        } else if (swscanf(p, L"Combine %lc %d %d = %d", &typec, &a, &b, &val) == 4) {
            JamoType t = TypeFromChar(typec);
            if (t == JAMO_NONE) FAIL(L"Combine: type must be C, M or T");
            else if (!ValidIdx(t, a) || !ValidIdx(t, b) || !ValidIdx(t, val)) FAIL(L"Combine: jamo index out of range");
            else if (hl->combineCount >= HL_MAX_COMBINE) FAIL(L"too many Combine rules (max 256)");
            else { HangulCombine *c = &hl->combines[hl->combineCount++];
                   c->type = t; c->a = a; c->b = b; c->result = val; }
        }
        // 알 수 없는 줄은 무시 (향후 확장 여지)
    }
    fclose(fp);
    if (bad) { HeapFree(GetProcessHeap(), 0, hl); return NULL; }   // 부분 로드 대신 명시적 실패
    return hl;
}
#undef FAIL

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
