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
static void Prescan(const KlayLines *L, wchar_t *type, wchar_t *abbrev, KlayMeta *m, HeaderLines *ln) {
    // 줄 목록 전체를 훑고 뒤가 이긴다 — 기반 자판(Extends) 줄이 앞, 자기 줄이 뒤다 (RFC-0011 P4).
    type[0] = L'\0'; abbrev[0] = L'\0';
    for (int i = 0; i < L->n; i++) {
        wchar_t line[256];
        lstrcpynW(line, L->v[i].text, 256);
        TrimTail(line);
        wchar_t *p = line;
        while (*p == L' ' || *p == L'\t') p++;
        if (*p == L'#' || *p == L'\0' || IsBodyDirective(p)) continue;
        wchar_t key[32] = {0};
        int vpos = 0;
        if (swscanf(p, L"%31l[A-Za-z] = %n", key, &vpos) < 1 || vpos == 0) continue;
        const wchar_t *v = p + vpos;
        const int lineno = L->v[i].line;
        const bool top = (L->v[i].file == 0);   // 최상위 파일 — 신원·저작 정보는 여기서만 (기반의 것을 물려받지 않는다)
        if      (!wcscmp(key, L"Type"))             lstrcpynW(type, v, 32);
        else if (!wcscmp(key, L"Abbrev"))           { lstrcpynW(abbrev, v, 16); if (top) ln->lnAbbrev = lineno; }
        else if (!top) {
            // 기반 자판이 더 높은 판을 요구하면 그 요구는 파생에도 걸린다
            if (!wcscmp(key, L"RequiresJamotong") && Klay_CompareVersion(v, m->requires) > 0) { lstrcpynW(m->requires, v, 16); ln->lnReq = lineno; }
            continue;
        }
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
}

static const wchar_t *const kStaticDirectives[] = { L"Map", NULL };

static bool LoadStatic(const KlayLines *L, LayoutConfig *out, KlayDiag *diag) {
    for (int i = 0; i < 256; i++) out->charMap[i] = (wchar_t)i;
    wchar_t nameBuf[64]; wcscpy_s(nameBuf, 64, L"static");
    wchar_t line[256];
    bool bad = false; int lineno = 0;
    #define SFAIL(col, code, msg, help) do { KlayDiag_Add(diag, KLAY_SEV_ERROR, lineno, (col), (code), (msg), (help)); bad = true; } while (0)
    for (int li = 0; li < L->n; li++) {
        lstrcpynW(line, L->v[li].text, 256);
        lineno = L->v[li].line;
        if (diag) diag->curFile = L->files[L->v[li].file];
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
        if (!wcsncmp(p, L"Map ", 4)) {   // P6: `Map q shift = X`, `Map @Q = x`
            const wchar_t *sp = NULL;
            if (!Klay_ParseKeyHead(p + 4, lhs, 64, &sp, diag, lineno, col + 4)) { bad = true; continue; }
            if (swscanf(sp, L" %63ls", rhs) != 1) {
                SFAIL(col, L"E-JMT-MAP-LEN", L"Map: missing output after '='", NULL);
                continue;
            }
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
    if (diag) diag->curFile = NULL;
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
    memset(out, 0, sizeof(*out));
    KlayLines L;   // 파일을 한 번 읽고 Extends/Include 를 푼다 (RFC-0011 P4)
    if (!KlayLines_Build(&L, path, diag)) { KlayLines_Free(&L); return false; }
    Prescan(&L, type, abbrev, &m, &hl0);
    if (m.formatVersion < 1) m.formatVersion = 1;
    diag->formatVersion = m.formatVersion;
    if (meta) *meta = m;

    // RFC-0011 P2: 머리부 검사 — 더 새 형식은 경고 후 최선 로드, 더 높은 판을 요구하면 거부.
    if (m.formatVersion > 2)
        KlayDiag_Add(diag, KLAY_SEV_WARNING, hl0.lnFv, 1, L"W-JMT-FORMAT-NEWER",
                     L"file uses a newer format version - loading what this version understands",
                     L"update Jamotong to use every feature of this layout");
    if (m.requires[0] && Klay_CompareVersion(m.requires, JAMOTONG_VERSION) > 0) {
        wchar_t msg[160];
        swprintf(msg, 160, L"this layout requires Jamotong %ls or newer (this is %ls)", m.requires, JAMOTONG_VERSION);
        KlayDiag_Add(diag, KLAY_SEV_ERROR, hl0.lnReq, 1, L"E-JMT-REQUIRES", msg, L"update Jamotong");
        KlayLines_Free(&L);
        return false;
    }
    if (wcslen(abbrev) > 4)
        KlayDiag_Add(diag, KLAY_SEV_WARNING, hl0.lnAbbrev, 1, L"W-JMT-ABBREV-LONG",
                     L"Abbrev is longer than 4 characters - the 2x2 icon shows only the first 4",
                     L"use 1 to 4 characters");

    bool ok = false;
    if (type[0] && _wcsicmp(type, L"static") && _wcsicmp(type, L"chord") && _wcsicmp(type, L"hangul")) {
        // RFC-0008 W2-02: 예전엔 모르는 Type 이 조용히 hangul 로 읽혔다
        wchar_t msg[160]; swprintf(msg, 160, L"unknown Type '%ls'", type);
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-TYPE-UNKNOWN", msg, L"Type must be static, hangul or chord");
        KlayLines_Free(&L);
        return false;
    }
    if (!_wcsicmp(type, L"static")) {
        ok = LoadStatic(&L, out, diag);
    } else if (!_wcsicmp(type, L"chord")) {
        ChordLayout *cl = ChordLayout_LoadFromLines(&L, diag);
        if (cl) {
            out->type = LAYOUT_TYPE_CHORD; out->pChordLayout = cl;
            out->name = _wcsdup(cl->name[0] ? cl->name : L"chord");
            if (out->name) ok = true; else ChordLayout_Free(cl);
        }
    } else {   // 기본: hangul (Type 생략 시)
        HangulLayout *hl = HangulLayout_LoadFromLines(&L, diag);
        if (hl) {
            out->type = LAYOUT_TYPE_HANGUL_CUSTOM; out->kbdVariant = KBD_SEBEOL; out->pHangulLayout = hl;
            out->name = _wcsdup(hl->name[0] ? hl->name : L"custom");
            if (out->name) ok = true; else HangulLayout_Free(hl);
        }
    }
    KlayLines_Free(&L);
    if (ok) {
        if (abbrev[0]) lstrcpynW(out->abbrev, abbrev, 8);
        else lstrcpynW(out->abbrev, (out->name && out->name[0]) ? out->name : L"??", 4);   // 앞 3글자 파생
    }
    return ok;
}
