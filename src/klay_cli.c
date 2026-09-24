// klay_cli.c — .jmt 저작 도구 명령 (RFC-0011 P3). WinAPI 무의존: 리눅스 네이티브로도 빌드된다.
#include "klay_cli.h"
#include "klay.h"
#include "hangul_layout.h"
#include "chord_layout.h"
#include "seq_layout.h"
#include "jdict_build.h"
#include "jlay.h"
#include "jlay_build.h"
#include "dict_import.h"
#include "klc_import.h"
#include "ngs_import.h"
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
        if (!wcscmp(argv[i], L"--check") || !wcscmp(argv[i], L"--export") || !wcscmp(argv[i], L"--expand")
            || !wcscmp(argv[i], L"--build-dict") || !wcscmp(argv[i], L"--build")
            || !wcscmp(argv[i], L"--build-dir") || !wcscmp(argv[i], L"--import-dict") || !wcscmp(argv[i], L"--import-klc") || !wcscmp(argv[i], L"--import-ngs")) return 1;
    return 0;
}

static const wchar_t *TypeName(int t) {
    switch (t) {
        case LAYOUT_TYPE_STATIC_MAP: return L"static";
        case LAYOUT_TYPE_PASSTHROUGH: return L"static";   // Identity = passthrough
        case LAYOUT_TYPE_CHORD: return L"chord";
        case LAYOUT_TYPE_SEQUENCE: return L"input";   // 3판 공통 표면 + Engine = sequence
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
    // 판은 원본의 것을 지킨다 — 3판 문법(문자열 동작·순차 표)을 2판으로 적으면 못 읽는 파일이 된다.
    W(b, L"FormatVersion = %d\nType = %ls\n", m->formatVersion >= 2 ? m->formatVersion : 2, TypeName(lc->type));
    if (lc->type == LAYOUT_TYPE_SEQUENCE) W(b, L"Engine = sequence\n");
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
    if (lc->type == LAYOUT_TYPE_PASSTHROUGH) { W(b, L"Identity = passthrough\n"); return true; }
    if (lc->type == LAYOUT_TYPE_STATIC_MAP) {
        for (int c = 33; c < 256; c++)
            if (lc->charMap[c] != (wchar_t)c && lc->charMap[c] > L' ') W(b, L"Map %lc = %lc\n", (wchar_t)c, lc->charMap[c]);
        return true;
    }
    if (lc->type == LAYOUT_TYPE_SEQUENCE) {
        // 표는 사전 파일에 있다 — 펼치기는 그 이름만 옮긴다 (사전을 자판 파일로 되돌리지 않는다).
        const SeqLayout *sl = (const SeqLayout*)lc->pSeqLayout;
        W(b, L"Dictionary = %ls\n", sl->dictFile);
        if (sl->onUnmatched == SEQ_UNMATCHED_CANCEL) W(b, L"OnUnmatched = cancel\n");
        return true;
    }
    // chord: 동작 표를 다시 글로 옮기는 대신, Extends/Include 를 편 원문 줄을 쓴다(자립 파일).
    //   단 같은 조합(층·키 집합·tap/hold)이 여러 번이면 로더가 고르는 한 줄만 쓴다 — 다른 파일(상속)은 나중 줄,
    //   같은 파일은 첫 줄 (chord_layout.c 와 같은 규칙, RFC-0016 §4). 안 그러면 펼친 한 파일 안의 중복이 되어
    //   첫 줄(부모)이 이기고 의미가 바뀐다 (§9: 펼치기는 의미를 보존한다).
    KlayLines L;
    if (!KlayLines_Build(&L, src, NULL)) { KlayLines_Free(&L); return false; }
    typedef struct { wchar_t id[80]; int file, line; } ChordId;
    ChordId *ids = (ChordId *)calloc((size_t)L.n + 1, sizeof(ChordId));
    int *winner = (int *)calloc((size_t)L.n + 1, sizeof(int));   // ids[i] 가 이긴 줄 번호, -1 = 조합 줄 아님
    if (!ids || !winner) { free(ids); free(winner); KlayLines_Free(&L); return false; }
    wchar_t layer[32] = L"base";
    for (int i = 0; i < L.n; i++) {
        const wchar_t *p = L.v[i].text;
        while (*p == L' ' || *p == L'\t') p++;
        wchar_t nm[32] = L"", keys[32] = L"";
        winner[i] = -1;
        if (swscanf(p, L"Layer %31ls", nm) == 1) { lstrcpynW(layer, nm, 32); continue; }
        bool hold = !wcsncmp(p, L"Hold ", 5);
        if (!hold && wcsncmp(p, L"Chord ", 6)) continue;
        if (swscanf(p + (hold ? 5 : 6), L"%31ls", keys) != 1) continue;
        size_t nk = wcslen(keys);   // 키 집합 — 순서 무시 (jk == kj)
        for (size_t a = 1; a < nk; a++) for (size_t c = a; c > 0 && keys[c-1] > keys[c]; c--) { wchar_t t = keys[c]; keys[c] = keys[c-1]; keys[c-1] = t; }
        swprintf(ids[i].id, 80, L"%ls|%ls|%d", layer, keys, hold ? 1 : 0);
        ids[i].file = L.v[i].file;
        int prev = -1;
        for (int j = 0; j < i; j++) if (winner[j] >= 0 && !wcscmp(ids[j].id, ids[i].id)) { prev = j; break; }
        if (prev < 0) winner[i] = i;
        else if (ids[prev].file != ids[i].file) { winner[i] = i; winner[prev] = -2; }   // 상속 덮어쓰기: 나중 줄
        else winner[i] = -2;                                                             // 같은 파일 중복: 첫 줄
    }
    for (int i = 0; i < L.n; i++) {
        const wchar_t *p = L.v[i].text;
        while (*p == L' ' || *p == L'\t') p++;
        if (KlayHeader_IsKnownKey(p) || !wcsncmp(p, L"FormatVersion", 13)) continue;   // 머리부는 위에 썼다
        if (winner[i] == -2) continue;                                                  // 진 조합 줄
        W(b, L"%ls\n", L.v[i].text);
    }
    free(ids); free(winner);
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
        L"  jamotong --expand <file.jmt> -o <out.jmt>\n"
        L"  jamotong --build-dict <file.jdt> -o <out.jdb>\n"
        L"  jamotong --build <file.jmt> [-o <out.jmb>]\n"
        L"  jamotong --build-dir <folder>\n"
        L"  jamotong --import-dict <file> -o <out.jdt> [--limit N] [--name ..] [--license ..]\n"
        L"  jamotong --import-klc <file.klc> -o <out.jmt>\n"
        L"  jamotong --import-ngs <file.key|.ist> -o <out.jmt>\n", ctx);
    return 2;
}

int KlayCli_Run(int argc, const wchar_t *const *argv, KlayCliOut out, void *ctx) {
    const wchar_t *cmd = NULL, *arg = NULL, *outPath = NULL;
    int json = 0, limit = 0;
    const wchar_t *dictName = NULL, *dictLicense = NULL;
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--check") || !wcscmp(argv[i], L"--export") || !wcscmp(argv[i], L"--expand")
            || !wcscmp(argv[i], L"--build-dict") || !wcscmp(argv[i], L"--build")
            || !wcscmp(argv[i], L"--build-dir") || !wcscmp(argv[i], L"--import-dict")
            || !wcscmp(argv[i], L"--import-klc") || !wcscmp(argv[i], L"--import-ngs")) {
            cmd = argv[i]; if (i + 1 < argc) arg = argv[++i];
        } else if (!wcscmp(argv[i], L"-o") && i + 1 < argc) outPath = argv[++i];
        else if (!wcscmp(argv[i], L"--limit") && i + 1 < argc) limit = (int)wcstol(argv[++i], NULL, 10);
        else if (!wcscmp(argv[i], L"--name") && i + 1 < argc) dictName = argv[++i];
        else if (!wcscmp(argv[i], L"--license") && i + 1 < argc) dictLicense = argv[++i];
        else if (!wcscmp(argv[i], L"--json")) json = 1;
    }
    if (!cmd || !arg) return Usage(out, ctx);

    // --build-dict: 사전 원본(.jdt)을 확인·검증해서 이진(.jdb)으로 굽는다 (오너 결정 2026-09-23).
    if (!wcscmp(cmd, L"--build-dict")) {
        if (!outPath) return Usage(out, ctx);
        JDictBuildResult br;
        const wchar_t *sbase = arg;
        for (const wchar_t *q = arg; *q; q++) if (*q == L'\\' || *q == L'/') sbase = q + 1;
        if (!JDict_Build(arg, outPath, &br)) {
            if (br.line > 0) Outf(out, ctx, L"%ls:%d: error: %ls\n", sbase, br.line, br.message);
            else Outf(out, ctx, L"%ls: error: %ls\n", sbase, br.message);
            if (br.help[0]) Outf(out, ctx, L"   help: %ls\n", br.help);
            Outf(out, ctx, L"   code: %ls\n", br.code);
            return 1;
        }
        // 구운 결과를 바로 다시 열어 전수 점검한다 — 못 믿을 산출물을 남기지 않는다
        JDictError derr = JDICT_OK;
        JDict *chk = JDict_Open(outPath, &derr);
        bool good = chk && JDict_Verify(chk, &derr);
        if (chk) JDict_Close(chk);
        if (!good) {
            Outf(out, ctx, L"%ls: error: the built dictionary did not pass its own check: %ls\n", sbase, JDict_ErrorText(derr));
            return 1;
        }
        Outf(out, ctx, L"wrote %ls - %d entries, longest typed %d, longest output %d\n",
             outPath, br.count, br.maxKeyLen, br.maxValLen);
        return 0;
    }

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

    // --import-dict: 남의 사전 자료를 우리 사전 원본(.jdt)으로. 자료는 사용자가 고른다 (§6.4).
    if (!wcscmp(cmd, L"--import-dict")) {
        if (!outPath) return Usage(out, ctx);
        DictImportResult ir;
        const wchar_t *sbase = arg;
        for (const wchar_t *q = arg; *q; q++) if (*q == L'\\' || *q == L'/') sbase = q + 1;
        DictImportMeta meta = { dictName, dictLicense };
        if (!DictImport_Run(arg, outPath, limit, &meta, &ir)) {
            Outf(out, ctx, L"%ls: error: %ls\n", sbase, ir.error[0] ? ir.error : L"cannot import");
            return 1;
        }
        Outf(out, ctx, L"wrote %ls - %d of %d rows (%d skipped)\n", outPath, ir.kept, ir.rows, ir.skipped);
        Outf(out, ctx, L"   next: jamotong --build-dict \"%ls\" -o <name>.jdb\n", outPath);
        Outf(out, ctx, L"   note: the entries keep the licence of the file they came from\n");
        return 0;
    }

    // --import-klc: MSKLC 자판 원본을 우리 정적 자판으로 (RFC-0011a).
    if (!wcscmp(cmd, L"--import-klc")) {
        if (!outPath) return Usage(out, ctx);
        KlcImportResult kr;
        const wchar_t *sbase = arg;
        for (const wchar_t *q = arg; *q; q++) if (*q == L'\\' || *q == L'/') sbase = q + 1;
        if (!KlcImport_Run(arg, outPath, &kr)) {
            Outf(out, ctx, L"%ls: error: %ls\n", sbase, kr.error[0] ? kr.error : L"cannot import");
            return 1;
        }
        Outf(out, ctx, L"wrote %ls - '%ls', %d keys from %d rows\n", outPath, kr.name, kr.mapped, kr.rows);
        if (kr.deadKeys || kr.ligatures || kr.skipped)
            Outf(out, ctx, L"   not carried over: %d dead key(s), %d ligature(s), %d other\n",
                 kr.deadKeys, kr.ligatures, kr.skipped);
        Outf(out, ctx, L"   next: jamotong --check \"%ls\"\n", outPath);
        return 0;
    }

    // --import-ngs: 날개셋 자판 파일(.key/.ist)의 글쇠 배열을 우리 자판으로 (RFC-0011b).
    if (!wcscmp(cmd, L"--import-ngs")) {
        if (!outPath) return Usage(out, ctx);
        NgsImportResult nr;
        const wchar_t *sbase = arg;
        for (const wchar_t *q = arg; *q; q++) if (*q == L'\\' || *q == L'/') sbase = q + 1;
        if (!NgsImport_Run(arg, outPath, &nr)) {
            Outf(out, ctx, L"%ls: error: %ls\n", sbase, nr.error[0] ? nr.error : L"cannot import");
            return 1;
        }
        Outf(out, ctx, L"wrote %ls - '%ls', %d of %d keys (%ls)\n", outPath, nr.name, nr.mapped, nr.keys,
             nr.hangul ? (nr.sebeol ? L"hangul, sebeol" : L"hangul, dubeol") : L"static layout");
        if (nr.formulas || nr.chars || nr.unknown)
            Outf(out, ctx, L"   not carried over: %d formula key(s), %d character key(s), %d unknown jamo\n",
                 nr.formulas, nr.chars, nr.unknown);
        if (nr.unknownList[0]) Outf(out, ctx, L"   unknown codes: %ls\n", nr.unknownList);
        Outf(out, ctx, L"   next: jamotong --check \"%ls\"\n", outPath);
        return 0;
    }

    // --build: 자판 원본을 구워 입력기가 읽을 파일(.jmb)을 만든다 (오너 결정 2026-09-23).
    if (!wcscmp(cmd, L"--build")) {
        wchar_t out2[MAX_PATH];
        if (!outPath) {   // -o 를 안 주면 원본 옆에 같은 이름으로
            lstrcpynW(out2, arg, MAX_PATH);
            size_t n = wcslen(out2);
            if (n < 5 || _wcsicmp(out2 + n - 4, L".jmt") != 0) { Outf(out, ctx, L"error: --build needs a .jmt file\n"); return 2; }
            wcscpy(out2 + n - 4, L".jmb");
            outPath = out2;
        }
        KlayDiag *bd = (KlayDiag*)malloc(sizeof(KlayDiag));
        if (!bd) return 1;
        const wchar_t *sbase = arg;
        for (const wchar_t *q = arg; *q; q++) if (*q == L'\\' || *q == L'/') sbase = q + 1;
        bool ok2 = JLay_Build(arg, outPath, bd);
        if (bd->count) {
            wchar_t *txt = (wchar_t*)malloc(16384 * sizeof(wchar_t));
            if (txt) { Klay_DiagFormat(bd, sbase, txt, 16384); out(txt, ctx); free(txt); }
        }
        free(bd);
        if (!ok2) { Outf(out, ctx, L"%ls: not built\n", sbase); return 1; }
        Outf(out, ctx, L"built %ls\n", outPath);
        return 0;
    }
    // --build-dir: 폴더 안의 원본 가운데 산출물이 없거나 낡은 것을 굽는다 (설치 스크립트·관리 앱).
    if (!wcscmp(cmd, L"--build-dir")) {
        int built = 0, failed = 0;
        if (!JLay_BuildDir(arg, &built, &failed)) {
            Outf(out, ctx, L"%ls: no layouts to build\n", arg);
            return 0;
        }
        Outf(out, ctx, L"%ls: %d built, %d failed\n", arg, built, failed);
        return failed ? 1 : 0;
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
        if (ok && !json) {
            if (lc.type == LAYOUT_TYPE_SEQUENCE) {
                const SeqLayout *sl = (const SeqLayout*)lc.pSeqLayout;
                Outf(out, ctx, L"%ls: OK - %ls layout '%ls' with dictionary '%ls' (%d entries), %d warning(s)\n",
                     base, TypeName(lc.type), lc.name ? lc.name : L"", sl->dictFile, JDict_Count(sl->dict), d->warnings);
                // 후보 사전도 이름을 대 준다 — 자판을 고를 때 이것까지 전수로 보므로, 무엇을 보았는지
                // 말해 주어야 사전 팩을 쓴 사람이 제 사전이 실린 것을 확인할 수 있다.
                if (sl->cand)
                    Outf(out, ctx, L"   candidates: '%ls' (%d entries)%ls%ls\n", sl->candFile, JDict_Count(sl->cand),
                         JDict_License(sl->cand)[0] ? L" - " : L"", JDict_License(sl->cand));
            } else {
                Outf(out, ctx, L"%ls: OK - %ls layout '%ls', %d warning(s)\n", base, TypeName(lc.type), lc.name ? lc.name : L"", d->warnings);
            }
            // 입력기는 구운 자판만 읽는다 — 상태만 알려 주고 여기서 굽지는 않는다 (오너 결정 2026-09-23).
            wchar_t built[MAX_PATH];
            lstrcpynW(built, arg, MAX_PATH);
            size_t bn = wcslen(built);
            if (bn > 4 && _wcsicmp(built + bn - 4, L".jmt") == 0) {
                wcscpy(built + bn - 4, L".jmb");
                if (JLay_IsStale(built, arg))
                    Outf(out, ctx, L"%ls: not built yet (or older than the source) - run 'jamotong --build %ls'\n", base, base);
                else
                    Outf(out, ctx, L"%ls: the built layout beside it is up to date\n", base);
            }
        }
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
