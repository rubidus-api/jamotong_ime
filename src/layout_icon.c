// layout_icon.c — 트레이·언어 전환창의 자판 아이콘을 그린다 (글자·색은 layout_icon_style.c).
#include "layout_icon.h"
#include <windows.h>
#include <stdbool.h>
#include <wchar.h>
#include "icon_font8.h"
#include "icon_resample.h"

// ── 자판 아이콘: 2x2 글자 (2026-11-12 오너 요청 "작을 때 식별이 안 된다 — 위에 언어, 아래에 자판 이름, 가독성 있게";
//   2026-11-13 오너 요청 "가장 바깥에 1px 패딩, 글자색은 언어·자판마다, 배경은 통일 — 밝은 배경에 어두운 글씨") ─────────────
//   16x16 점 그림 한 장: 한 가지 밝은 회색 바탕에 위 줄 언어 두 글자(언어 색)·아래 줄 자판 두 글자(자판 색). 글자는 오너가 직접 그린
//   6x6 점 글꼴(icon_font8.h, 글자 안에 좌우 여백 한 칸씩): 칸을 가로 2 + i*6, 세로 2 + r*6 에 둔다(빈칸2+글자6+글자6+빈칸2).
//   16px 가 기준이다. 다른 크기(125% = 20px, 150% = 24px, 200% = 32px …)는 이 그림을 Lanczos-3 로 선형광에서 키운다(icon_resample.c;
//   2026-11-13 오너 요청 "16px 기준, 나머지는 초고품질 리사이즈 필터"). GDI 글꼴이 없으니 굵기·모양이 어디서나 같다. 네 모서리 한 점은 비워 둥글게 보인다.
#define ART 16
static unsigned Bgra(unsigned rgb) { return 0xFF000000u | (rgb & 0xFFFFFFu); }   // 0xAARRGGBB (DIB 의 32비트 화소)

// 점 글꼴에서 글자를 찾는다 (없으면 '?')
static const IconGlyph8 *FindGlyph(wchar_t ch) {
    const IconGlyph8 *q = &g_icon8[0];
    for (int i = 0; i < ICON8_COUNT; i++) {
        if (g_icon8[i].ch == '?') q = &g_icon8[i];
    }
    for (int i = 0; i < ICON8_COUNT; i++)
        if ((wchar_t)(unsigned char)g_icon8[i].ch == ch) return &g_icon8[i];
    return q;
}

// 16x16 그림 (0 = 투명)
static void DrawArt(const LayoutIconStyle *st, unsigned art[ART][ART]) {
    for (int y = 0; y < ART; y++)
        for (int x = 0; x < ART; x++) art[y][x] = Bgra(LI_BACKGROUND_RGB);
    const wchar_t *rows[2] = { st->top, st->bottom };
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < 2 && rows[r][i]; i++) {
            const IconGlyph8 *gl = FindGlyph(rows[r][i]);
            const unsigned char *g = gl->rows;
            unsigned ink = Bgra(r == 0 ? st->langRgb : st->rgb);
            for (int y = 0; y < 6; y++)
                for (int x = 0; x < 6; x++)
                    if (g[y] & (1u << x)) art[2 + r * 6 + y][2 + i * 6 + x] = ink;
        }
    art[0][0] = art[0][ART - 1] = art[ART - 1][0] = art[ART - 1][ART - 1] = 0;
}

HICON LayoutIcon_Create(const wchar_t *abbrev, int size) {
    LayoutIconStyle st;
    LayoutIcon_Style(abbrev, &st);
    int sm = GetSystemMetrics(SM_CXSMICON);
    // 트레이 칸 크기 그대로 그린다(100% 16px, 125% 20px, 150% 24px)
    int sz = size > 0 ? size : (sm >= 16 ? sm : 16);
    unsigned art[ART][ART];
    DrawArt(&st, art);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sz; bi.bmiHeader.biHeight = -sz;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP color = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    int maskStride = ((sz + 15) / 16) * 2;
    void *maskBits = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)maskStride * sz);
    HBITMAP mask = maskBits ? CreateBitmap(sz, sz, 1, 1, maskBits) : NULL;
    HICON icon = NULL;
    if (color && bits && mask) {
        UINT32 *out = (UINT32 *)bits;
        if (sz == ART) {
            for (int y = 0; y < ART; y++)
                for (int x = 0; x < ART; x++) out[y * ART + x] = art[y][x];   // 16px 는 그림 그대로
        } else {
            IconResample(&art[0][0], ART, out, sz);   // 그 밖의 크기는 Lanczos-3 (선형광)
        }
        ICONINFO ii = { 0 };
        ii.fIcon = TRUE; ii.hbmColor = color; ii.hbmMask = mask;
        icon = CreateIconIndirect(&ii);
    }
    if (color) DeleteObject(color);
    if (mask) DeleteObject(mask);
    if (maskBits) HeapFree(GetProcessHeap(), 0, maskBits);
    return icon;
}
