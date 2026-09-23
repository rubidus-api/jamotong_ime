#include "klay.h"
#include "layout.h"          // KBD_SEBEOL
#include "hangul_layout.h"
#include "chord_layout.h"
#include "seq_layout.h"
#include "version.h"   // RequiresJamotong 비교
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 머리부 훑기 (RFC-0011 P0·P2): Type·Abbrev·FormatVersion 과 메타데이터를 한 번의 열기로 읽는다.
//   full=false(DLL 로드 경로)면 본문 지시문을 만나는 곳에서 멈춘다 — 머리부는 앞에 둔다는 약속.
typedef struct { int lnFv, lnReq, lnAbbrev; } HeaderLines;
static bool IsBodyDirective(const wchar_t *p) {
    static const wchar_t *const k[] = { L"Key ", L"Map ", L"Combine ", L"Chord ", L"Hold ", L"Layer ", L"Begin ", L"Sequence ", NULL };
    for (int i = 0; k[i]; i++) if (!wcsncmp(p, k[i], wcslen(k[i]))) return true;
    return false;
}
static void TrimTail(wchar_t *s) {
    size_t n = wcslen(s);
    while (n > 0 && (s[n-1] == L'\n' || s[n-1] == L'\r' || s[n-1] == L' ' || s[n-1] == L'\t')) s[--n] = L'\0';
}
static void Prescan(const KlayLines *L, wchar_t *type, wchar_t *engine, wchar_t *abbrev, KlayMeta *m, HeaderLines *ln) {
    // 줄 목록 전체를 훑고 뒤가 이긴다 — 기반 자판(Extends) 줄이 앞, 자기 줄이 뒤다 (RFC-0011 P4).
    type[0] = L'\0'; engine[0] = L'\0'; abbrev[0] = L'\0';
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
        else if (!wcscmp(key, L"Engine"))           lstrcpynW(engine, v, 32);
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

// "…" 문자열: \" \\ \n \t \u{hex}. 잘못된 이스케이프·닫히지 않음·고립 서로게이트·NUL·U+10FFFF 초과는 오류.
//   *pp 는 여는 따옴표를 가리키고, 성공하면 닫는 따옴표 다음을 가리킨다. out 은 UTF-16.
bool Klay_ParseQuoted(const wchar_t **pp, wchar_t *out, size_t cap) {
    const wchar_t *p = *pp;
    if (*p != L'"') return false;
    p++;
    size_t n = 0;
    for (;;) {
        wchar_t ch = *p;
        if (ch == L'\0' || ch == L'\n' || ch == L'\r') return false;       // 닫히지 않음
        if (ch == L'"') { p++; break; }
        unsigned long cp;
        if (ch == L'\\') {
            wchar_t e2 = p[1];
            if (e2 == L'"' || e2 == L'\\') { cp = e2; p += 2; }
            else if (e2 == L'n') { cp = L'\n'; p += 2; }
            else if (e2 == L't') { cp = L'\t'; p += 2; }
            else if (e2 == L'u' && p[2] == L'{') {
                const wchar_t *q = p + 3; cp = 0; int digits = 0;
                while (digits < 7 && ((*q >= L'0' && *q <= L'9') || (*q >= L'a' && *q <= L'f') || (*q >= L'A' && *q <= L'F'))) {
                    cp = cp * 16 + (unsigned long)(*q <= L'9' ? *q - L'0' : (*q | 0x20) - L'a' + 10); q++; digits++;
                }
                if (!digits || *q != L'}') return false;
                p = q + 1;
            } else return false;                                              // 모르는 이스케이프
        } else { cp = (unsigned long)ch; p++; }
        if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        if (cp >= 0x10000) {
            if (n + 2 >= cap) return false;
            cp -= 0x10000; out[n++] = (wchar_t)(0xD800 + (cp >> 10)); out[n++] = (wchar_t)(0xDC00 + (cp & 0x3FF));
        } else {
            if (n + 1 >= cap) return false;
            out[n++] = (wchar_t)cp;
        }
    }
    out[n] = L'\0';
    *pp = p;
    return n > 0;
}
static const wchar_t *const kStaticDirectives[] = { L"Map", L"Identity", NULL };

static bool LoadStatic(const KlayLines *L, LayoutConfig *out, KlayDiag *diag) {
    for (int i = 0; i < 256; i++) out->charMap[i] = (wchar_t)i;
    wchar_t nameBuf[64]; wcscpy_s(nameBuf, 64, L"static");
    wchar_t line[256];
    bool bad = false; int lineno = 0;
    // RFC-0016 §5.2: Identity = passthrough — 원래 키를 그대로 통과 (내장 QWERTY 와 같다; 무변경 표를 다시 보내는 것과 다르다).
    //   적용된 Map 이 없을 때만 통과 자판. 상속받은 passthrough 위에 자식이 Map 을 쓰면 정적 표(자식이 키를 바꾼다),
    //   같은 파일에 둘 다 있으면 모순이라 오류.
    bool identity = false; int identityFile = -1, mapLines = 0;
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
        if (!wcsncmp(p, L"Identity", 8) && (p[8] == L' ' || p[8] == L'\t' || p[8] == L'=')) {
            wchar_t v[32] = {0};
            if (swscanf(p, L"Identity = %31ls", v) == 1 && !_wcsicmp(v, L"passthrough")) { identity = true; identityFile = L->v[li].file; }
            else if (swscanf(p, L"Identity = %31ls", v) == 1 && !_wcsicmp(v, L"map")) { identity = false; identityFile = -1; }
            else SFAIL(col, L"E-JMT-VALUE", L"Identity must be passthrough or map", L"passthrough = keys go through unchanged (like the built-in QWERTY)");
            continue;
        }
        if (!wcsncmp(p, L"Map ", 4)) {   // P6: `Map q shift = X`, `Map @Q = x`
            if (identity && identityFile == L->v[li].file) {
                SFAIL(col, L"E-JMT-IDENTITY", L"Map cannot be used with 'Identity = passthrough' in the same file",
                      L"remove 'Identity = passthrough' to remap keys");
                continue;
            }
            mapLines++;
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
    out->type = (identity && mapLines == 0) ? LAYOUT_TYPE_PASSTHROUGH : LAYOUT_TYPE_STATIC_MAP;
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
    wchar_t type[32] = L"", engine[32] = L"", abbrev[16] = L"";
    memset(out, 0, sizeof(*out));
    KlayLines L;   // 파일을 한 번 읽고 Extends/Include 를 푼다 (RFC-0011 P4)
    if (!KlayLines_Build(&L, path, diag)) { KlayLines_Free(&L); return false; }
    Prescan(&L, type, engine, abbrev, &m, &hl0);
    if (m.formatVersion < 1) m.formatVersion = 1;
    diag->formatVersion = m.formatVersion;
    if (meta) *meta = m;

    // 머리부 검사 — 더 새 형식은 거부한다 (RFC-0016 §4, 2026-09-22 채택: RFC-0011 P2 의 "경고 후 최선 로드"를
    // 바꿨다 — 새 형식의 의미를 옛 해석으로 돌리면 조용히 다른 자판이 된다). 더 높은 판을 요구해도 거부.
    if (m.formatVersion > 3) {   // 3판 = RFC-0016 (2026-09-23 채택)
        wchar_t msg[160];
        swprintf(msg, 160, L"file uses FormatVersion %d - this Jamotong reads up to 3", m.formatVersion);
        KlayDiag_Add(diag, KLAY_SEV_ERROR, hl0.lnFv, 1, L"E-JMT-FORMAT-NEWER", msg, L"update Jamotong to load this layout");
        KlayLines_Free(&L);
        return false;
    }
    // RFC-0016 §4: 3판 파일은 실제 지원 판을 반드시 적는다 — 옛 자모통이 경고만 하고 다르게 읽는 일을 막는다.
    if (m.formatVersion >= 3 && !m.requires[0]) {
        KlayDiag_Add(diag, KLAY_SEV_ERROR, hl0.lnFv, 1, L"E-JMT-REQUIRES-MISSING",
                     L"a FormatVersion 3 layout must say which Jamotong it needs", L"add 'RequiresJamotong = 0.33.0'");
        KlayLines_Free(&L);
        return false;
    }
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
    const bool isInput = !_wcsicmp(type, L"input") && m.formatVersion >= 3;   // 3판 공통 표면 (RFC-0016 §6.3)
    if (type[0] && !isInput && _wcsicmp(type, L"static") && _wcsicmp(type, L"chord") && _wcsicmp(type, L"hangul")) {
        // RFC-0008 W2-02: 예전엔 모르는 Type 이 조용히 hangul 로 읽혔다
        wchar_t msg[160]; swprintf(msg, 160, L"unknown Type '%ls'", type);
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-TYPE-UNKNOWN", msg,
                     m.formatVersion >= 3 ? L"Type must be static, hangul, chord or input"
                                          : L"Type must be static, hangul or chord");
        KlayLines_Free(&L);
        return false;
    }
    if (isInput && _wcsicmp(engine, L"sequence")) {
        // `Type = input` 은 엔진을 반드시 고른다 — 빠지면 조용히 다른 엔진으로 읽힐 자리다.
        wchar_t msg[160];
        if (engine[0]) swprintf(msg, 160, L"this Jamotong does not have the input engine '%ls'", engine);
        else wcscpy(msg, L"'Type = input' must say which engine it uses");
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-ENGINE", msg, L"add 'Engine = sequence'");
        KlayLines_Free(&L);
        return false;
    }
    if (isInput) {
        SeqLayout *sl = SeqLayout_LoadFromLines(&L, path, diag);
        // 오너 지시 2026-09-23: 자판이 설 때 문법과 사전 데이터를 모두 본다. 사전 전수 점검은
        // SeqLayout_OpenDict 안에서 끝난다 (자판 파일·구운 자판이 같은 길을 쓴다).
        if (sl) {
            out->type = LAYOUT_TYPE_SEQUENCE; out->pSeqLayout = sl;
            out->name = _wcsdup(sl->name[0] ? sl->name : L"sequence");
            if (out->name) ok = true; else SeqLayout_Free(sl);
        }
    } else if (!_wcsicmp(type, L"static")) {
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
