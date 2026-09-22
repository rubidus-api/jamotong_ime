// klay_cli.c — .jmt 저작 도구 명령 (RFC-0011 P3). WinAPI 무의존: 리눅스 네이티브로도 빌드된다.
#include "klay_cli.h"
#include "klay.h"
#include "hangul_layout.h"
#include "chord_layout.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void Outf(KlayCliOut out, void *ctx, const wchar_t *fmt, ...) {
    wchar_t buf[1024];
    va_list ap; va_start(ap, fmt);
    vswprintf(buf, 1024, fmt, ap);
    va_end(ap);
    buf[1023] = L'\0';
    out(buf, ctx);
}

int KlayCli_IsCommand(int argc, const wchar_t *const *argv) {
    for (int i = 1; i < argc; i++)
        if (!wcscmp(argv[i], L"--check") || !wcscmp(argv[i], L"--export") || !wcscmp(argv[i], L"--expand")) return 1;
    return 0;
}

static const wchar_t *TypeName(int t) {
    switch (t) {
        case LAYOUT_TYPE_STATIC_MAP: return L"static";
        case LAYOUT_TYPE_CHORD: return L"chord";
        default: return L"hangul";
    }
}

static void JsonStr(const wchar_t *s, wchar_t *o, size_t cch) {
    size_t k = 0;
    for (; *s && k + 3 < cch; s++) {
        if (*s == L'"' || *s == L'\\') o[k++] = L'\\';
        o[k++] = *s;
    }
    o[k] = L'\0';
}

// ── 정규형 쓰기 (D4: 주석·순서는 보존하지 않는다) ──────────────────────────────────────
typedef struct { wchar_t *buf; size_t cch, o; } WBuf;
static void W(WBuf *b, const wchar_t *fmt, ...) {
    if (b->o + 1 >= b->cch) return;
    va_list ap; va_start(ap, fmt);
    int n = vswprintf(b->buf + b->o, b->cch - b->o, fmt, ap);
    va_end(ap);
    if (n > 0) b->o += (size_t)n;
}
static void WriteHeader(WBuf *b, const LayoutConfig *lc, const KlayMeta *m) {
    W(b, L"# written by jamotong --expand/--export (canonical form: comments and order are not kept)\n");
    W(b, L"FormatVersion = 2\nType = %ls\n", TypeName(lc->type));
    if (m->id[0]) W(b, L"Id = %ls\n", m->id);
    W(b, L"Name = %ls\n", lc->name ? lc->name : L"layout");
    if (lc->abbrev[0]) W(b, L"Abbrev = %ls\n", lc->abbrev);
    if (m->version[0]) W(b, L"Version = %ls\n", m->version);
    if (m->author[0]) W(b, L"Author = %ls\n", m->author);
    if (m->license[0]) W(b, L"License = %ls\n", m->license);
    if (m->homepage[0]) W(b, L"Homepage = %ls\n", m->homepage);
    if (m->description[0]) W(b, L"Description = %ls\n", m->description);
    if (m->locale[0]) W(b, L"Locale = %ls\n", m->locale);
    if (m->requires[0]) W(b, L"RequiresJamotong = %ls\n", m->requires);
}
static bool WriteCanonical(const wchar_t *src, const LayoutConfig *lc, const KlayMeta *m, WBuf *b) {
    WriteHeader(b, lc, m);
    if (lc->type == LAYOUT_TYPE_HANGUL_CUSTOM) {
        const HangulLayout *hl = (const HangulLayout*)lc->pHangulLayout;
        if (hl->moachigi) W(b, L"Moachigi = 1\n");
        for (int c = 33; c < 127; c++) {
            LayoutResult r = hl->keymap[c];
            if (r.type == JAMO_NONE) continue;
            W(b, L"Key %lc = %lc%d\n", (wchar_t)c, r.type == JAMO_CHO ? L'C' : r.type == JAMO_JUNG ? L'M' : L'T', r.index);
        }
        for (int i = 0; i < hl->combineCount; i++) {
            const HangulCombine *k = &hl->combines[i];
            W(b, L"Combine %lc %d %d = %d\n", k->type == JAMO_CHO ? L'C' : k->type == JAMO_JUNG ? L'M' : L'T', k->a, k->b, k->result);
        }
        return true;
    }
    if (lc->type == LAYOUT_TYPE_STATIC_MAP) {
        for (int c = 33; c < 256; c++)
            if (lc->charMap[c] != (wchar_t)c && lc->charMap[c] > L' ') W(b, L"Map %lc = %lc\n", (wchar_t)c, lc->charMap[c]);
        return true;
    }
    // chord: 동작 표를 다시 글로 옮기는 대신, Extends/Include 를 편 원문 줄을 쓴다(자립 파일).
    KlayLines L;
    if (!KlayLines_Build(&L, src, NULL)) { KlayLines_Free(&L); return false; }
    for (int i = 0; i < L.n; i++) {
        const wchar_t *p = L.v[i].text;
        while (*p == L' ' || *p == L'\t') p++;
        if (KlayHeader_IsKnownKey(p) || !wcsncmp(p, L"FormatVersion", 13)) continue;   // 머리부는 위에 썼다
        W(b, L"%ls\n", L.v[i].text);
    }
    KlayLines_Free(&L);
    return true;
}

static bool WriteFileText(const wchar_t *path, const wchar_t *text) {
    FILE *fp = _wfopen(path, L"w, ccs=UTF-8");
    if (!fp) return false;
    bool ok = fputws(text, fp) >= 0;
    ok = (fclose(fp) == 0) && ok;
    return ok;
}

static int Usage(KlayCliOut out, void *ctx) {
    out(L"usage:\n"
        L"  jamotong --check  <file.jmt> [--json]\n"
        L"  jamotong --export <@ko_3bul|@en_dvorak|@en_qwerty|file.jmt> -o <out.jmt>\n"
        L"  jamotong --expand <file.jmt> -o <out.jmt>\n", ctx);
    return 2;
}

int KlayCli_Run(int argc, const wchar_t *const *argv, KlayCliOut out, void *ctx) {
    const wchar_t *cmd = NULL, *arg = NULL, *outPath = NULL;
    int json = 0;
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--check") || !wcscmp(argv[i], L"--export") || !wcscmp(argv[i], L"--expand")) {
            cmd = argv[i]; if (i + 1 < argc) arg = argv[++i];
        } else if (!wcscmp(argv[i], L"-o") && i + 1 < argc) outPath = argv[++i];
        else if (!wcscmp(argv[i], L"--json")) json = 1;
    }
    if (!cmd || !arg) return Usage(out, ctx);

    // --export @내장: 표를 그대로 글로
    if (!wcscmp(cmd, L"--export") && arg[0] == L'@') {
        if (!outPath) return Usage(out, ctx);
        wchar_t *text = (wchar_t*)malloc(16384 * sizeof(wchar_t)), why[200];
        if (!text) return 1;
        bool ok = Klay_BuiltinText(arg + 1, text, 16384, why, 200);
        if (!ok) { Outf(out, ctx, L"error: cannot export '%ls': %ls\n", arg, why); free(text); return 1; }
        ok = WriteFileText(outPath, text);
        free(text);
        if (!ok) { Outf(out, ctx, L"error: cannot write '%ls'\n", outPath); return 1; }
        Outf(out, ctx, L"wrote %ls\n", outPath);
        return 0;
    }

    KlayDiag *d = (KlayDiag*)malloc(sizeof(KlayDiag));
    if (!d) return 1;
    KlayMeta m;
    LayoutConfig lc; memset(&lc, 0, sizeof(lc));
    bool ok = Klay_LoadEx(arg, &lc, d, &m);
    const wchar_t *base = arg;
    for (const wchar_t *q = arg; *q; q++) if (*q == L'\\' || *q == L'/') base = q + 1;

    if (!wcscmp(cmd, L"--check") && json) {
        out(L"{\"file\":\"", ctx);
        wchar_t e[400]; JsonStr(base, e, 400); out(e, ctx);
        Outf(out, ctx, L"\",\"ok\":%ls,\"errors\":%d,\"warnings\":%d,\"diagnostics\":[", ok ? L"true" : L"false", d->errors, d->warnings);
        for (int i = 0; i < d->count; i++) {
            const KlayDiagItem *it = &d->items[i];
            wchar_t f[200], msg[400], help[400];
            JsonStr(it->file[0] ? it->file : base, f, 200); JsonStr(it->message, msg, 400); JsonStr(it->help, help, 400);
            Outf(out, ctx, L"%ls{\"file\":\"%ls\",\"line\":%d,\"col\":%d,\"severity\":\"%ls\",\"code\":\"%ls\",\"message\":\"%ls\",\"help\":\"%ls\"}",
                 i ? L"," : L"", f, it->line, it->col, it->severity == KLAY_SEV_ERROR ? L"error" : L"warning", it->code, msg, help);
        }
        out(L"]}\n", ctx);
    } else if (d->count) {
        wchar_t *txt = (wchar_t*)malloc(16384 * sizeof(wchar_t));
        if (txt) { Klay_DiagFormat(d, base, txt, 16384); out(txt, ctx); free(txt); }
    }
    int rc = ok ? 0 : 1;
    if (!wcscmp(cmd, L"--check")) {
        if (ok && !json) Outf(out, ctx, L"%ls: OK - %ls layout '%ls', %d warning(s)\n", base, TypeName(lc.type), lc.name ? lc.name : L"", d->warnings);
    } else if (ok) {   // --expand / --export <file>
        if (!outPath) rc = Usage(out, ctx);
        else {
            WBuf b; b.cch = 65536; b.o = 0; b.buf = (wchar_t*)malloc(b.cch * sizeof(wchar_t));
            if (!b.buf) rc = 1;
            else {
                b.buf[0] = L'\0';
                if (!WriteCanonical(arg, &lc, &m, &b) || !WriteFileText(outPath, b.buf)) {
                    Outf(out, ctx, L"error: cannot write '%ls'\n", outPath); rc = 1;
                } else Outf(out, ctx, L"wrote %ls\n", outPath);
                free(b.buf);
            }
        }
    }
    if (ok) Config_FreeLayoutResources(&lc);
    free(d);
    return rc;
}
