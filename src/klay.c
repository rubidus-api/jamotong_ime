#include "klay.h"
#include "layout.h"          // KBD_SEBEOL
#include "hangul_layout.h"
#include "chord_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void PeekType(const wchar_t *path, wchar_t *type, size_t n) {
    (void)n;
    type[0] = L'\0';
    FILE *fp = _wfopen(path, L"r, ccs=UTF-8");
    if (!fp) return;
    wchar_t line[256];
    while (fgetws(line, 256, fp)) {
        wchar_t *p = line;
        while (*p == L' ' || *p == L'\t') p++;
        if (swscanf(p, L"Type = %31ls", type) == 1) break;
    }
    fclose(fp);
}

// Type = static: Map <키…> = <출력…>
//   좌변에 키를 여러 개 쓰면 배열 지정 — 좌우 같은 길이, 위치 대응 (예: Map qwe = ',.).
//   단건(Map q = ')은 길이 1의 특수형. 길이 불일치·범위 밖 키는 파일 거부.
static bool LoadStatic(const wchar_t *path, LayoutConfig *out) {
    FILE *fp = _wfopen(path, L"r, ccs=UTF-8");
    if (!fp) return false;
    for (int i = 0; i < 256; i++) out->charMap[i] = (wchar_t)i;
    wchar_t nameBuf[64]; wcscpy_s(nameBuf, 64, L"static");
    wchar_t line[256];
    bool bad = false;
    while (fgetws(line, 256, fp)) {
        wchar_t *p = line;
        while (*p == L' ' || *p == L'\t') p++;
        if (*p == L'#' || *p == L'\0') continue;
        wchar_t lhs[64] = {0}, rhs[64] = {0};
        if (swscanf(p, L"Name = %63l[^\n]", nameBuf) == 1) {
            size_t k = wcslen(nameBuf);
            while (k > 0 && (nameBuf[k-1]==L' '||nameBuf[k-1]==L'\t'||nameBuf[k-1]==L'\r')) nameBuf[--k]=L'\0';
            continue;
        }
        if (swscanf(p, L"Map %63ls = %63ls", lhs, rhs) == 2) {
            size_t n = wcslen(lhs);
            if (n == 0 || n != wcslen(rhs)) { bad = true; continue; }   // 키 수 ≠ 출력 수
            for (size_t i = 0; i < n; i++) {
                if ((unsigned)lhs[i] < 256) out->charMap[lhs[i]] = rhs[i];
                else bad = true;
            }
        }
    }
    fclose(fp);
    if (bad) return false;
    out->type = LAYOUT_TYPE_STATIC_MAP;
    out->name = _wcsdup(nameBuf);
    return out->name != NULL;
}

// .jmt의 "Abbrev = XX" (트레이 표시용 2~3글자)를 읽는다. 없으면 name 앞 3글자로 파생.
static void ReadAbbrev(const wchar_t *path, wchar_t *abbrev, const wchar_t *name) {
    abbrev[0] = L'\0';
    FILE *fp = _wfopen(path, L"r, ccs=UTF-8");
    if (fp) {
        wchar_t line[128];
        while (fgetws(line, 128, fp)) {
            wchar_t buf[16] = {0};
            if (swscanf(line, L"Abbrev = %7l[^\r\n]", buf) == 1) {
                size_t k = wcslen(buf);
                while (k > 0 && (buf[k-1]==L' '||buf[k-1]==L'\t')) buf[--k] = L'\0';
                if (buf[0]) lstrcpynW(abbrev, buf, 8);
                break;
            }
        }
        fclose(fp);
    }
    if (!abbrev[0]) lstrcpynW(abbrev, (name && name[0]) ? name : L"??", 4);   // 앞 3글자 파생
}

bool Klay_Load(const wchar_t *path, LayoutConfig *out) {
    wchar_t type[32] = L"";
    PeekType(path, type, 32);
    memset(out, 0, sizeof(*out));
    bool ok = false;

    if (!_wcsicmp(type, L"static")) {
        ok = LoadStatic(path, out);
    } else if (!_wcsicmp(type, L"chord")) {
        ChordLayout *cl = ChordLayout_LoadFromFile(path);
        if (cl) {
            out->type = LAYOUT_TYPE_CHORD; out->pChordLayout = cl;
            out->name = _wcsdup(cl->name[0] ? cl->name : L"chord");
            if (out->name) ok = true; else ChordLayout_Free(cl);
        }
    } else {   // 기본: hangul (Type 생략 시)
        HangulLayout *hl = HangulLayout_LoadFromFile(path);
        if (hl) {
            out->type = LAYOUT_TYPE_HANGUL_CUSTOM; out->kbdVariant = KBD_SEBEOL; out->pHangulLayout = hl;
            out->name = _wcsdup(hl->name[0] ? hl->name : L"custom");
            if (out->name) ok = true; else HangulLayout_Free(hl);
        }
    }
    if (ok) ReadAbbrev(path, out->abbrev, out->name);
    return ok;
}
