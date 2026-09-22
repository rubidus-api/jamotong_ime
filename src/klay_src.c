// klay_src.c — .jmt 줄 원천: Extends/Include 풀기와 내장 자판 텍스트 (RFC-0011 P4). WinAPI 무의존.
#include "klay.h"
#include "layout.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define KLAY_MAX_DEPTH 4

// ── 내장 자판 텍스트 ───────────────────────────────────────────────────────────────
static size_t Appendf(wchar_t *out, size_t cch, size_t o, const wchar_t *fmt, ...) {
    if (o + 1 >= cch) return o;
    va_list ap; va_start(ap, fmt);
    int n = vswprintf(out + o, cch - o, fmt, ap);
    va_end(ap);
    return n > 0 ? o + (size_t)n : o;
}

bool Klay_BuiltinText(const wchar_t *name, wchar_t *out, size_t cch, wchar_t *why, size_t whyCch) {
    if (why && whyCch) why[0] = L'\0';
    if (!out || cch < 64) return false;
    out[0] = L'\0';
    size_t o = 0;
    if (!wcscmp(name, L"ko_3bul")) {
        o = Appendf(out, cch, o, L"FormatVersion = 2\nType = hangul\nId = jamotong.ko_3bul\nName = ko_3bul\nAbbrev = KO3B\n");
        for (int c = 33; c < 127; c++) {
            LayoutResult r = Layout_MapKeyToJamo((wchar_t)c, KBD_SEBEOL);
            if (r.type == JAMO_NONE) continue;
            wchar_t t = r.type == JAMO_CHO ? L'C' : r.type == JAMO_JUNG ? L'M' : L'T';
            o = Appendf(out, cch, o, L"Key %lc = %lc%d\n", (wchar_t)c, t, r.index);
        }
        for (int a = 0; a <= 18; a++) for (int b = 0; b <= 18; b++) {
            int r = Layout_CombineCho(a, b); if (r >= 0) o = Appendf(out, cch, o, L"Combine C %d %d = %d\n", a, b, r);
        }
        for (int a = 0; a <= 20; a++) for (int b = 0; b <= 20; b++) {
            int r = Layout_CombineJung(a, b); if (r >= 0) o = Appendf(out, cch, o, L"Combine M %d %d = %d\n", a, b, r);
        }
        for (int a = 1; a <= 27; a++) for (int b = 1; b <= 27; b++) {
            int r = Layout_CombineJongPair(a, b); if (r >= 0) o = Appendf(out, cch, o, L"Combine T %d %d = %d\n", a, b, r);
        }
        return o + 1 < cch;
    }
    if (!wcscmp(name, L"ko_2bul")) {   // RFC-0016 P2: 두벌식도 파일로 — 기본값에 기대지 않고 표를 모두 쓴다
        o = Appendf(out, cch, o, L"FormatVersion = 2\nType = hangul\nComposition = dubeol\nId = jamotong.ko_2bul\nName = ko_2bul\nAbbrev = KO2B\n");
        for (int c = 33; c < 127; c++) {
            LayoutResult r = Layout_MapKeyToJamo((wchar_t)c, KBD_DUBEOL);
            if (r.type == JAMO_NONE) continue;
            wchar_t t = r.type == JAMO_CHO ? L'C' : r.type == JAMO_JUNG ? L'M' : L'T';
            o = Appendf(out, cch, o, L"Key %lc = %lc%d\n", (wchar_t)c, t, r.index);
        }
        for (int a = 0; a <= 20; a++) for (int b = 0; b <= 20; b++) {
            int r = Layout_CombineJung(a, b); if (r >= 0) o = Appendf(out, cch, o, L"Combine M %d %d = %d\n", a, b, r);
        }
        // 겹받침: 받침 a 뒤에 초성 c 가 오면 — 표는 (a, c 를 받침으로 바꾼 번호) 로 쓴다
        for (int a = 1; a <= 27; a++) for (int c = 0; c <= 18; c++) {
            int r = Layout_CombineJong(a, c), t2 = Layout_ChoToJong(c);
            if (r >= 0 && t2 > 0) o = Appendf(out, cch, o, L"Combine T %d %d = %d\n", a, t2, r);
        }
        return o + 1 < cch;
    }
    if (!wcscmp(name, L"en_dvorak") || !wcscmp(name, L"en_qwerty")) {
        bool dv = !wcscmp(name, L"en_dvorak");
        o = Appendf(out, cch, o, L"FormatVersion = 2\nType = static\nId = jamotong.%ls\nName = %ls\nAbbrev = %ls\n",
                    name, name, dv ? L"ENDV" : L"ENQW");
        if (!dv) o = Appendf(out, cch, o, L"Identity = passthrough\n");   // RFC-0016 §5.2: 내장 QWERTY 와 같게 통과
        if (dv) {
            wchar_t m[256]; for (int i = 0; i < 256; i++) m[i] = (wchar_t)i;
            Layout_FillDvorak(m);
            for (int c = 33; c < 127; c++) if (m[c] != (wchar_t)c) o = Appendf(out, cch, o, L"Map %lc = %lc\n", (wchar_t)c, m[c]);
        }
        return o + 1 < cch;
    }
    if (why && whyCch) {
        swprintf(why, whyCch, L"built-in layouts that can be used: @ko_2bul, @ko_3bul, @en_dvorak, @en_qwerty");
    }
    return false;
}

// ── 줄 목록 ────────────────────────────────────────────────────────────────────────
static bool Push(KlayLines *L, const wchar_t *text, int line, int file) {
    if (L->n == L->cap) {
        int nc = L->cap ? L->cap * 2 : 128;
        KlayLine *nv = (KlayLine*)realloc(L->v, sizeof(KlayLine) * (size_t)nc);
        if (!nv) return false;
        L->v = nv; L->cap = nc;
    }
    size_t len = wcslen(text);
    wchar_t *t = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    if (!t) return false;
    wmemcpy(t, text, len + 1);
    L->v[L->n].text = t; L->v[L->n].line = line; L->v[L->n].file = file;
    L->n++;
    return true;
}

void KlayLines_Free(KlayLines *L) {
    if (!L) return;
    for (int i = 0; i < L->n; i++) free(L->v[i].text);
    free(L->v);
    memset(L, 0, sizeof(*L));
}

typedef struct { wchar_t **text; int n; } RawLines;
static void RawFree(RawLines *r) { for (int i = 0; i < r->n; i++) free(r->text[i]); free(r->text); r->text = NULL; r->n = 0; }
static bool RawAdd(RawLines *r, const wchar_t *s, size_t len) {
    wchar_t **nt = (wchar_t**)realloc(r->text, sizeof(wchar_t*) * (size_t)(r->n + 1));
    if (!nt) return false;
    r->text = nt;
    wchar_t *t = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    if (!t) return false;
    wmemcpy(t, s, len); t[len] = L'\0';
    while (len > 0 && (t[len-1] == L'\n' || t[len-1] == L'\r')) t[--len] = L'\0';
    r->text[r->n++] = t;
    return true;
}
// RFC-0008 W2-02: 255자를 넘는 줄은 예전엔 둘로 쪼개져 두 줄로 읽혔다(뒷조각이 엉뚱한 지시문이 됨).
// 이제 그 줄 번호를 돌려주고 호출자가 오류로 낸다. 줄 수도 상한(KLAY_MAX_LINES)을 둔다.
#define KLAY_MAX_LINES 20000
// 파일에서 읽다 생긴 문제 — 하나라도 있으면 그 파일은 쓰지 않는다 (RFC-0008 W2-02).
typedef struct {
    int  longLine;   // 255자를 넘은 첫 줄 (1부터, 0=없음)
    int  badText;    // U+FFFD(잘못된 UTF-8 이 바뀐 자리)가 든 첫 줄
    bool tooMany;    // KLAY_MAX_LINES 초과
    bool readErr;    // 읽다 멈춤 — 입출력 오류 또는 CRT 가 거부한 UTF-8 (남은 줄을 조용히 버리지 않는다)
} ReadIssues;

static bool ReadFileLines(const wchar_t *path, RawLines *r, ReadIssues *is) {
    memset(is, 0, sizeof(*is));
    FILE *fp = _wfopen(path, L"r, ccs=UTF-8");
    if (!fp) return false;
    wchar_t buf[256];
    bool ok = true;
    while (ok && fgetws(buf, 256, fp)) {
        size_t n = wcslen(buf);
        if (n == 255 && buf[254] != L'\n') {   // 줄이 버퍼를 넘친다 — 나머지는 버리고 표시
            if (!is->longLine) is->longLine = r->n + 1;
            wint_t c;
            while ((c = fgetwc(fp)) != WEOF && c != L'\n') { }
        }
        if (!is->badText && wcschr(buf, 0xFFFD)) is->badText = r->n + 1;
        if (r->n >= KLAY_MAX_LINES) { is->tooMany = true; break; }
        ok = RawAdd(r, buf, n);
    }
    if (ok && !is->tooMany && ferror(fp)) is->readErr = true;
    fclose(fp);
    return ok;
}
static void SplitText(const wchar_t *text, RawLines *r) {
    const wchar_t *p = text;
    while (*p) {
        const wchar_t *e = wcschr(p, L'\n');
        size_t len = e ? (size_t)(e - p) : wcslen(p);
        if (!RawAdd(r, p, len)) return;
        p += len + (e ? 1 : 0);
    }
}

static const wchar_t *Skip(const wchar_t *p) { while (*p == L' ' || *p == L'\t') p++; return p; }
// `Key = value` 꼴에서 value (앞뒤 공백 제거) — 아니면 NULL
static const wchar_t *ValueOf(const wchar_t *line, const wchar_t *key, wchar_t *out, size_t cch) {
    const wchar_t *p = Skip(line);
    size_t k = wcslen(key);
    if (wcsncmp(p, key, k) != 0) return NULL;
    p = Skip(p + k);
    if (*p != L'=') return NULL;
    p = Skip(p + 1);
    lstrcpynW(out, p, (int)cch);
    size_t n = wcslen(out);
    while (n > 0 && (out[n-1] == L' ' || out[n-1] == L'\t' || out[n-1] == L'\r')) out[--n] = L'\0';
    const wchar_t *hash = wcschr(out, L'#');   // 꼬리 주석
    if (hash) { out[hash - out] = L'\0'; n = wcslen(out); while (n > 0 && out[n-1] == L' ') out[--n] = L'\0'; }
    return out;
}

static const wchar_t *Basename(const wchar_t *p) {
    const wchar_t *b = p;
    for (const wchar_t *q = p; *q; q++) if (*q == L'\\' || *q == L'/') b = q + 1;
    return b;
}

typedef struct { wchar_t path[16][MAX_PATH]; int n; } Stack;
static void Canon(const wchar_t *in, wchar_t *out, size_t cch) {
    lstrcpynW(out, in, (int)cch);
    for (wchar_t *q = out; *q; q++) if (*q == L'/') *q = L'\\';
}
static bool Resolve(const wchar_t *from, const wchar_t *target, wchar_t *out, size_t cch) {
    const wchar_t *t = target;
    if (t[0] == L'.' && (t[1] == L'/' || t[1] == L'\\')) t += 2;
    bool abs = (t[0] == L'\\' || t[0] == L'/' || (t[0] && t[1] == L':'));
    if (abs) { lstrcpynW(out, t, (int)cch); return true; }
    const wchar_t *b = Basename(from);
    size_t dirLen = (size_t)(b - from);
    if (dirLen + wcslen(t) + 1 >= cch) return false;
    wmemcpy(out, from, dirLen);
    wcscpy(out + dirLen, t);
    return true;
}

static int AddFile(KlayLines *L, const wchar_t *name) {
    if (L->nfiles >= KLAY_MAX_FILES) return L->nfiles - 1;
    lstrcpynW(L->files[L->nfiles], name, 64);
    return L->nfiles++;
}

// 한 원천(파일 또는 @내장)을 목록에 붙인다. typeOut 에 그 원천의 Type(없으면 빈 값). 반환 = 오류 없음.
static bool AddSource(KlayLines *L, const wchar_t *path, const wchar_t *builtin, int depth, bool allowExtends,
                      Stack *st, KlayDiag *d, wchar_t *typeOut) {
    RawLines raw = {0};
    wchar_t label[64];
    typeOut[0] = L'\0';
    if (builtin) {
        wchar_t *text = (wchar_t*)malloc(16384 * sizeof(wchar_t)), why[200];
        if (!text) return false;
        bool ok = Klay_BuiltinText(builtin, text, 16384, why, 200);
        if (ok) SplitText(text, &raw);
        free(text);
        if (!ok) return false;   // 호출자가 진단을 낸다
        swprintf(label, 64, L"@%ls", builtin);
    } else {
        ReadIssues is;
        if (!ReadFileLines(path, &raw, &is)) {
            KlayDiag_Add(d, KLAY_SEV_ERROR, 0, 0, L"E-JMT-OPEN", L"cannot open file", NULL);
            RawFree(&raw);
            return false;
        }
        lstrcpynW(label, Basename(path), 64);
        if (is.longLine || is.tooMany || is.badText || is.readErr) {
            int fi = AddFile(L, label);
            const wchar_t *pf = d ? d->curFile : NULL;
            if (d) d->curFile = L->files[fi];
            if (is.longLine) KlayDiag_Add(d, KLAY_SEV_ERROR, is.longLine, 256, L"E-JMT-LINE-LONG", L"line is longer than 255 characters",
                                          L"split it into several lines (a key list can be written over several Key lines)");
            if (is.tooMany) KlayDiag_Add(d, KLAY_SEV_ERROR, 0, 0, L"E-JMT-TOO-LONG", L"file has more than 20000 lines", NULL);
            if (is.badText) KlayDiag_Add(d, KLAY_SEV_ERROR, is.badText, 0, L"E-JMT-ENCODING", L"line is not valid UTF-8 (or contains U+FFFD)",
                                         L"save the file as UTF-8");
            if (is.readErr) KlayDiag_Add(d, KLAY_SEV_ERROR, raw.n + 1, 0, L"E-JMT-READ", L"reading stopped here (read error or invalid UTF-8)",
                                         L"save the file as UTF-8; the lines after this point were not read");
            if (d) d->curFile = pf;
            RawFree(&raw);
            return false;
        }
    }
    int fidx = AddFile(L, label);
    const wchar_t *prevFile = d ? d->curFile : NULL;
    if (d) d->curFile = L->files[fidx];
    bool ok = true;
    wchar_t v[MAX_PATH];

    // 1) Extends — 한 줄만. 기반 자판의 줄을 먼저 붙인다.
    int extLine = 0;
    for (int i = 0; i < raw.n; i++) {
        if (ValueOf(raw.text[i], L"Type", v, 32) && !typeOut[0]) lstrcpynW(typeOut, v, 32);
        if (!ValueOf(raw.text[i], L"Extends", v, MAX_PATH)) continue;
        int col = (int)(Skip(raw.text[i]) - raw.text[i]) + 1;
        if (!allowExtends) {
            KlayDiag_Add(d, KLAY_SEV_ERROR, i + 1, col, L"E-JMT-EXTENDS-INCLUDE", L"an included fragment cannot use Extends",
                         L"put Extends in the main layout file");
            ok = false; continue;
        }
        if (extLine) {
            KlayDiag_Add(d, KLAY_SEV_ERROR, i + 1, col, L"E-JMT-EXTENDS-TWICE", L"only one Extends line is allowed",
                         L"use Include for extra fragments");
            ok = false; continue;
        }
        extLine = i + 1;
        if (depth >= KLAY_MAX_DEPTH) {
            KlayDiag_Add(d, KLAY_SEV_ERROR, i + 1, col, L"E-JMT-DEPTH", L"Extends chain is deeper than 4",
                         L"flatten a level with 'jamotong.exe --expand'");
            ok = false; continue;
        }
        wchar_t baseType[32];
        bool bok;
        if (v[0] == L'@') {
            wchar_t why[200];
            wchar_t *probe = (wchar_t*)malloc(16384 * sizeof(wchar_t));
            bool can = probe && Klay_BuiltinText(v + 1, probe, 16384, why, 200);
            free(probe);
            if (!can) {
                wchar_t msg[160]; swprintf(msg, 160, L"built-in layout '%ls' cannot be extended", v);
                KlayDiag_Add(d, KLAY_SEV_ERROR, i + 1, col, L"E-JMT-BUILTIN", msg, why);
                ok = false; continue;
            }
            bok = AddSource(L, NULL, v + 1, depth + 1, true, st, d, baseType);
        } else {
            wchar_t full[MAX_PATH], canon[MAX_PATH];
            if (!Resolve(path ? path : L"", v, full, MAX_PATH)) { ok = false; continue; }
            Canon(full, canon, MAX_PATH);
            bool cyc = false;
            for (int s = 0; s < st->n; s++) if (!_wcsicmp(st->path[s], canon)) cyc = true;
            if (cyc) {
                wchar_t msg[160]; swprintf(msg, 160, L"Extends cycle: '%ls' is already being loaded", v);
                KlayDiag_Add(d, KLAY_SEV_ERROR, i + 1, col, L"E-JMT-CYCLE", msg, L"remove the loop between these files");
                ok = false; continue;
            }
            if (st->n < 16) lstrcpynW(st->path[st->n++], canon, MAX_PATH);
            bok = AddSource(L, full, NULL, depth + 1, true, st, d, baseType);
            st->n--;
        }
        if (d) d->curFile = L->files[fidx];
        if (!bok) { ok = false; continue; }
        const wchar_t *mine = typeOut[0] ? typeOut : L"hangul";
        const wchar_t *theirs = baseType[0] ? baseType : L"hangul";
        if (_wcsicmp(mine, theirs) != 0) {
            wchar_t msg[160]; swprintf(msg, 160, L"Type %ls cannot extend a %ls layout", mine, theirs);
            KlayDiag_Add(d, KLAY_SEV_ERROR, i + 1, col, L"E-JMT-EXTENDS-TYPE", msg, L"use the same Type as the base layout");
            ok = false;
        }
    }
    // 2) 자기 줄 — Include 는 그 자리에 조각을 편다, Extends 줄은 이미 처리했다.
    //    P6: `Begin <지시문> [공통 인자]` … `End` 블록은 안쪽 줄마다 머리를 붙여 한 줄 지시문으로 편다
    //    (파서는 여전히 한 줄씩 읽는다). 중첩 없음, 빠진 End·짝 없는 End 는 오류.
    wchar_t blockHead[128] = L"";
    int blockLine = 0;
    for (int i = 0; ok && i < raw.n; i++) {
        const wchar_t *t = Skip(raw.text[i]);
        const int col = (int)(t - raw.text[i]) + 1;
        if (!wcsncmp(t, L"Begin ", 6) || !wcscmp(t, L"Begin")) {
            if (blockLine) {
                KlayDiag_Add(d, KLAY_SEV_ERROR, i + 1, col, L"E-JMT-BLOCK", L"Begin inside another Begin block", L"close the first block with End");
                ok = false; continue;
            }
            lstrcpynW(blockHead, Skip(t + 5), 128);
            size_t hn = wcslen(blockHead);
            while (hn > 0 && (blockHead[hn-1] == L' ' || blockHead[hn-1] == L'\t' || blockHead[hn-1] == L'\r')) blockHead[--hn] = L'\0';
            if (!blockHead[0]) {
                KlayDiag_Add(d, KLAY_SEV_ERROR, i + 1, col, L"E-JMT-BLOCK", L"Begin needs a directive", L"e.g. 'Begin Combine C'");
                ok = false; continue;
            }
            blockLine = i + 1;
            continue;
        }
        if (!wcscmp(t, L"End")) {
            if (!blockLine) {
                KlayDiag_Add(d, KLAY_SEV_ERROR, i + 1, col, L"E-JMT-BLOCK", L"End without Begin", NULL);
                ok = false; continue;
            }
            blockLine = 0; blockHead[0] = L'\0';
            continue;
        }
        if (blockLine) {
            if (*t == L'\0' || *t == L'#') continue;
            wchar_t joined[512];
            swprintf(joined, 512, L"%ls %ls", blockHead, t);
            if (!Push(L, joined, i + 1, fidx)) ok = false;
            continue;
        }
        if (ValueOf(raw.text[i], L"Extends", v, MAX_PATH)) continue;
        if (ValueOf(raw.text[i], L"Include", v, MAX_PATH)) {
            int col = (int)(Skip(raw.text[i]) - raw.text[i]) + 1;
            wchar_t full[MAX_PATH], t2[32];
            if (depth >= KLAY_MAX_DEPTH || !path || !Resolve(path, v, full, MAX_PATH)) {
                KlayDiag_Add(d, KLAY_SEV_ERROR, i + 1, col, L"E-JMT-INCLUDE", L"cannot include this file here", NULL);
                ok = false; continue;
            }
            if (!AddSource(L, full, NULL, depth + 1, false, st, d, t2)) ok = false;
            if (d) d->curFile = L->files[fidx];
            continue;
        }
        if (!Push(L, raw.text[i], i + 1, fidx)) ok = false;
    }
    if (ok && blockLine) {
        KlayDiag_Add(d, KLAY_SEV_ERROR, blockLine, 1, L"E-JMT-BLOCK", L"Begin block is never closed", L"add 'End' after the block");
        ok = false;
    }
    RawFree(&raw);
    if (d) d->curFile = prevFile;
    return ok;
}

bool KlayLines_Build(KlayLines *L, const wchar_t *path, KlayDiag *d) {
    memset(L, 0, sizeof(*L));
    Stack *st = (Stack*)calloc(1, sizeof(Stack));
    if (!st) return false;
    Canon(path, st->path[0], MAX_PATH); st->n = 1;
    if (d) lstrcpynW(d->topFile, Basename(path), 64);
    wchar_t t[32];
    bool ok = AddSource(L, path, NULL, 0, true, st, d, t);
    free(st);
    return ok;
}
