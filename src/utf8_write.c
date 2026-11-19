// utf8_write.c — 와이드 → UTF-8 (utf8_write.h).
#include "utf8_write.h"
#include <stdarg.h>
#include <stdlib.h>

void Utf8_FromWide(const wchar_t *w, char *out, size_t cap) {
    size_t o = 0;
    if (!cap) return;
    for (size_t i = 0; w[i]; i++) {
        unsigned long c = (unsigned long)w[i];
        if (sizeof(wchar_t) == 2 && c >= 0xD800 && c < 0xDC00 && w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + ((unsigned long)w[i + 1] - 0xDC00); i++;
        }
        if (c >= 0xD800 && c < 0xE000) c = 0xFFFD;
        char b[4]; size_t n;
        if (c < 0x80) { b[0] = (char)c; n = 1; }
        else if (c < 0x800) { b[0] = (char)(0xC0 | (c >> 6)); b[1] = (char)(0x80 | (c & 0x3F)); n = 2; }
        else if (c < 0x10000) { b[0] = (char)(0xE0 | (c >> 12)); b[1] = (char)(0x80 | ((c >> 6) & 0x3F)); b[2] = (char)(0x80 | (c & 0x3F)); n = 3; }
        else { b[0] = (char)(0xF0 | (c >> 18)); b[1] = (char)(0x80 | ((c >> 12) & 0x3F)); b[2] = (char)(0x80 | ((c >> 6) & 0x3F)); b[3] = (char)(0x80 | (c & 0x3F)); n = 4; }
        if (o + n + 1 > cap) break;
        for (size_t k = 0; k < n; k++) out[o + k] = b[k];
        o += n;
    }
    out[o] = 0;
}

bool Utf8_Fprintf(FILE *f, const wchar_t *fmt, ...) {
    // 줄 길이에 상한을 두지 않는다 — 긴 경로·이름에서 헤더 줄이 조용히 빠지지 않게 버퍼를 키워 가며 만든다 (리뷰 2026-11-19 #5).
    size_t cap = 512;
    wchar_t *w = NULL;
    int n = -1;
    for (;;) {
        wchar_t *nw = (wchar_t*)realloc(w, cap * sizeof(wchar_t));
        if (!nw) { free(w); return false; }
        w = nw;
        va_list ap;
        va_start(ap, fmt);
        n = vswprintf(w, cap, fmt, ap);
        va_end(ap);
        if (n >= 0 && (size_t)n < cap) break;     // 들어갔다
        if (cap >= (1u << 20)) { free(w); return false; }   // 백만 글자를 넘는 한 줄은 이 도구의 쓰임이 아니다
        cap *= 2;
    }
    size_t u8cap = (size_t)n * 4 + 1;               // 한 글자는 UTF-8 로 최대 4바이트
    char *u8 = (char*)malloc(u8cap);
    if (!u8) { free(w); return false; }
    Utf8_FromWide(w, u8, u8cap);
    size_t len = 0;
    while (u8[len]) len++;
    bool ok = fwrite(u8, 1, len, f) == len;
    free(u8); free(w);
    return ok;
}
