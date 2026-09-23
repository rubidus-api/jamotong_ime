// jlay_build.c — 자판 원본(.jmt)을 텍스트 로더로 읽어 검사하고, 이진(.jmb)으로 굽는다.
//   구조체를 그대로 덤프하지 않는다: 고정폭 리틀엔디안으로 **필드마다** 적어, x64 와 x86 이
//   같은 파일을 읽고 구조체가 바뀌어도 파일 판으로 다룰 수 있게 한다 (RFC-0016 P5b-2).
#include "jlay_build.h"
#include "jlay.h"
#include "jlay_fmt.h"
#include "jdict.h"          // JDict_Crc32
#include "hangul_layout.h"
#include "chord_layout.h"
#include "seq_layout.h"
#include "version.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ── 늘어나는 바이트 버퍼 ───────────────────────────────────────────────────────────
typedef struct { unsigned char *p; size_t n, cap; bool bad; } Buf;
static void BufPut(Buf *b, const void *data, size_t n) {
    if (b->bad) return;
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 4096;
        while (cap < b->n + n) cap *= 2;
        unsigned char *q = (unsigned char *)realloc(b->p, cap);
        if (!q) { b->bad = true; return; }
        b->p = q; b->cap = cap;
    }
    memcpy(b->p + b->n, data, n);
    b->n += n;
}
static void Put32(Buf *b, unsigned v) { unsigned char t[4]; JLayWr32(t, v); BufPut(b, t, 4); }
static void Put16(Buf *b, unsigned v) { unsigned char t[2]; JLayWr16(t, v); BufPut(b, t, 2); }
static void Put8(Buf *b, unsigned v)  { unsigned char t = (unsigned char)v; BufPut(b, &t, 1); }
// 문자열: u16 길이 + UTF-16LE (NUL 없음)
static void PutStr(Buf *b, const wchar_t *s) {
    size_t n = s ? wcslen(s) : 0;
    if (n > 0xFFFF) n = 0xFFFF;
    Put16(b, (unsigned)n);
    for (size_t i = 0; i < n; i++) Put16(b, (unsigned)(unsigned short)s[i]);
}

// ── 종류별 본문 ────────────────────────────────────────────────────────────────────
static void WriteStatic(Buf *b, const LayoutConfig *lc) {
    for (int i = 0; i < 256; i++) Put16(b, (unsigned)(unsigned short)lc->charMap[i]);
}
static void WriteHangul(Buf *b, const HangulLayout *hl) {
    PutStr(b, hl->name);
    Put32(b, (unsigned)hl->moachigi);
    Put32(b, (unsigned)hl->composition);
    for (int i = 0; i < 128; i++) {
        Put16(b, (unsigned)hl->keymap[i].type);
        Put16(b, (unsigned)(unsigned short)(short)hl->keymap[i].index);
    }
    Put32(b, (unsigned)hl->combineCount);
    for (int i = 0; i < hl->combineCount; i++) {
        Put16(b, (unsigned)hl->combines[i].type);
        Put16(b, (unsigned)(unsigned short)(short)hl->combines[i].a);
        Put16(b, (unsigned)(unsigned short)(short)hl->combines[i].b);
        Put16(b, (unsigned)(unsigned short)(short)hl->combines[i].result);
    }
}
static void WriteChord(Buf *b, const ChordLayout *cl) {
    PutStr(b, cl->name);
    Put32(b, (unsigned)cl->v3);
    Put32(b, (unsigned)cl->comboTermMs);
    Put32(b, (unsigned)cl->holdTermMs);
    Put32(b, (unsigned)cl->holdPolicy);
    for (int i = 0; i < 128; i++) Put16(b, (unsigned)(unsigned short)(short)cl->keyBit[i]);
    Put32(b, (unsigned)cl->layerCount);
    for (int i = 0; i < cl->layerCount; i++) PutStr(b, cl->layerNames[i]);
    Put32(b, (unsigned)cl->macroCount);
    for (int i = 0; i < cl->macroCount; i++) {
        PutStr(b, cl->macros[i].name);
        Put32(b, (unsigned)cl->macros[i].first);
        Put32(b, (unsigned)cl->macros[i].count);
    }
    Put32(b, (unsigned)cl->stepCount);
    for (int i = 0; i < cl->stepCount; i++) {
        const ChordMacroStep *s = &cl->steps[i];
        Put8(b, s->kind);
        Put32(b, (unsigned)s->vk); Put32(b, (unsigned)s->mod);
        Put32(b, (unsigned)s->p1); Put32(b, (unsigned)s->p2); Put32(b, (unsigned)s->prof);
        Put16(b, s->textOff); Put16(b, s->textLen);
    }
    Put32(b, (unsigned)cl->macroTextLen);
    for (int i = 0; i < cl->macroTextLen; i++) Put16(b, (unsigned)(unsigned short)cl->macroText[i]);
    Put32(b, (unsigned)cl->chordCount);
    for (int i = 0; i < cl->chordCount; i++) {
        const ChordEntry *e = &cl->chords[i];
        Put32(b, e->mask);
        Put32(b, (unsigned)e->layer);
        Put32(b, (unsigned)e->isHold);
        Put32(b, (unsigned)e->act);
        PutStr(b, e->text);
        Put32(b, (unsigned)e->vk); Put32(b, (unsigned)e->keyExt); Put32(b, (unsigned)e->mod);
        Put32(b, (unsigned)e->targetLayer);
        Put32(b, (unsigned)e->p1); Put32(b, (unsigned)e->p2); Put32(b, (unsigned)e->prof);
        Put32(b, (unsigned)e->holdOneshot);   // 판 4: Hold 에 건 원샷인가 (momentary 와 구분)
    }
}
static void WriteSeq(Buf *b, const SeqLayout *sl) {
    PutStr(b, sl->name);
    PutStr(b, sl->dictFile);
    Put32(b, (unsigned)sl->onUnmatched);
    PutStr(b, sl->candFile);                           // 후보 사전 이름 (없으면 빈 문자열, §6.4)
    Put32(b, (unsigned)sl->convertVk);                 // 변환 글쇠 (0 = 후보 없음)
    Put32(b, sl->chord ? 1u : 0u);                     // 앞단 조합 인식기가 있는가 (§6.3)
    if (sl->chord) WriteChord(b, (const ChordLayout *)sl->chord);
}

// 원본의 크기·수정시각 (낡음 판정에 쓴다)
static void SourceStamp(const wchar_t *path, unsigned *size, unsigned *mtimeL, unsigned *mtimeH) {
    *size = 0; *mtimeL = 0; *mtimeH = 0;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) {
        *size = fad.nFileSizeLow;
        *mtimeL = fad.ftLastWriteTime.dwLowDateTime;
        *mtimeH = fad.ftLastWriteTime.dwHighDateTime;
    }
}

bool JLay_Build(const wchar_t *srcPath, const wchar_t *outPath, KlayDiag *diag) {
    KlayDiag local;
    if (!diag) diag = &local;
    LayoutConfig lc;
    memset(&lc, 0, sizeof lc);
    if (!Klay_Load(srcPath, &lc, diag)) return false;   // 문법·사전 점검은 텍스트 로더가 다 한다

    Buf b = {0};
    PutStr(&b, lc.name ? lc.name : L"");
    PutStr(&b, lc.abbrev);
    Put32(&b, (unsigned)lc.kbdVariant);
    switch (lc.type) {
        case LAYOUT_TYPE_PASSTHROUGH:
        case LAYOUT_TYPE_STATIC_MAP:    WriteStatic(&b, &lc); break;
        case LAYOUT_TYPE_HANGUL_CUSTOM: WriteHangul(&b, (const HangulLayout *)lc.pHangulLayout); break;
        case LAYOUT_TYPE_CHORD:         WriteChord(&b, (const ChordLayout *)lc.pChordLayout); break;
        case LAYOUT_TYPE_SEQUENCE:      WriteSeq(&b, (const SeqLayout *)lc.pSeqLayout); break;
        default:
            KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMB-KIND", L"this layout kind cannot be built", NULL);
            Config_FreeLayoutResources(&lc);
            free(b.p);
            return false;
    }
    const unsigned kind = (unsigned)lc.type;
    Config_FreeLayoutResources(&lc);
    if (b.bad) { free(b.p); KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMB-MEMORY", L"out of memory", NULL); return false; }

    unsigned total = JLAY_HEADER + (unsigned)b.n;
    unsigned char *file = (unsigned char *)calloc(1, total);
    if (!file) { free(b.p); KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMB-MEMORY", L"out of memory", NULL); return false; }
    memcpy(file, JLAY_MAGIC, 8);
    JLayWr32(file + JLAY_OFF_VERSION, JLAY_FORMAT_VERSION);
    JLayWr32(file + JLAY_OFF_KIND, kind);
    JLayWr32(file + JLAY_OFF_BODY, (unsigned)b.n);
    JLayWr32(file + JLAY_OFF_CRC, JDict_Crc32(b.p, (unsigned long)b.n));
    JLayWr32(file + JLAY_OFF_SIZE, total);
    unsigned ss, ml, mh;
    SourceStamp(srcPath, &ss, &ml, &mh);
    JLayWr32(file + JLAY_OFF_SRCSIZE, ss);
    JLayWr32(file + JLAY_OFF_MTIMEL, ml);
    JLayWr32(file + JLAY_OFF_MTIMEH, mh);
    JLayWr32(file + JLAY_OFF_BUILTBY,
             (unsigned)(JAMOTONG_VERSION_MAJOR * 10000 + JAMOTONG_VERSION_MINOR * 100 + JAMOTONG_VERSION_PATCH));
    memcpy(file + JLAY_HEADER, b.p, b.n);
    free(b.p);

    FILE *out = _wfopen(outPath, L"wb");
    bool wrote = out && fwrite(file, 1, total, out) == total;
    if (out) wrote = (fclose(out) == 0) && wrote;
    free(file);
    if (!wrote) {
        _wremove(outPath);
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMB-WRITE", L"cannot write the built layout",
                     L"check that the folder exists and is writable");
        return false;
    }
    return true;
}

bool JLay_BuildDir(const wchar_t *dir, int *built, int *failed) {
    if (built) *built = 0;
    if (failed) *failed = 0;
    if (!dir || !dir[0]) return false;
    wchar_t pat[MAX_PATH];
    _snwprintf(pat, MAX_PATH, L"%ls\\*.jmt", dir);
    pat[MAX_PATH - 1] = L'\0';
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        wchar_t src[MAX_PATH], out[MAX_PATH];
        _snwprintf(src, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
        src[MAX_PATH - 1] = L'\0';
        size_t n = wcslen(src);
        if (n < 4 || n + 1 >= MAX_PATH) continue;
        lstrcpynW(out, src, MAX_PATH);
        wcscpy(out + n - 4, L".jmb");
        if (!JLay_IsStale(out, src)) continue;         // 이미 성한 산출물이 있으면 그대로 둔다
        KlayDiag d;
        if (JLay_Build(src, out, &d)) { if (built) (*built)++; }
        else { if (failed) (*failed)++; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}
