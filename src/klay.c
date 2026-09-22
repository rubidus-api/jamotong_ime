#include "klay.h"
#include "layout.h"          // KBD_SEBEOL
#include "hangul_layout.h"
#include "chord_layout.h"
#include "version.h"   // RequiresJamotong 비교
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 머리부 훑기 (RFC-0011 P0·P2): Type·Abbrev·FormatVersion 과 메타데이터를 한 번의 열기로 읽는다.
//   full=false(DLL 로드 경로)면 본문 지시문을 만나는 곳에서 멈춘다 — 머리부는 앞에 둔다는 약속.
typedef struct { int lnFv, lnReq, lnAbbrev; } HeaderLines;
static bool IsBodyDirective(const wchar_t *p) {
    static const wchar_t *const k[] = { L"Key ", L"Map ", L"Combine ", L"Chord ", L"Hold ", L"Layer ", L"Begin ", NULL };
    for (int i = 0; k[i]; i++) if (!wcsncmp(p, k[i], wcslen(k[i]))) return true;
    return false;
}
static void TrimTail(wchar_t *s) {
    size_t n = wcslen(s);
    while (n > 0 && (s[n-1] == L'\n' || s[n-1] == L'\r' || s[n-1] == L' ' || s[n-1] == L'\t')) s[--n] = L'\0';
}
static void Prescan(const wchar_t *path, wchar_t *type, wchar_t *abbrev, KlayMeta *m, HeaderLines *ln, bool full) {
    type[0] = L'\0'; abbrev[0] = L'\0';
    FILE *fp = _wfopen(path, L"r, ccs=UTF-8");
    if (!fp) return;
    wchar_t line[256];
    int lineno = 0;
    while (fgetws(line, 256, fp)) {
        lineno++;
        TrimTail(line);
        wchar_t *p = line;
        while (*p == L' ' || *p == L'\t') p++;
        if (*p == L'#' || *p == L'\0') continue;
        if (IsBodyDirective(p)) { if (full) continue; break; }
        wchar_t key[32] = {0};
        int vpos = 0;
        if (swscanf(p, L"%31l[A-Za-z] = %n", key, &vpos) < 1 || vpos == 0) continue;
        const wchar_t *v = p + vpos;
        if      (!wcscmp(key, L"Type"))             lstrcpynW(type, v, 32);
        else if (!wcscmp(key, L"Abbrev"))           { lstrcpynW(abbrev, v, 16); ln->lnAbbrev = lineno; }
        else if (!wcscmp(key, L"FormatVersion"))    { m->formatVersion = (int)wcstol(v, NULL, 10); ln->lnFv = lineno; }
        else if (!wcscmp(key, L"RequiresJamotong")) { lstrcpynW(m->requires, v, 16); ln->lnReq = lineno; }
        else if (!wcscmp(key, L"Id"))               lstrcpynW(m->id, v, 64);
        else if (!wcscmp(key, L"Version"))          lstrcpynW(m->version, v, 32);
        else if (!wcscmp(key, L"Author"))           lstrcpynW(m->author, v, 128);
        else if (!wcscmp(key, L"License"))          lstrcpynW(m->license, v, 64);
        else if (!wcscmp(key, L"Homepage"))         lstrcpynW(m->homepage, v, 160);
        else if (!wcscmp(key, L"Description"))      lstrcpynW(m->description, v, 160);
        else if (!wcscmp(key, L"Locale"))           lstrcpynW(m->locale, v, 16);
    }
    fclose(fp);
}

// Type = static: Map <키…> = <출력…>
//   좌변에 키를 여러 개 쓰면 배열 지정 — 좌우 같은 길이, 위치 대응 (예: Map qwe = ',.).
//   단건(Map q = ')은 길이 1의 특수형. 길이 불일치·범위 밖 키는 파일 거부.
static const wchar_t *const kStaticDirectives[] = { L"Map", NULL };

static bool LoadStatic(const wchar_t *path, LayoutConfig *out, KlayDiag *diag) {
    FILE *fp = _wfopen(path, L"r, ccs=UTF-8");
    if (!fp) { KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 0, L"E-JMT-OPEN", L"cannot open file", NULL); return false; }
    for (int i = 0; i < 256; i++) out->charMap[i] = (wchar_t)i;
    wchar_t nameBuf[64]; wcscpy_s(nameBuf, 64, L"static");
    wchar_t line[256];
    bool bad = false; int lineno = 0;
    #define SFAIL(col, code, msg, help) do { KlayDiag_Add(diag, KLAY_SEV_ERROR, lineno, (col), (code), (msg), (help)); bad = true; } while (0)
    while (fgetws(line, 256, fp)) {
        lineno++;
        TrimTail(line);
        wchar_t *p = line;
        while (*p == L' ' || *p == L'\t') p++;
        if (*p == L'#' || *p == L'\0') continue;
        int col = (int)(p - line) + 1;
        wchar_t lhs[64] = {0}, rhs[64] = {0};
        if (swscanf(p, L"Name = %63l[^\n]", nameBuf) == 1) {
            size_t k = wcslen(nameBuf);
            while (k > 0 && (nameBuf[k-1]==L' '||nameBuf[k-1]==L'\t'||nameBuf[k-1]==L'\r')) nameBuf[--k]=L'\0';
            continue;
        }
        if (swscanf(p, L"Map %63ls = %63ls", lhs, rhs) == 2) {
            size_t n = wcslen(lhs);
            if (n == 0 || n != wcslen(rhs)) {
                SFAIL(col + 4, L"E-JMT-MAP-LEN", L"Map: left and right sides must have the same length",
                      L"write one output character for each key, e.g. 'Map qwe = abc'");
                continue;
            }
            for (size_t i = 0; i < n; i++) {
                if ((unsigned)lhs[i] < 256) out->charMap[lhs[i]] = rhs[i];
                else SFAIL(col + 4 + (int)i, L"E-JMT-LATIN1", L"Map: key must be a Latin-1 character (code < 256)", NULL);
            }
            continue;
        }
        if (KlayHeader_IsKnownKey(p)) continue;
        if (Klay_UnknownLine(diag, p, lineno, col, kStaticDirectives)) bad = true;
    }
    #undef SFAIL
    fclose(fp);
    if (bad) return false;
    out->type = LAYOUT_TYPE_STATIC_MAP;
    out->name = _wcsdup(nameBuf);
    return out->name != NULL;
}


bool Klay_Load(const wchar_t *path, LayoutConfig *out, KlayDiag *diag) {
    return Klay_LoadEx(path, out, diag, NULL);
}

bool Klay_LoadEx(const wchar_t *path, LayoutConfig *out, KlayDiag *diag, KlayMeta *meta) {
    KlayDiag local;
    if (!diag) diag = &local;   // 파서는 FormatVersion 문맥이 필요하다 (호출자가 진단을 원치 않아도)
    KlayDiag_Init(diag);
    KlayMeta m; memset(&m, 0, sizeof(m)); m.formatVersion = 1;
    HeaderLines hl0 = {0};
    wchar_t type[32] = L"", abbrev[16] = L"";
    Prescan(path, type, abbrev, &m, &hl0, meta != NULL);
    if (m.formatVersion < 1) m.formatVersion = 1;
    diag->formatVersion = m.formatVersion;
    if (meta) *meta = m;
    memset(out, 0, sizeof(*out));

    // RFC-0011 P2: 머리부 검사 — 더 새 형식은 경고 후 최선 로드, 더 높은 판을 요구하면 거부.
    if (m.formatVersion > 2)
        KlayDiag_Add(diag, KLAY_SEV_WARNING, hl0.lnFv, 1, L"W-JMT-FORMAT-NEWER",
                     L"file uses a newer format version - loading what this version understands",
                     L"update Jamotong to use every feature of this layout");
    if (m.requires[0] && Klay_CompareVersion(m.requires, JAMOTONG_VERSION) > 0) {
        wchar_t msg[160];
        swprintf(msg, 160, L"this layout requires Jamotong %ls or newer (this is %ls)", m.requires, JAMOTONG_VERSION);
        KlayDiag_Add(diag, KLAY_SEV_ERROR, hl0.lnReq, 1, L"E-JMT-REQUIRES", msg, L"update Jamotong");
        return false;
    }
    if (wcslen(abbrev) > 4)
        KlayDiag_Add(diag, KLAY_SEV_WARNING, hl0.lnAbbrev, 1, L"W-JMT-ABBREV-LONG",
                     L"Abbrev is longer than 4 characters - the 2x2 icon shows only the first 4",
                     L"use 1 to 4 characters");

    bool ok = false;
    if (!_wcsicmp(type, L"static")) {
        ok = LoadStatic(path, out, diag);
    } else if (!_wcsicmp(type, L"chord")) {
        ChordLayout *cl = ChordLayout_LoadFromFile(path, diag);
        if (cl) {
            out->type = LAYOUT_TYPE_CHORD; out->pChordLayout = cl;
            out->name = _wcsdup(cl->name[0] ? cl->name : L"chord");
            if (out->name) ok = true; else ChordLayout_Free(cl);
        }
    } else {   // 기본: hangul (Type 생략 시)
        HangulLayout *hl = HangulLayout_LoadFromFile(path, diag);
        if (hl) {
            out->type = LAYOUT_TYPE_HANGUL_CUSTOM; out->kbdVariant = KBD_SEBEOL; out->pHangulLayout = hl;
            out->name = _wcsdup(hl->name[0] ? hl->name : L"custom");
            if (out->name) ok = true; else HangulLayout_Free(hl);
        }
    }
    if (ok) {
        if (abbrev[0]) lstrcpynW(out->abbrev, abbrev, 8);
        else lstrcpynW(out->abbrev, (out->name && out->name[0]) ? out->name : L"??", 4);   // 앞 3글자 파생
    }
    return ok;
}
