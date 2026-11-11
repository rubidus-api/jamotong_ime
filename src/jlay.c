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
#include "fsm.h"        // JamoType, HANGUL_*_COUNT
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

// 파일에서 온 값은 쓰는 곳이 믿고 인덱스로 쓴다 — 읽는 자리에서 범위를 확인한다 (구운 자판은 CRC 만 맞추면 누구나 만들 수 있다).
static int JamoLimit(int type) {   // 자소 종류별 인덱스 수 (JAMO_NONE 은 인덱스가 없다)
    switch (type) {
        case JAMO_CHO:  return HANGUL_CHO_COUNT;
        case JAMO_JUNG: return HANGUL_JUNG_COUNT;
        case JAMO_JONG: return HANGUL_JONG_COUNT;
    }
    return 0;
}
static bool JamoRefOk(int type, int index) { return type == JAMO_NONE ? true : (type > JAMO_NONE && type <= JAMO_JONG && index >= 0 && index < JamoLimit(type)); }

static bool ReadStatic(Rd *r, LayoutConfig *out) {
    for (int i = 0; i < 256; i++) out->charMap[i] = (wchar_t)Get16(r);
    return !r->bad;
}
static ChordLayout *ReadChordTable(Rd *r);   // 아래에서 정의 (순차·한글 자판의 앞단도 같은 표를 쓴다)
static bool ReadHangul(Rd *r, LayoutConfig *out) {
    // 해제는 HangulLayout_Free(HeapFree) 가 한다 — 같은 할당기를 써야 한다
    HangulLayout *hl = (HangulLayout *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(HangulLayout));
    if (!hl) return false;
    GetStr(r, hl->name, 64);
    hl->moachigi = GetI32(r);
    hl->composition = GetI32(r);
    for (int i = 0; i < 128; i++) {
        unsigned ty = Get16(r);
        int ix = GetI16(r);
        if (!JamoRefOk((int)ty, ix)) r->bad = true;
        hl->keymap[i].type = (JamoType)(ty <= JAMO_JONG ? ty : JAMO_NONE);
        hl->keymap[i].index = (ty == JAMO_NONE || r->bad) ? 0 : ix;
    }
    int n = GetI32(r);
    if (r->bad || n < 0 || n > HL_MAX_COMBINE) { HeapFree(GetProcessHeap(), 0, hl); return false; }
    if (hl->moachigi < 0 || hl->moachigi > 1) { HeapFree(GetProcessHeap(), 0, hl); return false; }
    hl->combineCount = n;
    for (int i = 0; i < n; i++) {
        unsigned ty = Get16(r);
        int a = GetI16(r), b = GetI16(r), res = GetI16(r);
        if (ty < JAMO_CHO || ty > JAMO_JONG || !JamoRefOk((int)ty, a) || !JamoRefOk((int)ty, b) || !JamoRefOk((int)ty, res)) r->bad = true;
        hl->combines[i].type = (JamoType)(r->bad ? JAMO_NONE : ty);
        hl->combines[i].a = r->bad ? 0 : a; hl->combines[i].b = r->bad ? 0 : b; hl->combines[i].result = r->bad ? 0 : res;
    }
    if (r->bad) { HeapFree(GetProcessHeap(), 0, hl); return false; }
    int gn = GetI32(r);
    if (r->bad || gn < 0 || gn > HL_MAX_GUARDED) { HeapFree(GetProcessHeap(), 0, hl); return false; }
    hl->guardedCount = gn;
    for (int i = 0; i < gn; i++) {
        HangulGuarded *g = &hl->guarded[i];
        unsigned gk = Get16(r), gt = Get16(r);
        int gi = GetI16(r);
        if (gk >= 128 || !JamoRefOk((int)gt, gi)) r->bad = true;
        g->key = (unsigned char)(gk & 0x7F);
        g->r.type = (JamoType)(gt <= JAMO_JONG ? gt : JAMO_NONE);
        g->r.index = (gt == JAMO_NONE || r->bad) ? 0 : gi;
        unsigned len = Get16(r);
        g->len = (unsigned char)(len <= HL_GUARD_CODE ? len : 0);
        for (int k = 0; k < HL_GUARD_CODE; k++) {
            unsigned v = Get16(r);
            g->code[k] = (unsigned char)v;
        }
        if (len > HL_GUARD_CODE || r->bad) { HeapFree(GetProcessHeap(), 0, hl); return false; }
    }
    unsigned hasChord = Get32(r);                      // 앞단 조합 (6판, RFC-0007)
    if (r->bad) { HeapFree(GetProcessHeap(), 0, hl); return false; }
    if (hasChord) {
        hl->chord = ReadChordTable(r);
        if (!hl->chord) { HeapFree(GetProcessHeap(), 0, hl); return false; }
    }
    out->pHangulLayout = hl;
    out->kbdVariant = KBD_SEBEOL;
    return true;
}

static bool ReadChord(Rd *r, LayoutConfig *out) {
    ChordLayout *cl = ReadChordTable(r);
    if (!cl) return false;
    out->pChordLayout = cl;
    return true;
}

static ChordLayout *ReadChordTable(Rd *r) {
    // 해제는 ChordLayout_Free(HeapFree) 가 한다. 조합 앞의 부분(약 10KB)만 먼저 잡고, 조합 수를 읽은 뒤 그만큼 늘린다
    //   (RFC-0020 F2) — chords[2048] 전체는 2MB 가 넘어, 예전에는 그만큼을 잡았다가 줄였다(앱마다 순간 2MB).
    const size_t head = offsetof(ChordLayout, chords);
    ChordLayout *cl = (ChordLayout *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, head);
    if (!cl) return NULL;
    GetStr(r, cl->name, 64);
    cl->v3 = GetI32(r);
    cl->comboTermMs = GetI32(r);
    cl->holdTermMs = GetI32(r);
    cl->holdPolicy = GetI32(r);
    if (cl->comboTermMs < 0 || cl->comboTermMs > 5000 || cl->holdTermMs < 0 || cl->holdTermMs > 5000 ||
        (cl->holdPolicy != CHORD_HOLD_INTERRUPT && cl->holdPolicy != CHORD_HOLD_TIMEOUT)) r->bad = true;
    for (int i = 0; i < 128; i++) {
        cl->keyBit[i] = GetI16(r);
        if (cl->keyBit[i] < -1 || cl->keyBit[i] > 31) r->bad = true;   // 1u << 비트 — 31 을 넘으면 정의되지 않은 동작
    }
    if (r->bad) { HeapFree(GetProcessHeap(), 0, cl); return NULL; }
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
        unsigned kind = Get8(r);
        s->kind = (unsigned char)kind;
        s->vk = GetI32(r); s->mod = GetI32(r);
        s->p1 = GetI32(r); s->p2 = GetI32(r); s->prof = GetI32(r);
        if (kind > MS_ENDWITH || s->mod < 0 || s->mod > 0xFF || s->prof < -1 || s->prof > CPROF_SCROLL) r->bad = true;
        if (kind == MS_KEY && (s->vk < 0 || s->vk > 255)) r->bad = true;
        if (kind == MS_PTR && (s->vk < CA_PTR_MOVE || s->vk > CA_PTR_WHEEL)) r->bad = true;
        if (kind == MS_WAIT && (s->p1 < 0 || s->p1 > CL_MACRO_MAX_MS)) r->bad = true;   // 기다림은 음수일 수 없고 한 번 실행의 상한 안이다
        s->textOff = (unsigned short)Get16(r);
        s->textLen = (unsigned short)Get16(r);
    }
    int textLen = GetI32(r);
    if (r->bad || textLen < 0 || textLen > CL_MACRO_TEXT) { HeapFree(GetProcessHeap(), 0, cl); return NULL; }
    cl->macroTextLen = textLen;
    for (int i = 0; i < textLen; i++) cl->macroText[i] = (wchar_t)Get16(r);
    int chords = GetI32(r);
    if (r->bad || chords < 0 || chords > CL_MAX_CHORDS) { HeapFree(GetProcessHeap(), 0, cl); return NULL; }
    {
        ChordLayout *grown = (ChordLayout *)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cl, head + (size_t)chords * sizeof(ChordEntry));
        if (!grown) { HeapFree(GetProcessHeap(), 0, cl); return NULL; }
        cl = grown;
    }
    cl->chordCount = chords;
    if (r->bad) { HeapFree(GetProcessHeap(), 0, cl); return NULL; }
    for (int i = 0; i < chords; i++) {
        ChordEntry *e = &cl->chords[i];
        e->mask = Get32(r);
        e->layer = GetI32(r);
        e->isHold = GetI32(r);
        unsigned act = Get32(r);
        if (act > CA_SYMBOL) r->bad = true;
        e->act = (ChordActionType)(act > CA_SYMBOL ? CA_TEXT : act);
        GetStr(r, e->text, sizeof e->text / sizeof e->text[0]);
        e->vk = GetI32(r); e->keyExt = GetI32(r); e->mod = GetI32(r);
        e->targetLayer = GetI32(r);
        e->p1 = GetI32(r); e->p2 = GetI32(r); e->prof = GetI32(r);
        e->holdOneshot = GetI32(r);   // 판 4 (Hold 의 원샷) — 판이 다르면 위에서 이미 거절된다
        if (e->isHold < 0 || e->isHold > 1 || e->holdOneshot < 0 || e->holdOneshot > 1 ||
            e->mod < 0 || e->mod > 0xFF || e->prof < -1 || e->prof > CPROF_SCROLL ||
            (e->act == CA_KEY && (e->vk < 0 || e->vk > 255))) r->bad = true;
    }
    // 참조 무결성: 매크로 단계·레이어 번호가 실제로 있는 것을 가리켜야 한다
    for (int i = 0; i < macros && !r->bad; i++)
        if (cl->macros[i].first < 0 || cl->macros[i].count < 0 || cl->macros[i].count > steps ||
            cl->macros[i].first > steps - cl->macros[i].count) r->bad = true;   // 더하지 않고 뺀다 — int 가 한 바퀴 돌지 않게
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
    return cl;   // 처음부터 조합 수만큼이다 — 줄일 것이 없다
}
static bool ReadSeq(Rd *r, const wchar_t *jmbPath, LayoutConfig *out, JLayError *err) {
    SeqLayout *sl = (SeqLayout *)calloc(1, sizeof(SeqLayout));
    if (!sl) return false;
    GetStr(r, sl->name, 64);
    GetStr(r, sl->dictFile, 64);
    sl->onUnmatched = GetI32(r);
    if (sl->onUnmatched != SEQ_UNMATCHED_FLUSH && sl->onUnmatched != SEQ_UNMATCHED_CANCEL) r->bad = true;
    GetStr(r, sl->candFile, 64);
    sl->convertVk = GetI32(r);
    if (sl->convertVk < 0 || sl->convertVk > 255) r->bad = true;
    sl->zh = GetI32(r);                   // 판 7
    if (sl->zh < SEQ_ZH_NONE || sl->zh > SEQ_ZH_TRADITIONAL) r->bad = true;
    sl->nScheme = GetI32(r);              // 판 8
    if (sl->nScheme < 0 || sl->nScheme > SEQ_MAX_SCHEMES) { r->bad = true; sl->nScheme = 0; }
    for (int i = 0; i < sl->nScheme && !r->bad; i++) { GetStr(r, sl->schemeName[i], 16); GetStr(r, sl->schemeFile[i], 64); }
    GetStr(r, sl->toneFile, 64);          // 판 9
    sl->ja = GetI32(r);                   // 판 10
    if (sl->ja < 0 || sl->ja > 1 || (sl->ja && sl->zh)) r->bad = true;
    GetStr(r, sl->connFile, 64);          // 판 11
    unsigned hasChord = Get32(r);
    if (r->bad || !sl->dictFile[0]) { SeqLayout_Free(sl); return false; }
    if (hasChord) {                       // 앞단 조합 인식기 (§6.3)
        sl->chord = ReadChordTable(r);
        if (!sl->chord) { SeqLayout_Free(sl); return false; }
    }
    if (!SeqLayout_OpenDict(sl, jmbPath, NULL)) {   // 사전은 자판 밖에 있다 — 열고 전수 점검한다
        SeqLayout_Free(sl);               // 이미 연 사전·조합표도 함께 닫는다 (예전에는 샜고 .jdb 가 프로세스 끝까지 잠겼다)
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

bool JLay_LoadHeader(const wchar_t *path, LayoutConfig *out, JLayError *err) {
    JLayError dummy;
    if (!err) err = &dummy;
    *err = JLAY_OK;
    memset(out, 0, sizeof *out);
    size_t len = 0;
    unsigned char *file = ReadWhole(path, &len);   // 구운 자판은 작다 (가장 큰 것이 수백 KB) — 검사합은 그대로 본다
    if (!file) { *err = JLAY_E_OPEN; return false; }
    if (len < JLAY_HEADER || memcmp(file, JLAY_MAGIC, 8) != 0) { free(file); *err = JLAY_E_MAGIC; return false; }
    if (JLayRd32(file + JLAY_OFF_VERSION) != JLAY_FORMAT_VERSION) { free(file); *err = JLAY_E_VERSION; return false; }
    unsigned body = JLayRd32(file + JLAY_OFF_BODY), total = JLayRd32(file + JLAY_OFF_SIZE);
    if (total != (unsigned)len || (size_t)body + JLAY_HEADER != len) { free(file); *err = JLAY_E_LAYOUT; return false; }
    if (JDict_Crc32(file + JLAY_HEADER, body) != JLayRd32(file + JLAY_OFF_CRC)) { free(file); *err = JLAY_E_CRC; return false; }
    unsigned kind = JLayRd32(file + JLAY_OFF_KIND);
    if (kind != LAYOUT_TYPE_PASSTHROUGH && kind != LAYOUT_TYPE_STATIC_MAP && kind != LAYOUT_TYPE_HANGUL_CUSTOM &&
        kind != LAYOUT_TYPE_CHORD && kind != LAYOUT_TYPE_SEQUENCE) { free(file); *err = JLAY_E_KIND; return false; }
    Rd r = { file + JLAY_HEADER, body, 0, false };
    wchar_t name[64] = L"", abbrev[8] = L"";
    GetStr(&r, name, 64);
    GetStr(&r, abbrev, 8);
    out->kbdVariant = GetI32(&r);
    free(file);
    if (r.bad) { *err = JLAY_E_LAYOUT; return false; }
    out->type = (LayoutType)kind;
    out->name = _wcsdup(name[0] ? name : L"layout");
    if (!out->name) { *err = JLAY_E_MEMORY; return false; }
    lstrcpynW(out->abbrev, abbrev, 8);
    return true;
}

bool JLay_LoadDeferredBody(LayoutConfig *L) {
    if (!L || !L->deferredPath) return false;
    LayoutConfig t; JLayError e = JLAY_OK;
    if (!JLay_Load(L->deferredPath, &t, &e)) return false;
    if (t.type != L->type) { Config_FreeLayoutResources(&t); return false; }   // 그새 파일이 바뀌었다
    L->kbdVariant = t.kbdVariant;
    memcpy(L->charMap, t.charMap, sizeof L->charMap);
    L->pHangulLayout = t.pHangulLayout; L->pChordLayout = t.pChordLayout; L->pSeqLayout = t.pSeqLayout;
    free((void *)t.name);   // 이름은 목록의 것을 쓴다
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

bool JLay_BuiltPath(const wchar_t *jmtPath, wchar_t *out, size_t cch) {
    if (!jmtPath || !out || cch == 0) return false;
    size_t n = wcslen(jmtPath);
    if (n < 5 || _wcsicmp(jmtPath + n - 4, L".jmt") != 0) return false;
    int w = _snwprintf(out, cch, L"%.*ls.v%u.jmb", (int)(n - 4), jmtPath, (unsigned)JLAY_FORMAT_VERSION);
    if (w < 0 || (size_t)w >= cch) { out[0] = L'\0'; return false; }
    return true;
}

unsigned JLay_FileFormat(const wchar_t *jmbPath) {
    FILE *f = _wfopen(jmbPath, L"rb");
    if (!f) return 0;
    unsigned char h[JLAY_OFF_VERSION + 4];
    size_t got = fread(h, 1, sizeof h, f);
    fclose(f);
    if (got != sizeof h || memcmp(h, JLAY_MAGIC, 8) != 0) return 0;
    return JLayRd32(h + JLAY_OFF_VERSION);
}

