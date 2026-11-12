// layout_icon.c — 트레이·언어 전환창의 자판 아이콘을 그린다 (글자·색은 layout_icon_style.c).
#include "layout_icon.h"
#include <windows.h>
#include <stdbool.h>
#include <wchar.h>
#include "icon_font8.h"

// ── 자판 아이콘: 2x2 글자 (2026-11-12 오너 요청 "작을 때 식별이 안 된다 — 위에 언어, 아래에 자판 이름, 가독성 있게") ─────────────
//   16x16 점 그림 한 장을 만든다: 위 절반(8행) = 밝은 회색 바탕에 언어 두 글자(어두운 글자), 아래 절반 = 자판 색 띠에 자판 두 글자
//   (흰 글자). 글자는 직접 조형한 7x7 점 글꼴(icon_font8.h, 8x8 칸)이라 100% 트레이(16px)에서 점 하나도 뭉개지지 않는다. 다른 크기
//   (125% = 20px, 150% = 24px, 200% = 32px …)는 이 그림을 가장 가까운 점으로 늘린다 — 두 칸을 붙여 섞으면 흐려지므로 섞지 않는다.
//   GDI 글꼴·번짐이 없으니 굵기·모양이 어디서나 같다. 네 모서리 한 점은 비워 둥글게 보인다.
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
        for (int x = 0; x < ART; x++) art[y][x] = Bgra(y < 8 ? LI_BACKGROUND_RGB : st->rgb);
    const wchar_t *rows[2] = { st->top, st->bottom };
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < 2 && rows[r][i]; i++) {
            const IconGlyph8 *gl = FindGlyph(rows[r][i]);
            const unsigned char *g = gl->rows;
            unsigned ink = Bgra(r == 0 ? LI_TEXT_RGB : LI_BAND_TEXT_RGB);
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    if (g[y] & (1u << x)) art[r * 8 + y][i * 8 + x] = ink;
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
        for (int y = 0; y < sz; y++)
            for (int x = 0; x < sz; x++) out[y * sz + x] = art[y * ART / sz][x * ART / sz];
        ICONINFO ii = { 0 };
        ii.fIcon = TRUE; ii.hbmColor = color; ii.hbmMask = mask;
        icon = CreateIconIndirect(&ii);
    }
    if (color) DeleteObject(color);
    if (mask) DeleteObject(mask);
    if (maskBits) HeapFree(GetProcessHeap(), 0, maskBits);
    return icon;
}
