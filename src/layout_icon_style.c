// layout_icon_style.c — 자판마다 서로 다른 아이콘 표지 (layout_icon_style.h).
#include "layout_icon_style.h"
#include <string.h>

#define F_HANGUL L"Malgun Gothic"
#define F_LATIN  L"Segoe UI"
#define F_SC     L"Microsoft YaHei"
#define F_TC     L"Microsoft JhengHei"
#define F_JA     L"Yu Gothic UI"

// 실리는 자판: 모양은 모두 다르고 색도 모두 다르다(같은 자판의 내장판과 파일판만 같은 표지를 나눈다).
static const struct { const wchar_t *abbrev; int shape; unsigned rgb; const wchar_t *glyph; const wchar_t *font; } kKnown[] = {
    { L"KO2S", LI_CIRCLE,        0x1F6FD1u, L"\xB450",   F_HANGUL },   // 두벌식 표준 — 파란 원, 두
    { L"KO2B", LI_CIRCLE,        0x1F6FD1u, L"\xB450",   F_HANGUL },   //   (내장 두벌식 — 같은 표지)
    { L"KO3F", LI_SQUARE,        0x2E9E4Fu, L"\xC138",   F_HANGUL },   // 세벌식 최종 — 초록 네모, 세
    { L"KO3B", LI_SQUARE,        0x2E9E4Fu, L"\xC138",   F_HANGUL },   //   (내장 세벌식 — 같은 표지)
    { L"KO39", LI_DIAMOND,       0x0E8A8Au, L"90",       F_LATIN  },   // 세벌식 390 — 청록 마름모, 90
    { L"KO11", LI_HEXAGON,       0x7A8C1Eu, L"11",       F_LATIN  },   // 세벌식 3-2011 — 올리브 육각형, 11
    { L"KO12", LI_PENTAGON,      0x5B5FC7u, L"12",       F_LATIN  },   // 세벌식 3-2012 — 남색 오각형, 12
    { L"KOSA", LI_TRIANGLE,      0xE07B00u, L"\xC21C",   F_HANGUL },   // 순아래 — 주황 세모, 순
    { L"KO1H", LI_OCTAGON,       0x8E3FB0u, L"\xC190",   F_HANGUL },   // 한 손 — 보라 팔각형, 손
    { L"KOEX", LI_RING,          0x8B5A2Bu, L"\xC608",   F_HANGUL },   // 예제 한글 — 갈색 고리, 예
    { L"ENQW", LI_PARALLELOGRAM, 0x4A4A4Au, L"Q",        F_LATIN  },   // QWERTY — 짙은 회색 평행사변형, Q
    { L"ENDV", LI_TRIDOWN,       0xC0392Bu, L"D",        F_LATIN  },   // Dvorak — 빨간 역삼각형, D
    { L"ENDX", LI_TRIDOWN,       0xC0392Bu, L"D",        F_LATIN  },   //   (예제 Dvorak — 같은 표지)
    { L"ENAR", LI_STAR,          0xC9970Cu, L"A",        F_LATIN  },   // ARTSEY — 금색 별, A
    { L"ZHSP", LI_CAPSULE,       0xD32F2Fu, L"\x7B80",   F_SC     },   // 중국어 간체 — 빨간 알약, 简
    { L"ZHTP", LI_SHIELD,        0x7B1E3Au, L"\x7E41",   F_TC     },   // 중국어 번체 — 자주 방패, 繁
    { L"JARO", LI_ROUNDSQ,       0xD8457Fu, L"\x3042",   F_JA     },   // 일본어 — 분홍 둥근 네모, あ
};
// 모르는 자판(사용자 자판)에 쓸 색 — 위와 겹치지 않는 색들
static const unsigned kSpare[] = { 0x2C7BB6u, 0x1A9850u, 0xD95F02u, 0x7570B3u, 0xE7298Au, 0x66A61Eu, 0xA6761Du, 0x1B9E77u };

int LayoutIcon_KnownCount(void) { return (int)(sizeof kKnown / sizeof kKnown[0]); }
const wchar_t *LayoutIcon_KnownAbbrev(int i) { return (i >= 0 && i < LayoutIcon_KnownCount()) ? kKnown[i].abbrev : L""; }

static wchar_t Lower(wchar_t c) { return (c >= L'A' && c <= L'Z') ? (wchar_t)(c + 32) : c; }

bool LayoutIcon_Style(const wchar_t *abbrev, LayoutIconStyle *out) {
    memset(out, 0, sizeof *out);
    const wchar_t *a = abbrev && abbrev[0] ? abbrev : L"?";
    // 언어: 앞 두 글자 (ZH → cn, JA → jp — 사용자가 고른 표기)
    if (a[0] && a[1] && wcscmp(a, L"--")) {
        if (!wcsncmp(a, L"ZH", 2)) wcscpy(out->lang, L"cn");
        else if (!wcsncmp(a, L"JA", 2)) wcscpy(out->lang, L"jp");
        else { out->lang[0] = Lower(a[0]); out->lang[1] = Lower(a[1]); out->lang[2] = 0; }
    }
    if (!wcscmp(a, L"--")) {   // 무간섭(직접 입력) 모드
        out->shape = LI_SQUARE; out->rgb = 0x777777u; wcscpy(out->glyph, L"\x2014"); out->font = F_LATIN;
        return true;
    }
    for (int i = 0; i < LayoutIcon_KnownCount(); i++)
        if (!wcscmp(a, kKnown[i].abbrev)) {
            out->shape = kKnown[i].shape; out->rgb = kKnown[i].rgb; out->font = kKnown[i].font;
            for (int k = 0; k < 3 && kKnown[i].glyph[k]; k++) out->glyph[k] = kKnown[i].glyph[k];
            return true;
        }
    // 모르는 자판: abbrev 로 모양·색을 고른다(같은 abbrev 는 늘 같은 표지). 글자는 뒤 두 글자(없으면 앞 글자).
    unsigned h = 2166136261u;
    for (const wchar_t *p = a; *p; p++) h = (h ^ (unsigned)*p) * 16777619u;
    out->shape = (int)(h % LI_SHAPES);
    out->rgb = kSpare[(h / LI_SHAPES) % (sizeof kSpare / sizeof kSpare[0])];
    out->font = F_LATIN;
    size_t n = wcslen(a);
    if (n >= 4) { out->glyph[0] = a[2]; out->glyph[1] = a[3]; }
    else if (n == 3) out->glyph[0] = a[2];
    else out->glyph[0] = a[0];
    for (int i = 0; out->glyph[i]; i++) if ((out->glyph[i] >= 0xAC00 && out->glyph[i] <= 0xD7A3) || out->glyph[i] >= 0x1100) out->font = F_HANGUL;
    return true;
}
