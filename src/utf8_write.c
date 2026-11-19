// utf8_write.c — 와이드 → UTF-8 (utf8_write.h).
#include "utf8_write.h"
#include <stdarg.h>

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
    wchar_t w[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vswprintf(w, 512, fmt, ap);
    va_end(ap);
    if (n < 0) return false;
    char u8[2048];
    Utf8_FromWide(w, u8, sizeof u8);
    size_t len = 0;
    while (u8[len]) len++;
    return fwrite(u8, 1, len, f) == len;
}
