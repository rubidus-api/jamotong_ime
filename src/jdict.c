// jdict.c — 읽기 전용 이진 사전: 매핑해서 그대로 읽고, 정렬된 키를 이진 탐색한다 (RFC-0016 P5).
//   파일 구조는 jdict.h 에 적혀 있다. 여기서는 (1) 열면서 범위를 검사하고, (2) 고를 때 전수 점검하고,
//   (3) 엔진이 묻는 세 가지(정확 일치·더 긴 후보·앞쪽 최장 일치)에 답한다.
#include "jdict.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#define JD_HEADER_BYTES 64
#define JD_REC_BYTES    12
#define JD_MAX_COUNT    JDICT_MAX_ENTRIES

typedef struct JDict {
    HANDLE   file, mapping;
    const unsigned char *base;
    size_t   size;
    unsigned count, kind, maxKeyLen, maxValLen, flags, crc;
    unsigned offIndex, offKeys, offVals, keyBytes, valBytes;
    wchar_t  name[64], license[64], version[32];
} JDict;

static unsigned Rd32(const unsigned char *p) {   // 리틀엔디안 — 파일이 어느 기계에서 구워졌든 같게 읽는다
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static unsigned Rd16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }

static const unsigned char *RecAt(const JDict *d, int i) { return d->base + d->offIndex + (size_t)i * JD_REC_BYTES; }
static const char *KeyAt(const JDict *d, int i, int *len) {
    const unsigned char *r = RecAt(d, i);
    *len = (int)Rd16(r + 8);
    return (const char *)(d->base + d->offKeys + Rd32(r));
}
static const jdchar *ValAt(const JDict *d, int i, int *len) {
    const unsigned char *r = RecAt(d, i);
    *len = (int)Rd16(r + 10);
    return (const jdchar *)(const void *)(d->base + d->offVals + Rd32(r + 4));
}

// 찾는 키를 UTF-8 로 (순차 사전의 키는 ASCII 이지만, 다른 종류를 대비해 한 곳에서 변환한다)
static int KeyToUtf8(const wchar_t *key, char *out, int cap) {
    int n = 0;
    for (const wchar_t *p = key; *p; p++) {
        unsigned cp = (unsigned)*p;
        if (cp >= 0xD800 && cp <= 0xDBFF && p[1] >= 0xDC00 && p[1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + ((unsigned)p[1] - 0xDC00); p++;
        }
        if (cp < 0x80) { if (n + 1 > cap) return -1; out[n++] = (char)cp; }
        else if (cp < 0x800) { if (n + 2 > cap) return -1; out[n++] = (char)(0xC0 | (cp >> 6)); out[n++] = (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { if (n + 3 > cap) return -1; out[n++] = (char)(0xE0 | (cp >> 12)); out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[n++] = (char)(0x80 | (cp & 0x3F)); }
        else { if (n + 4 > cap) return -1; out[n++] = (char)(0xF0 | (cp >> 18)); out[n++] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[n++] = (char)(0x80 | (cp & 0x3F)); }
    }
    return n;
}
// 바이트 사전식 비교 (짧은 쪽이 앞) — 구울 때와 찾을 때가 같은 규칙을 써야 한다
static int CmpBytes(const char *a, int an, const char *b, int bn) {
    int n = an < bn ? an : bn;
    int c = n ? memcmp(a, b, (size_t)n) : 0;
    if (c) return c;
    return an == bn ? 0 : (an < bn ? -1 : 1);
}
// key 이상인 첫 항목의 자리 (없으면 count)
static int LowerBound(const JDict *d, const char *key, int klen) {
    int lo = 0, hi = (int)d->count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2, mlen = 0;
        const char *mk = KeyAt(d, mid, &mlen);
        if (CmpBytes(mk, mlen, key, klen) < 0) lo = mid + 1; else hi = mid;
    }
    return lo;
}

// ── 열기 ───────────────────────────────────────────────────────────────────────────
static bool CheckLayout(JDict *d) {
    const unsigned char *h = d->base;
    if (d->size < JD_HEADER_BYTES) return false;
    if (Rd32(h + 56) != (unsigned)d->size) return false;          // 잘렸거나 덧붙었다
    d->count     = Rd32(h + 16);
    d->offIndex  = Rd32(h + 20);
    d->offKeys   = Rd32(h + 24);
    d->offVals   = Rd32(h + 28);
    d->keyBytes  = Rd32(h + 32);
    d->valBytes  = Rd32(h + 36);
    d->maxKeyLen = Rd32(h + 40);
    d->maxValLen = Rd32(h + 44);
    d->flags     = Rd32(h + 48);
    d->crc       = Rd32(h + 52);
    if (d->count == 0 || d->count > JD_MAX_COUNT) return false;
    if (d->maxKeyLen == 0 || d->maxKeyLen > JDICT_MAX_KEY) return false;
    if (d->maxValLen == 0 || d->maxValLen > JDICT_MAX_VALUE) return false;
    // 판 1 의 자리는 고정이다 — 이 배치가 아니면 우리가 구운 파일이 아니다
    if (d->offIndex != JD_HEADER_BYTES) return false;
    if (d->offKeys != d->offIndex + d->count * JD_REC_BYTES) return false;
    if (d->offVals != ((d->offKeys + d->keyBytes + 1u) & ~1u)) return false;
    if ((d->valBytes & 1u) != 0) return false;
    unsigned offMeta = d->offVals + d->valBytes;
    if (offMeta > d->size) return false;
    // 색인 전수 범위 검사 (작다: 10만 항목 = 1.2MB, 어차피 탐색에서 읽는다)
    for (unsigned i = 0; i < d->count; i++) {
        const unsigned char *r = d->base + d->offIndex + (size_t)i * JD_REC_BYTES;
        unsigned ko = Rd32(r), vo = Rd32(r + 4), kl = Rd16(r + 8), vl = Rd16(r + 10);
        if (kl == 0 || kl > JDICT_MAX_KEY || vl == 0 || vl > JDICT_MAX_VALUE) return false;
        if (ko > d->keyBytes || ko + kl > d->keyBytes) return false;
        if (vo > d->valBytes || vo + vl * 2u > d->valBytes || (vo & 1u) != 0) return false;
    }
    // 꼬리의 이름/라이선스/판 (각각 u16 길이 + UTF-16LE)
    unsigned cap[3] = { 64, 64, 32 };
    wchar_t *out[3] = { d->name, d->license, d->version };
    unsigned at = offMeta;
    for (int f = 0; f < 3; f++) {
        out[f][0] = L'\0';
        if (at + 2 > d->size) return false;
        unsigned len = Rd16(d->base + at);
        at += 2;
        if (at + len * 2u > d->size) return false;
        unsigned keep = len < cap[f] - 1 ? len : cap[f] - 1;
        for (unsigned i = 0; i < keep; i++) out[f][i] = (wchar_t)Rd16(d->base + at + i * 2u);
        out[f][keep] = L'\0';
        at += len * 2u;
    }
    return at == d->size;
}

JDict *JDict_Open(const wchar_t *path, JDictError *err) {
    JDictError dummy;
    if (!err) err = &dummy;
    *err = JDICT_OK;
    JDict *d = (JDict *)calloc(1, sizeof(JDict));
    if (!d) { *err = JDICT_E_MEMORY; return NULL; }
    d->file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (d->file == INVALID_HANDLE_VALUE) { free(d); *err = JDICT_E_OPEN; return NULL; }
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(d->file, &sz) || sz.QuadPart < JD_HEADER_BYTES || sz.QuadPart > 0x7FFFFFFF) {
        CloseHandle(d->file); free(d); *err = JDICT_E_LAYOUT; return NULL;
    }
    d->size = (size_t)sz.QuadPart;
    d->mapping = CreateFileMappingW(d->file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!d->mapping) { CloseHandle(d->file); free(d); *err = JDICT_E_OPEN; return NULL; }
    d->base = (const unsigned char *)MapViewOfFile(d->mapping, FILE_MAP_READ, 0, 0, 0);
    if (!d->base) { CloseHandle(d->mapping); CloseHandle(d->file); free(d); *err = JDICT_E_OPEN; return NULL; }
    if (memcmp(d->base, "JMTDICT\0", 8) != 0) { JDict_Close(d); *err = JDICT_E_MAGIC; return NULL; }
    if (Rd32(d->base + 8) != JDICT_FORMAT_VERSION) { JDict_Close(d); *err = JDICT_E_VERSION; return NULL; }
    d->kind = Rd32(d->base + 12);
    if (!CheckLayout(d)) { JDict_Close(d); *err = JDICT_E_LAYOUT; return NULL; }
    return d;
}

void JDict_Close(JDict *d) {
    if (!d) return;
    if (d->base) UnmapViewOfFile(d->base);
    if (d->mapping) CloseHandle(d->mapping);
    if (d->file && d->file != INVALID_HANDLE_VALUE) CloseHandle(d->file);
    free(d);
}

int            JDict_Count(const JDict *d)     { return d ? (int)d->count : 0; }
int            JDict_Kind(const JDict *d)      { return d ? (int)d->kind : 0; }
int            JDict_MaxKeyLen(const JDict *d) { return d ? (int)d->maxKeyLen : 0; }
const wchar_t *JDict_Name(const JDict *d)      { return d ? d->name : L""; }
const wchar_t *JDict_License(const JDict *d)   { return d ? d->license : L""; }

const wchar_t *JDict_ErrorText(JDictError e) {
    switch (e) {
        case JDICT_OK:         return L"ok";
        case JDICT_E_OPEN:     return L"the dictionary file cannot be opened";
        case JDICT_E_MAGIC:    return L"this is not a Jamotong dictionary file";
        case JDICT_E_VERSION:  return L"this dictionary was built for a newer Jamotong";
        case JDICT_E_LAYOUT:   return L"the dictionary file is damaged (its parts do not fit the file)";
        case JDICT_E_CRC:      return L"the dictionary content does not match its checksum";
        case JDICT_E_ORDER:    return L"the dictionary entries are not in order";
        case JDICT_E_KEY:      return L"the dictionary has a key this engine cannot type";
        case JDICT_E_MEMORY:   return L"out of memory";
    }
    return L"unknown error";
}

// ── 전수 점검 (자판을 고를 때 한 번) ───────────────────────────────────────────────
// 표 방식 CRC-32/IEEE. 비트마다 도는 판은 100MB 당 1초라 큰 사전에서 고르는 순간이 멈춘다.
unsigned JDict_Crc32(const void *data, unsigned long len) {
    static unsigned tbl[256];
    static int ready = 0;
    if (!ready) {   // 표는 값이 정해져 있어 여러 스레드가 같이 만들어도 결과가 같다
        for (unsigned i = 0; i < 256; i++) {
            unsigned c = i;
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (unsigned)(-(int)(c & 1)));
            tbl[i] = c;
        }
        ready = 1;
    }
    const unsigned char *p = (const unsigned char *)data;
    unsigned c = 0xFFFFFFFFu;
    for (unsigned long i = 0; i < len; i++) c = tbl[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool JDict_Verify(const JDict *d, JDictError *err) {
    JDictError dummy;
    if (!err) err = &dummy;
    *err = JDICT_OK;
    if (!d) { *err = JDICT_E_LAYOUT; return false; }
    if (JDict_Crc32(d->base + JD_HEADER_BYTES, (unsigned long)(d->size - JD_HEADER_BYTES)) != d->crc) { *err = JDICT_E_CRC; return false; }
    int prevLen = 0;
    const char *prev = NULL;
    for (unsigned i = 0; i < d->count; i++) {
        int klen = 0;
        const char *k = KeyAt(d, (int)i, &klen);
        int cmp = prev ? CmpBytes(prev, prevLen, k, klen) : -1;
        // 순차 사전은 키가 꼭 커져야 하고, 후보 사전은 같은 키가 이어질 수 있다 (§6.4)
        if (cmp > 0 || (cmp == 0 && d->kind != JDICT_KIND_CANDIDATES)) { *err = JDICT_E_ORDER; return false; }
        if (d->kind == JDICT_KIND_SEQUENCE)
            for (int j = 0; j < klen; j++)
                if ((unsigned char)k[j] < 0x21 || (unsigned char)k[j] > 0x7E) { *err = JDICT_E_KEY; return false; }
        prev = k; prevLen = klen;
    }
    return true;
}

// ── 찾기 ───────────────────────────────────────────────────────────────────────────
int JDict_CopyValue(const jdchar *val, int valLen, wchar_t *out, int cap) {
    if (!val || !out || valLen < 0 || valLen + 1 > cap) return -1;
    for (int i = 0; i < valLen; i++) out[i] = (wchar_t)val[i];
    out[valLen] = L'\0';
    return valLen;
}

bool JDict_Exact(const JDict *d, const wchar_t *key, const jdchar **val, int *valLen) {
    if (!d || !key || !*key) return false;
    char kb[JDICT_MAX_KEY * 4];
    int kn = KeyToUtf8(key, kb, (int)sizeof(kb));
    if (kn <= 0) return false;
    int at = LowerBound(d, kb, kn);
    if (at >= (int)d->count) return false;
    int klen = 0;
    const char *k = KeyAt(d, at, &klen);
    if (klen != kn || memcmp(k, kb, (size_t)kn) != 0) return false;
    if (val || valLen) {
        int vlen = 0;
        const jdchar *v = ValAt(d, at, &vlen);
        if (val) *val = v;
        if (valLen) *valLen = vlen;
    }
    return true;
}

bool JDict_Candidates(const JDict *d, const wchar_t *key, int *first, int *count) {
    if (!d || !key || !*key) return false;
    char kb[JDICT_MAX_KEY * 4];
    int kn = KeyToUtf8(key, kb, (int)sizeof(kb));
    if (kn <= 0) return false;
    int at = LowerBound(d, kb, kn);
    int n = 0;
    while (at + n < (int)d->count) {   // 같은 키가 이어지는 만큼이 후보다 (구울 때 차례를 지킨다)
        int klen = 0;
        const char *k = KeyAt(d, at + n, &klen);
        if (klen != kn || memcmp(k, kb, (size_t)kn) != 0) break;
        n++;
    }
    if (n == 0) return false;
    if (first) *first = at;
    if (count) *count = n;
    return true;
}

bool JDict_CandidateAt(const JDict *d, int index, const jdchar **val, int *valLen) {
    if (!d || index < 0 || index >= (int)d->count) return false;
    int vlen = 0;
    const jdchar *v = ValAt(d, index, &vlen);
    if (val) *val = v;
    if (valLen) *valLen = vlen;
    return true;
}

bool JDict_HasLonger(const JDict *d, const wchar_t *key) {
    if (!d || !key || !*key) return false;
    char kb[JDICT_MAX_KEY * 4];
    int kn = KeyToUtf8(key, kb, (int)sizeof(kb));
    if (kn <= 0) return false;
    int at = LowerBound(d, kb, kn);
    // 같은 키가 있으면 그 다음부터 — 접두가 같고 더 긴 것은 바로 뒤에 온다 (바이트 정렬)
    if (at < (int)d->count) {
        int klen = 0;
        const char *k = KeyAt(d, at, &klen);
        if (klen == kn && memcmp(k, kb, (size_t)kn) == 0) at++;
    }
    if (at >= (int)d->count) return false;
    int klen = 0;
    const char *k = KeyAt(d, at, &klen);
    return klen > kn && memcmp(k, kb, (size_t)kn) == 0;
}

bool JDict_LongestPrefix(const JDict *d, const wchar_t *buf, int *keyLen,
                         const jdchar **val, int *valLen) {
    if (!d || !buf || !*buf) return false;
    int n = (int)wcslen(buf);
    if (n > (int)d->maxKeyLen) n = (int)d->maxKeyLen;
    // 긴 쪽부터 정확 일치로 물어본다 — 키 길이 한도가 32 라 최악도 32번의 이진 탐색이다.
    wchar_t probe[JDICT_MAX_KEY + 1];
    for (int len = n; len >= 1; len--) {
        if (len > JDICT_MAX_KEY) continue;
        wmemcpy(probe, buf, (size_t)len);
        probe[len] = L'\0';
        if (JDict_Exact(d, probe, val, valLen)) {
            if (keyLen) *keyLen = len;
            return true;
        }
    }
    return false;
}
