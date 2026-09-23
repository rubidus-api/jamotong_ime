// jlay.c — 구운 자판(.jmb)을 읽는다. 입력기 DLL 이 자판을 얻는 **유일한** 길이다 (RFC-0016 P5b-2).
//   텍스트 파서는 도구 쪽(jlay_build.c)에 있다. 여기서는 읽고, 범위를 보고, crc 를 맞춰 보고,
//   순차 자판이면 사전까지 전수 점검한다 — 성한 자판만 목록에 오른다.
#include "jlay.h"
#include "jlay_fmt.h"
#include "jdict.h"
#include "hangul_layout.h"
#include "chord_layout.h"
#include "seq_layout.h"
#include "layout.h"     // KBD_SEBEOL
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

// ── 본문 읽개 (범위를 넘으면 bad 가 서고 그 뒤는 0 을 돌려준다) ────────────────────
typedef struct { const unsigned char *p; size_t n, at; bool bad; } Rd;
static unsigned Get32(Rd *r) {
    if (r->bad || r->at + 4 > r->n) { r->bad = true; return 0; }
    unsigned v = JLayRd32(r->p + r->at); r->at += 4; return v;
}
static unsigned Get16(Rd *r) {
    if (r->bad || r->at + 2 > r->n) { r->bad = true; return 0; }
    unsigned v = JLayRd16(r->p + r->at); r->at += 2; return v;
}
static unsigned Get8(Rd *r) {
    if (r->bad || r->at + 1 > r->n) { r->bad = true; return 0; }
    return r->p[r->at++];
}
static int GetI16(Rd *r) { return (short)(unsigned short)Get16(r); }
static int GetI32(Rd *r) { return (int)Get32(r); }
// 문자열: u16 길이 + UTF-16LE. cch 는 버퍼 칸 수(NUL 포함) — 넘치면 파일을 거부한다.
static void GetStr(Rd *r, wchar_t *out, size_t cch) {
    unsigned len = Get16(r);
    if (r->bad || len + 1 > cch || r->at + (size_t)len * 2 > r->n) { r->bad = true; if (cch) out[0] = L'\0'; return; }
    for (unsigned i = 0; i < len; i++) out[i] = (wchar_t)JLayRd16(r->p + r->at + (size_t)i * 2);
    out[len] = L'\0';
    r->at += (size_t)len * 2;
}

static bool ReadStatic(Rd *r, LayoutConfig *out) {
    for (int i = 0; i < 256; i++) out->charMap[i] = (wchar_t)Get16(r);
    return !r->bad;
}
static bool ReadHangul(Rd *r, LayoutConfig *out) {
    // 해제는 HangulLayout_Free(HeapFree) 가 한다 — 같은 할당기를 써야 한다
    HangulLayout *hl = (HangulLayout *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(HangulLayout));
    if (!hl) return false;
    GetStr(r, hl->name, 64);
    hl->moachigi = GetI32(r);
    hl->composition = GetI32(r);
    for (int i = 0; i < 128; i++) {
        hl->keymap[i].type = (JamoType)Get16(r);
        hl->keymap[i].index = GetI16(r);
    }
    int n = GetI32(r);
    if (r->bad || n < 0 || n > HL_MAX_COMBINE) { HeapFree(GetProcessHeap(), 0, hl); return false; }
    hl->combineCount = n;
    for (int i = 0; i < n; i++) {
        hl->combines[i].type = (JamoType)Get16(r);
        hl->combines[i].a = GetI16(r);
        hl->combines[i].b = GetI16(r);
        hl->combines[i].result = GetI16(r);
    }
    if (r->bad) { HeapFree(GetProcessHeap(), 0, hl); return false; }
    out->pHangulLayout = hl;
    out->kbdVariant = KBD_SEBEOL;
    return true;
}
static ChordLayout *ReadChordTable(Rd *r);   // 아래에서 정의 (순차 자판의 앞단도 같은 표를 쓴다)

static bool ReadChord(Rd *r, LayoutConfig *out) {
    ChordLayout *cl = ReadChordTable(r);
    if (!cl) return false;
    out->pChordLayout = cl;
    return true;
}

static ChordLayout *ReadChordTable(Rd *r) {
    // 해제는 ChordLayout_Free(HeapFree) 가 한다. 다 읽은 뒤에는 텍스트 로더처럼 실제 조합 수만큼만
    // 남기고 줄인다 — chords[2048] 전체는 2MB 가 넘고, 그걸 모든 호스트 프로세스가 진다.
    ChordLayout *cl = (ChordLayout *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ChordLayout));
    if (!cl) return NULL;
    GetStr(r, cl->name, 64);
    cl->v3 = GetI32(r);
    cl->comboTermMs = GetI32(r);
    cl->holdTermMs = GetI32(r);
    cl->holdPolicy = GetI32(r);
    for (int i = 0; i < 128; i++) cl->keyBit[i] = GetI16(r);
    int layers = GetI32(r);
    if (r->bad || layers < 0 || layers > CL_MAX_LAYERS) { HeapFree(GetProcessHeap(), 0, cl); return NULL; }
    cl->layerCount = layers;
    for (int i = 0; i < layers; i++) GetStr(r, cl->layerNames[i], 32);
    int macros = GetI32(r);
    if (r->bad || macros < 0 || macros > CL_MAX_MACROS) { HeapFree(GetProcessHeap(), 0, cl); return NULL; }
    cl->macroCount = macros;
    for (int i = 0; i < macros; i++) {
        GetStr(r, cl->macros[i].name, 32);
        cl->macros[i].first = GetI32(r);
        cl->macros[i].count = GetI32(r);
    }
    int steps = GetI32(r);
    if (r->bad || steps < 0 || steps > CL_MAX_STEPS) { HeapFree(GetProcessHeap(), 0, cl); return NULL; }
    cl->stepCount = steps;
    for (int i = 0; i < steps; i++) {
        ChordMacroStep *s = &cl->steps[i];
        s->kind = (unsigned char)Get8(r);
        s->vk = GetI32(r); s->mod = GetI32(r);
        s->p1 = GetI32(r); s->p2 = GetI32(r); s->prof = GetI32(r);
        s->textOff = (unsigned short)Get16(r);
        s->textLen = (unsigned short)Get16(r);
    }
    int textLen = GetI32(r);
    if (r->bad || textLen < 0 || textLen > CL_MACRO_TEXT) { HeapFree(GetProcessHeap(), 0, cl); return NULL; }
    cl->macroTextLen = textLen;
    for (int i = 0; i < textLen; i++) cl->macroText[i] = (wchar_t)Get16(r);
    int chords = GetI32(r);
    if (r->bad || chords < 0 || chords > CL_MAX_CHORDS) { HeapFree(GetProcessHeap(), 0, cl); return NULL; }
    cl->chordCount = chords;
    for (int i = 0; i < chords; i++) {
        ChordEntry *e = &cl->chords[i];
        e->mask = Get32(r);
        e->layer = GetI32(r);
        e->isHold = GetI32(r);
        e->act = (ChordActionType)Get32(r);
        GetStr(r, e->text, sizeof e->text / sizeof e->text[0]);
        e->vk = GetI32(r); e->keyExt = GetI32(r); e->mod = GetI32(r);
        e->targetLayer = GetI32(r);
        e->p1 = GetI32(r); e->p2 = GetI32(r); e->prof = GetI32(r);
    }
    // 참조 무결성: 매크로 단계·레이어 번호가 실제로 있는 것을 가리켜야 한다
    for (int i = 0; i < macros && !r->bad; i++)
        if (cl->macros[i].first < 0 || cl->macros[i].count < 0 ||
            cl->macros[i].first + cl->macros[i].count > steps) r->bad = true;
    for (int i = 0; i < steps && !r->bad; i++)
        if ((size_t)cl->steps[i].textOff + cl->steps[i].textLen > (size_t)textLen) r->bad = true;
    for (int i = 0; i < chords && !r->bad; i++) {
        const ChordEntry *e = &cl->chords[i];
        if (e->layer < 0 || e->layer >= layers) r->bad = true;
        if ((e->act == CA_LAYER_ONESHOT || e->act == CA_LAYER_TOGGLE || e->act == CA_LAYER_SWITCH) &&
            (e->targetLayer < 0 || e->targetLayer >= layers)) r->bad = true;
        if (e->act == CA_MACRO && (e->p1 < 0 || e->p1 >= macros)) r->bad = true;
    }
    if (r->bad) { HeapFree(GetProcessHeap(), 0, cl); return NULL; }
    {   // 텍스트 로더와 같은 축소 (RFC-0011 P0)
        size_t need = offsetof(ChordLayout, chords) + (size_t)cl->chordCount * sizeof(ChordEntry);
        ChordLayout *shrunk = (ChordLayout *)HeapAlloc(GetProcessHeap(), 0, need);
        if (shrunk) { memcpy(shrunk, cl, need); HeapFree(GetProcessHeap(), 0, cl); cl = shrunk; }
    }
    return cl;
}
static bool ReadSeq(Rd *r, const wchar_t *jmbPath, LayoutConfig *out, JLayError *err) {
    SeqLayout *sl = (SeqLayout *)calloc(1, sizeof(SeqLayout));
    if (!sl) return false;
    GetStr(r, sl->name, 64);
    GetStr(r, sl->dictFile, 64);
    sl->onUnmatched = GetI32(r);
    GetStr(r, sl->candFile, 64);
    sl->convertVk = GetI32(r);
    unsigned hasChord = Get32(r);
    if (r->bad || !sl->dictFile[0]) { free(sl); return false; }
    if (hasChord) {                       // 앞단 조합 인식기 (§6.3)
        sl->chord = ReadChordTable(r);
        if (!sl->chord) { free(sl); return false; }
    }
    if (!SeqLayout_OpenDict(sl, jmbPath, NULL)) {   // 사전은 자판 밖에 있다 — 열고 전수 점검한다
        free(sl);
        *err = JLAY_E_DICT;
        return false;
    }
    out->pSeqLayout = sl;
    return true;
}

const wchar_t *JLay_ErrorText(JLayError e) {
    switch (e) {
        case JLAY_OK:        return L"ok";
        case JLAY_E_OPEN:    return L"the built layout cannot be opened";
        case JLAY_E_MAGIC:   return L"this is not a built Jamotong layout";
        case JLAY_E_VERSION: return L"this layout was built by a newer Jamotong";
        case JLAY_E_LAYOUT:  return L"the built layout is damaged (its parts do not fit the file)";
        case JLAY_E_CRC:     return L"the built layout does not match its checksum";
        case JLAY_E_KIND:    return L"this build holds a layout kind this Jamotong does not know";
        case JLAY_E_DICT:    return L"the dictionary this layout needs cannot be used";
        case JLAY_E_MEMORY:  return L"out of memory";
    }
    return L"unknown error";
}

static unsigned char *ReadWhole(const wchar_t *path, size_t *outLen) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz <= 0 || sz > 64L * 1024 * 1024 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return NULL; }
    *outLen = got;
    return buf;
}

bool JLay_Load(const wchar_t *path, LayoutConfig *out, JLayError *err) {
    JLayError dummy;
    if (!err) err = &dummy;
    *err = JLAY_OK;
    memset(out, 0, sizeof *out);

    size_t len = 0;
    unsigned char *file = ReadWhole(path, &len);
    if (!file) { *err = JLAY_E_OPEN; return false; }
    if (len < JLAY_HEADER || memcmp(file, JLAY_MAGIC, 8) != 0) { free(file); *err = JLAY_E_MAGIC; return false; }
    if (JLayRd32(file + JLAY_OFF_VERSION) != JLAY_FORMAT_VERSION) { free(file); *err = JLAY_E_VERSION; return false; }
    unsigned body = JLayRd32(file + JLAY_OFF_BODY);
    unsigned total = JLayRd32(file + JLAY_OFF_SIZE);
    if (total != (unsigned)len || (size_t)body + JLAY_HEADER != len) { free(file); *err = JLAY_E_LAYOUT; return false; }
    if (JDict_Crc32(file + JLAY_HEADER, body) != JLayRd32(file + JLAY_OFF_CRC)) { free(file); *err = JLAY_E_CRC; return false; }

    unsigned kind = JLayRd32(file + JLAY_OFF_KIND);
    Rd r = { file + JLAY_HEADER, body, 0, false };
    wchar_t name[64] = L"", abbrev[8] = L"";
    GetStr(&r, name, 64);
    GetStr(&r, abbrev, 8);
    out->kbdVariant = GetI32(&r);
    bool ok = false;
    switch (kind) {
        case LAYOUT_TYPE_PASSTHROUGH:
        case LAYOUT_TYPE_STATIC_MAP:    ok = ReadStatic(&r, out); break;
        case LAYOUT_TYPE_HANGUL_CUSTOM: ok = ReadHangul(&r, out); break;
        case LAYOUT_TYPE_CHORD:         ok = ReadChord(&r, out); break;
        case LAYOUT_TYPE_SEQUENCE:      ok = ReadSeq(&r, path, out, err); break;
        default: free(file); *err = JLAY_E_KIND; return false;
    }
    free(file);
    if (!ok || r.bad) {
        Config_FreeLayoutResources(out);
        memset(out, 0, sizeof *out);
        if (*err == JLAY_OK) *err = JLAY_E_LAYOUT;
        return false;
    }
    out->type = (LayoutType)kind;
    out->name = _wcsdup(name[0] ? name : L"layout");
    if (!out->name) { Config_FreeLayoutResources(out); memset(out, 0, sizeof *out); *err = JLAY_E_MEMORY; return false; }
    lstrcpynW(out->abbrev, abbrev, 8);
    return true;
}

bool JLay_IsStale(const wchar_t *jmbPath, const wchar_t *jmtPath) {
    WIN32_FILE_ATTRIBUTE_DATA src;
    if (!GetFileAttributesExW(jmtPath, GetFileExInfoStandard, &src)) return false;   // 원본이 없으면 판단하지 않는다
    size_t len = 0;
    unsigned char *file = ReadWhole(jmbPath, &len);
    if (!file) return true;                                                          // 산출물이 없다
    bool stale = true;
    if (len >= JLAY_HEADER && memcmp(file, JLAY_MAGIC, 8) == 0 &&
        JLayRd32(file + JLAY_OFF_VERSION) == JLAY_FORMAT_VERSION &&   // 판이 다르면 낡은 것으로 본다
        JLayRd32(file + JLAY_OFF_SRCSIZE) == src.nFileSizeLow &&
        JLayRd32(file + JLAY_OFF_MTIMEL) == src.ftLastWriteTime.dwLowDateTime &&
        JLayRd32(file + JLAY_OFF_MTIMEH) == src.ftLastWriteTime.dwHighDateTime)
        stale = false;
    free(file);
    return stale;
}
