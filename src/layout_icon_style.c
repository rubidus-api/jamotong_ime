// layout_icon_style.c — 자판마다 다른 아이콘 글자·색 (layout_icon_style.h).
#include "layout_icon_style.h"
#include <string.h>

// 실리는 자판: 아래 줄 글자색(밝은 바탕에서 읽히도록 Darken 이 어둡게 맞춘다). 같은 자판의 내장판과 파일판은 색을 나눈다.
static const struct { const wchar_t *abbrev; unsigned rgb; } kKnown[] = {
    { L"KO2S", 0x1F6FD1u }, { L"KO2B", 0x1F6FD1u },   // 두벌식 — 파랑
    { L"KO3F", 0x2E9E4Fu }, { L"KO3B", 0x2E9E4Fu },   // 세벌식 최종 — 초록
    { L"KO39", 0x0E8A8Au },                           // 세벌식 390 — 청록
    { L"KO11", 0x7A8C1Eu },                           // 세벌식 3-2011 — 올리브
    { L"KO12", 0x5B5FC7u },                           // 세벌식 3-2012 — 남색
    { L"KOSA", 0xE07B00u },                           // 순아래 — 주황
    { L"KO1H", 0x8E3FB0u },                           // 한 손 — 보라
    { L"KOEX", 0x8B5A2Bu },                           // 예제 한글 — 갈색
    { L"ENQW", 0x4A4A4Au },                           // QWERTY — 짙은 회색
    { L"ENDV", 0xC0392Bu }, { L"ENDX", 0xC0392Bu },   // Dvorak — 빨강
    { L"ENAR", 0xC9970Cu },                           // ARTSEY — 금색
    { L"ZHSP", 0xD32F2Fu },                           // 중국어 간체 — 빨강
    { L"ZHTP", 0x7B1E3Au },                           // 중국어 번체 — 자주
    { L"JARO", 0xD8457Fu },                           // 일본어 — 분홍
};
// 모르는 자판(사용자 자판)에 쓸 색
static const unsigned kSpare[] = { 0x2C7BB6u, 0x1A9850u, 0xD95F02u, 0x7570B3u, 0xE7298Au, 0x66A61Eu, 0xA6761Du, 0x1B9E77u };

int LayoutIcon_KnownCount(void) { return (int)(sizeof kKnown / sizeof kKnown[0]); }
const wchar_t *LayoutIcon_KnownAbbrev(int i) { return (i >= 0 && i < LayoutIcon_KnownCount()) ? kKnown[i].abbrev : L""; }

// 점 글꼴에 있는 글자(A-Z 0-9 -)만 쓴다: 소문자는 대문자로, 나머지는 '?'.
static wchar_t Glyph(wchar_t c) {
    if (c >= L'a' && c <= L'z') return (wchar_t)(c - 32);
    return ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-') ? c : L'?';
}

// 언어마다 글자색 (밝은 바탕 위; Darken 이 대비를 맞춘다). 모르는 언어는 abbrev 앞 두 글자로 고른다.
static const struct { const wchar_t *lang; unsigned rgb; } kLang[] = {
    { L"KO", 0x1F5FBFu },   // 한국어 — 파랑
    { L"EN", 0x3A3A3Au },   // 영어 — 짙은 회색
    { L"CN", 0xC62828u },   // 중국어 — 빨강
    { L"JP", 0xB0306Au },   // 일본어 — 자주
};

// 바탕(LI_BACKGROUND_RGB)과의 대비가 충분하도록 색을 어둡게 한다 (감마 2 근사 휘도: 대비 = (Lbg + 0.05) / (L + 0.05) ≥ 4.5 ⇔ L ≤ LI_MIN_CONTRAST_LUM).
static unsigned Darken(unsigned rgb) {
    unsigned c[3] = { (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF };
    for (int tries = 0; tries < 40; tries++) {
        double r = c[0] / 255.0, g = c[1] / 255.0, b = c[2] / 255.0;
        if (0.2126 * r * r + 0.7152 * g * g + 0.0722 * b * b <= LI_MIN_CONTRAST_LUM) break;
        for (int k = 0; k < 3; k++) c[k] = c[k] * 92 / 100;
    }
    return (c[0] << 16) | (c[1] << 8) | c[2];
}

bool LayoutIcon_Style(const wchar_t *abbrev, LayoutIconStyle *out) {
    memset(out, 0, sizeof *out);
    const wchar_t *a = abbrev && abbrev[0] ? abbrev : L"?";
    if (!wcscmp(a, L"--")) {   // 무간섭(직접 입력) 모드
        wcscpy(out->top, L"--"); wcscpy(out->bottom, L"--"); out->rgb = out->langRgb = Darken(0x777777u);
        return true;
    }
    size_t n = wcslen(a);
    // 위 줄: 언어 — 앞 두 글자 (ZH → CN, JA → JP). 한 글자뿐이면 그 글자 하나.
    if (n >= 2 && !wcsncmp(a, L"ZH", 2)) wcscpy(out->top, L"CN");
    else if (n >= 2 && !wcsncmp(a, L"JA", 2)) wcscpy(out->top, L"JP");
    else { out->top[0] = Glyph(a[0]); if (n >= 2) out->top[1] = Glyph(a[1]); }
    // 아래 줄: 자판 — 뒤 두 글자. 네 글자 미만이면 있는 만큼(두 글자 이하는 위와 같은 글자를 쓰지 않도록 뒤쪽).
    if (n >= 4) { out->bottom[0] = Glyph(a[2]); out->bottom[1] = Glyph(a[3]); }
    else if (n == 3) { out->bottom[0] = Glyph(a[1]); out->bottom[1] = Glyph(a[2]); }
    else if (n == 2) { out->bottom[0] = Glyph(a[0]); out->bottom[1] = Glyph(a[1]); }
    else { out->bottom[0] = Glyph(a[0]); }
    out->langRgb = 0;
    for (size_t i = 0; i < sizeof kLang / sizeof kLang[0]; i++)
        if (!wcscmp(out->top, kLang[i].lang)) out->langRgb = Darken(kLang[i].rgb);
    if (!out->langRgb) {   // 모르는 언어: 위 두 글자로 색을 고른다
        unsigned hl = 2166136261u;
        for (const wchar_t *p = out->top; *p; p++) hl = (hl ^ (unsigned)*p) * 16777619u;
        out->langRgb = Darken(kSpare[(hl >> 3) % (sizeof kSpare / sizeof kSpare[0])]);
    }
    for (int i = 0; i < LayoutIcon_KnownCount(); i++)
        if (!wcscmp(a, kKnown[i].abbrev)) { out->rgb = Darken(kKnown[i].rgb); return true; }
    // 모르는 자판: abbrev 로 색을 고른다(같은 abbrev 는 늘 같은 색)
    unsigned h = 2166136261u;
    for (const wchar_t *p = a; *p; p++) h = (h ^ (unsigned)*p) * 16777619u;
    out->rgb = Darken(kSpare[h % (sizeof kSpare / sizeof kSpare[0])]);
    return true;
}
