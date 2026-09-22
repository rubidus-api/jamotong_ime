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
