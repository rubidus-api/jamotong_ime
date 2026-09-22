// klay_diag.c — .jmt 진단·머리부 공용 로직 (RFC-0011 P1·P2). WinAPI 무의존(네이티브 테스트).
#include "klay.h"
#include "version.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

void KlayDiag_Init(KlayDiag *d) {
    if (!d) return;
    memset(d, 0, sizeof(*d));
    d->formatVersion = 1;
}

static void CopyW(wchar_t *dst, size_t n, const wchar_t *src) {
    if (!n) return;
    size_t i = 0;
    for (; src && src[i] && i + 1 < n; i++) dst[i] = src[i];
    dst[i] = L'\0';
}

void KlayDiag_Add(KlayDiag *d, KlaySeverity sev, int line, int col,
                  const wchar_t *code, const wchar_t *msg, const wchar_t *help) {
    if (!d) return;
    if (sev == KLAY_SEV_ERROR) {
        if (d->errors == 0) { d->line = line; CopyW(d->message, 160, msg); }
        d->errors++;
    } else d->warnings++;
    if (d->count >= KLAY_DIAG_MAX) return;
    KlayDiagItem *it = &d->items[d->count++];
    it->line = line; it->col = col; it->severity = sev;
    CopyW(it->code, 24, code); CopyW(it->message, 160, msg); CopyW(it->help, 160, help ? help : L"");
    CopyW(it->file, 64, d->curFile ? d->curFile : L"");
}

void Klay_DiagFormat(const KlayDiag *d, const wchar_t *file, wchar_t *out, size_t cch) {
    if (!out || !cch) return;
    out[0] = L'\0';
    size_t o = 0;
    for (int i = 0; d && i < d->count && o + 1 < cch; i++) {
        const KlayDiagItem *it = &d->items[i];
        const wchar_t *fn = (it->file[0] && wcscmp(it->file, d->topFile) != 0) ? it->file : (file ? file : L"<file>");
        int n = swprintf(out + o, cch - o, L"%ls:%d:%d: %ls: %ls [%ls]\n", fn,
                         it->line, it->col, it->severity == KLAY_SEV_ERROR ? L"error" : L"warning",
                         it->message, it->code);
        if (n < 0) break;
        o += (size_t)n;
        if (it->help[0] && o + 1 < cch) {
            n = swprintf(out + o, cch - o, L"   help: %ls\n", it->help);
            if (n < 0) break;
            o += (size_t)n;
        }
    }
}

static const wchar_t *const kHeaderKeys[] = {
    L"FormatVersion", L"Type", L"Id", L"Name", L"Abbrev", L"Version", L"Author", L"License",
    L"Homepage", L"Description", L"Locale", L"RequiresJamotong", NULL
};

// 첫 낱말을 뽑는다. `word = ...` 꼴이면 hasEq=true.
static size_t FirstWord(const wchar_t *line, wchar_t *w, size_t n, bool *hasEq) {
    const wchar_t *p = line;
    while (*p == L' ' || *p == L'\t') p++;
    size_t k = 0;
    while (p[k] && !iswspace(p[k]) && p[k] != L'=' && k + 1 < n) { w[k] = p[k]; k++; }
    w[k] = L'\0';
    const wchar_t *q = p + k;
    while (*q == L' ' || *q == L'\t') q++;
    if (hasEq) *hasEq = (*q == L'=');
    return k;
}

bool KlayHeader_IsKnownKey(const wchar_t *line) {
    wchar_t w[48]; bool eq = false;
    if (!FirstWord(line, w, 48, &eq) || !eq) return false;
    for (int i = 0; kHeaderKeys[i]; i++) if (wcscmp(w, kHeaderKeys[i]) == 0) return true;
    return false;
}

static int Lev(const wchar_t *a, const wchar_t *b) {   // 짧은 낱말용 편집 거리 (대소문자 무시)
    int la = (int)wcslen(a), lb = (int)wcslen(b);
    if (la > 40 || lb > 40) return 99;
    int prev[41], cur[41];
    for (int j = 0; j <= lb; j++) prev[j] = j;
    for (int i = 1; i <= la; i++) {
        cur[0] = i;
        for (int j = 1; j <= lb; j++) {
            int cost = towlower(a[i-1]) == towlower(b[j-1]) ? 0 : 1;
            int m = prev[j] + 1;
            if (cur[j-1] + 1 < m) m = cur[j-1] + 1;
            if (prev[j-1] + cost < m) m = prev[j-1] + cost;
            cur[j] = m;
        }
        memcpy(prev, cur, sizeof(int) * (size_t)(lb + 1));
    }
    return prev[lb];
}

static const wchar_t *Suggest(const wchar_t *w, const wchar_t *const *a, const wchar_t *const *b) {
    const wchar_t *best = NULL; int bestD = 3;   // 거리 2 이하만 제안
    for (int pass = 0; pass < 2; pass++) {
        const wchar_t *const *list = pass ? b : a;
        for (int i = 0; list && list[i]; i++) {
            int dist = Lev(w, list[i]);
            if (dist < bestD) { bestD = dist; best = list[i]; }
        }
    }
    return best;
}

bool Klay_UnknownLine(KlayDiag *d, const wchar_t *line, int lineno, int col, const wchar_t *const *known) {
    wchar_t w[48], msg[160], help[160];
    bool eq = false;
    FirstWord(line, w, 48, &eq);
    const wchar_t *s = Suggest(w, known, kHeaderKeys);   // 같은 거리면 지시문이 먼저 (Kye → Key)
    if (s) swprintf(help, 160, L"did you mean '%ls'?", s); else help[0] = L'\0';
    if (eq) {
        swprintf(msg, 160, L"unknown header key '%ls' - ignored", w);
        KlayDiag_Add(d, KLAY_SEV_WARNING, lineno, col, L"W-JMT-UNKNOWN-KEY", msg, help);
        return false;
    }
    int fv = d ? d->formatVersion : 1;
    if (fv >= 2) {
        swprintf(msg, 160, L"unknown directive '%ls'", w);
        KlayDiag_Add(d, KLAY_SEV_ERROR, lineno, col, L"E-JMT-UNKNOWN-DIRECTIVE", msg, help);
        return true;
    }
    swprintf(msg, 160, L"unrecognised line ignored ('%ls') - an error from FormatVersion 2", w);
    KlayDiag_Add(d, KLAY_SEV_WARNING, lineno, col, L"W-JMT-IGNORED-LINE", msg, help);
    return false;
}

int Klay_CompareVersion(const wchar_t *a, const wchar_t *b) {
    for (int i = 0; i < 4; i++) {
        long x = 0, y = 0;
        if (a && *a) { x = wcstol(a, (wchar_t**)&a, 10); if (*a == L'.') a++; }
        if (b && *b) { y = wcstol(b, (wchar_t**)&b, 10); if (*b == L'.') b++; }
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

// ── 글쇠 머리 해석 (RFC-0011 P6) ────────────────────────────────────────────────────
static const wchar_t kUsBase[]  = L"`1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./";
static const wchar_t kUsShift[] = L"~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?";

static wchar_t UsShift(wchar_t c) {
    for (int i = 0; kUsBase[i]; i++) if (kUsBase[i] == c) return kUsShift[i];
    return 0;
}
static bool IsUsShifted(wchar_t c) {
    for (int i = 0; kUsShift[i]; i++) if (kUsShift[i] == c) return true;
    return false;
}

// US 스캔코드(set 1) → 기본 문자
static wchar_t FromScan(unsigned sc) {
    static const struct { unsigned sc; const wchar_t *row; } R[] = {
        { 0x02, L"1234567890-=" }, { 0x10, L"qwertyuiop[]" }, { 0x1E, L"asdfghjkl;'`" }, { 0x2C, L"zxcvbnm,./" } };
    if (sc == 0x29) return L'`';
    if (sc == 0x2B) return L'\\';
    if (sc == 0x39) return L' ';
    for (int r = 0; r < 4; r++) {
        size_t n = wcslen(R[r].row);
        if (sc >= R[r].sc && sc < R[r].sc + n) {
            wchar_t c = R[r].row[sc - R[r].sc];
            if (r == 2 && sc == 0x28) return L'\'';
            if (r == 2 && c == L'`') return 0;
            return c;
        }
    }
    return 0;
}

static wchar_t FromVkName(const wchar_t *n) {
    static const struct { const wchar_t *name; wchar_t c; } V[] = {
        { L"VK_OEM_1", L';' }, { L"VK_OEM_PLUS", L'=' }, { L"VK_OEM_COMMA", L',' }, { L"VK_OEM_MINUS", L'-' },
        { L"VK_OEM_PERIOD", L'.' }, { L"VK_OEM_2", L'/' }, { L"VK_OEM_3", L'`' }, { L"VK_OEM_4", L'[' },
        { L"VK_OEM_5", L'\\' }, { L"VK_OEM_6", L']' }, { L"VK_OEM_7", L'\'' }, { L"VK_SPACE", L' ' } };
    for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) if (!wcscmp(n, V[i].name)) return V[i].c;
    if (!wcsncmp(n, L"VK_", 3) && n[3] && !n[4]) {
        wchar_t c = n[3];
        if (c >= L'A' && c <= L'Z') return (wchar_t)(c + 32);
        if (c >= L'0' && c <= L'9') return c;
    }
    return 0;
}

bool Klay_ParseKeyHead(const wchar_t *p, wchar_t *out, size_t cch, const wchar_t **specPos,
                       KlayDiag *d, int lineno, int col0) {
    const wchar_t *q = p;
    while (*q == L' ' || *q == L'\t') q++;
    const int colKeys = col0 + (int)(q - p);
    wchar_t keys[64]; size_t k = 0;
    // 글쇠 토큰은 공백까지 (예전 `%ls` 와 같다) — `=` 글쇠 자체(`Map = = ]`)도 글쇠로 읽힌다.
    while (*q && *q != L' ' && *q != L'\t' && k + 1 < 64) keys[k++] = *q++;
    keys[k] = L'\0';
    while (*q == L' ' || *q == L'\t') q++;
    wchar_t level[16] = L""; size_t lv = 0;
    if (*q != L'=') {
        while (*q && *q != L' ' && *q != L'\t' && *q != L'=' && lv + 1 < 16) level[lv++] = *q++;
        level[lv] = L'\0';
        while (*q == L' ' || *q == L'\t') q++;
    }
    if (*q != L'=' || !keys[0]) {
        KlayDiag_Add(d, KLAY_SEV_ERROR, lineno, colKeys, L"E-JMT-KEY-SYNTAX", L"expected '<keys> [base|shift] = ...'",
                     L"e.g. 'Key q = C0' or 'Key q shift = C1'");
        return false;
    }
    *specPos = q + 1;
    bool shift = false;
    if (level[0]) {
        if (!wcscmp(level, L"shift")) shift = true;
        else if (wcscmp(level, L"base") != 0) {
            wchar_t msg[160];
            swprintf(msg, 160, L"unknown or unsupported shift level '%ls'", level);
            KlayDiag_Add(d, KLAY_SEV_ERROR, lineno, colKeys + (int)k + 1, L"E-JMT-LEVEL", msg,
                         L"use 'base' or 'shift' (AltGr is not read by the IME)");
            return false;
        }
    }
    wchar_t chars[64]; size_t n = 0;
    if (keys[0] == L'@' && keys[1]) {   // `@` 한 글자는 '@' 글쇠 자체, `@이름` 은 물리 글쇠
        const wchar_t *name = keys + 1;
        wchar_t c = 0;
        if (!wcsncmp(name, L"SC", 2) && name[2]) { wchar_t *e; unsigned long v = wcstoul(name + 2, &e, 16); if (!*e) c = FromScan((unsigned)v); }
        else if (!wcsncmp(name, L"VK_", 3)) c = FromVkName(name);
        else if (name[0] && !name[1]) { c = name[0]; if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32); if (!UsShift(c) && c != L' ') c = 0; }
        if (!c) {
            wchar_t msg[160]; swprintf(msg, 160, L"unknown physical key '%ls'", keys);
            KlayDiag_Add(d, KLAY_SEV_ERROR, lineno, colKeys, L"E-JMT-PHYSKEY", msg,
                         L"one key per line: @Q (US key name), @SC10 (scan code, hex) or @VK_OEM_1");
            return false;
        }
        chars[n++] = c;
    } else {
        for (size_t i = 0; i < k && n + 1 < 64; i++) chars[n++] = keys[i];
    }
    chars[n] = L'\0';
    if (shift) {
        for (size_t i = 0; i < n; i++) {
            wchar_t sc = UsShift(chars[i]);
            if (!sc) {
                wchar_t msg[160];
                swprintf(msg, 160, IsUsShifted(chars[i]) ? L"'%lc' is already a Shift character" : L"'%lc' has no Shift character",
                         chars[i]);
                KlayDiag_Add(d, KLAY_SEV_ERROR, lineno, colKeys + (int)i, L"E-JMT-LEVEL", msg,
                             L"write the unshifted key with 'shift', e.g. 'Key q shift' for Q");
                return false;
            }
            chars[i] = sc;
        }
    }
    if (n + 1 > cch) return false;
    wcscpy(out, chars);
    return true;
}
