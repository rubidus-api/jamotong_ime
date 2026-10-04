// layout_icon.c — 트레이·언어 전환창의 자판 아이콘을 그린다 (짜임은 layout_icon_style.c).
#include "layout_icon.h"
#include <windows.h>
#include <stdbool.h>
#include <wchar.h>

// ── 자판 아이콘 (2026-10-04 사용자 요청) ─────────────────────────────────────────────────
//   밝은 회색 둥근 바탕 + 왼쪽 위의 표지(자판마다 모양·색·글자가 다르다, layout_icon_style.c) + 오른쪽 아래의 언어 두 글자.
//   다크·라이트 작업 표시줄 모두에서 보이도록 바탕은 불투명한 밝은 회색에 어두운 테두리. 네 배 크기로 GDI 로 그린 뒤
//   평균을 내 줄인다(가장자리가 매끈하다). 알파는 우리가 셈한다 — GDI 는 알파를 쓰지 않으므로, 바탕 밖은 열쇠색으로 칠해
//   그 비율로 투명도를 정한다.
#define LI_KEY 0xFF00FFu   // 바탕 밖 (열쇠색)
static COLORREF Rgb(unsigned rgb) { return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }
static void ShapePoints(int shape, int x, int y, int w, POINT *pt, int *n) {
    static const double tri[] = { .5,.02, .98,.90, .02,.90 }, tridown[] = { .02,.08, .98,.08, .5,.98 },
        diamond[] = { .5,0, 1,.5, .5,1, 0,.5 }, para[] = { .24,.14, 1,.14, .76,.86, 0,.86 },
        hex[] = { .25,.05, .75,.05, 1,.5, .75,.95, .25,.95, 0,.5 },
        pent[] = { .5,.02, .98,.38, .80,.96, .20,.96, .02,.38 },
        oct[] = { .3,0, .7,0, 1,.3, 1,.7, .7,1, .3,1, 0,.7, 0,.3 },
        shield[] = { .06,.04, .94,.04, .94,.46, .86,.68, .70,.84, .5,.98, .30,.84, .14,.68, .06,.46 },
        star[] = { .5,0, .62,.36, 1,.38, .69,.60, .80,.98, .5,.76, .20,.98, .31,.60, 0,.38, .38,.36 };
    const double *v = NULL; int k = 0;
    switch (shape) {
        case LI_TRIANGLE: v = tri; k = 3; break;
        case LI_TRIDOWN: v = tridown; k = 3; break;
        case LI_DIAMOND: v = diamond; k = 4; break;
        case LI_PARALLELOGRAM: v = para; k = 4; break;
        case LI_HEXAGON: v = hex; k = 6; break;
        case LI_PENTAGON: v = pent; k = 5; break;
        case LI_OCTAGON: v = oct; k = 8; break;
        case LI_SHIELD: v = shield; k = 9; break;
        case LI_STAR: v = star; k = 10; break;
        default: break;
    }
    for (int i = 0; i < k; i++) { pt[i].x = x + (LONG)(v[2 * i] * w); pt[i].y = y + (LONG)(v[2 * i + 1] * w); }
    *n = k;
}
HICON LayoutIcon_Create(const wchar_t *abbrev, int size) {
    LayoutIconStyle st;
    LayoutIcon_Style(abbrev, &st);
    int sm = GetSystemMetrics(SM_CXSMICON);
    // 트레이 칸 크기 그대로 그린다(100% 16px, 125% 20px, 150% 24px) — 32px 를 셸이 줄이면 언어 글자가 뭉개졌다(VM 2026-10-04).
    int sz = size > 0 ? size : (sm >= 16 ? sm : 16);
    const bool compact = sz < 24;   // 작은 아이콘: 표지는 모양·색만, 언어 글자를 크게
    const int K = 4, S = sz * K;    // 네 배로 그린다
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = S; bi.bmiHeader.biHeight = -S;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *hiBits = NULL, *loBits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP hi = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &hiBits, NULL, 0) : NULL;
    bi.bmiHeader.biWidth = sz; bi.bmiHeader.biHeight = -sz;
    HBITMAP lo = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &loBits, NULL, 0);
    int maskStride = ((sz + 15) / 16) * 2;
    void *maskBits = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)maskStride * sz);
    HBITMAP mask = maskBits ? CreateBitmap(sz, sz, 1, 1, maskBits) : NULL;
    HICON icon = NULL;
    if (dc && hi && hiBits && lo && loBits && mask) {
        HGDIOBJ oldBmp = SelectObject(dc, hi);
        UINT32 *px = (UINT32 *)hiBits;
        for (int i = 0; i < S * S; i++) px[i] = LI_KEY;
        // 1) 바탕: 밝은 회색 둥근 네모, 어두운 테두리
        HBRUSH bg = CreateSolidBrush(Rgb(LI_BACKGROUND_RGB));
        HPEN border = CreatePen(PS_SOLID, K, Rgb(LI_BORDER_RGB));
        HGDIOBJ ob = SelectObject(dc, bg), op = SelectObject(dc, border);
        RoundRect(dc, K / 2, K / 2, S - K / 2, S - K / 2, S * 3 / 10, S * 3 / 10);
        // 2) 표지: 왼쪽 위 (바탕의 0.06 ~ 0.74, 작은 아이콘은 0.08 ~ 0.66)
        int ex = S * (compact ? 8 : 6) / 100, ey = ex, ew = S * (compact ? 58 : 68) / 100;
        HBRUSH fill = CreateSolidBrush(Rgb(st.rgb));
        HPEN edge = CreatePen(PS_SOLID, 1, Rgb(st.rgb));
        SelectObject(dc, fill); SelectObject(dc, edge);
        double gy = 0.5;   // 글자 자리 (표지 안의 세로 가운데, 모양마다)
        bool hollow = false;
        switch (st.shape) {
            case LI_CIRCLE: Ellipse(dc, ex, ey, ex + ew, ey + ew); break;
            case LI_SQUARE: Rectangle(dc, ex + ew / 16, ey + ew / 16, ex + ew - ew / 16, ey + ew - ew / 16); break;
            case LI_ROUNDSQ: RoundRect(dc, ex, ey, ex + ew, ey + ew, ew / 2, ew / 2); break;
            case LI_CAPSULE: RoundRect(dc, ex - ew / 20, ey + ew / 6, ex + ew + ew / 20, ey + ew - ew / 6, ew * 2 / 3, ew * 2 / 3); break;
            case LI_RING: {
                HPEN thick = CreatePen(PS_SOLID, ew / 8, Rgb(st.rgb));
                SelectObject(dc, thick); SelectObject(dc, bg);
                Ellipse(dc, ex + ew / 16, ey + ew / 16, ex + ew - ew / 16, ey + ew - ew / 16);
                SelectObject(dc, edge);
                DeleteObject(thick);
                hollow = true;
                break;
            }
            default: {
                POINT pt[12]; int n = 0;
                ShapePoints(st.shape, ex, ey, ew, pt, &n);
                if (n) Polygon(dc, pt, n);
                if (st.shape == LI_TRIANGLE) gy = 0.62;
                else if (st.shape == LI_TRIDOWN) gy = 0.38;
                else if (st.shape == LI_STAR) gy = 0.56;
                else if (st.shape == LI_SHIELD) gy = 0.44;
                break;
            }
        }
        // 3) 표지 안의 글자 — 표지에 맞게 크기를 고른다 (작은 아이콘은 그리지 않는다 — 몇 픽셀이라 읽히지 않는다)
        SetBkMode(dc, TRANSPARENT);
        if (compact) st.glyph[0] = 0;
        SetTextColor(dc, hollow ? Rgb(st.rgb) : RGB(255, 255, 255));
        int box = (st.shape == LI_TRIANGLE || st.shape == LI_TRIDOWN || st.shape == LI_STAR) ? ew * 46 / 100 : ew * 64 / 100;
        int fh = box;
        HFONT gf = NULL;
        for (int tries = 0; tries < 8; tries++) {
            if (gf) DeleteObject(gf);
            gf = CreateFontW(-fh, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, st.font);
            SelectObject(dc, gf);
            SIZE ts = { 0, 0 };
            GetTextExtentPoint32W(dc, st.glyph, (int)wcslen(st.glyph), &ts);
            if (ts.cx <= box) break;
            fh = fh * box / (ts.cx + 1);
        }
        RECT gr = { ex, ey + (LONG)(ew * gy) - ew / 2, ex + ew, ey + (LONG)(ew * gy) + ew / 2 };
        DrawTextW(dc, st.glyph, -1, &gr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        // 4) 언어 두 글자: 오른쪽 아래, 바탕색 띠 위에
        if (st.lang[0]) {
            SelectObject(dc, bg); SelectObject(dc, GetStockObject(NULL_PEN));
            if (!compact) RoundRect(dc, S * 58 / 100, S * 66 / 100, S - K * 2, S - K * 2, S / 7, S / 7);
            HFONT lf = CreateFontW(-(S * (compact ? 56 : 34) / 100), 0, 0, 0, FW_HEAVY, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            SelectObject(dc, lf);
            SetTextColor(dc, Rgb(LI_LANG_RGB));
            RECT lr = { compact ? S * 24 / 100 : S * 54 / 100, compact ? S * 40 / 100 : S * 62 / 100,
                        S - K * (compact ? 1 : 3), S - K * (compact ? 1 : 2) };
            DrawTextW(dc, st.lang, -1, &lr, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOCLIP);
            SelectObject(dc, gf);
            DeleteObject(lf);
        }
        SelectObject(dc, ob); SelectObject(dc, op); SelectObject(dc, oldBmp);
        DeleteObject(gf); DeleteObject(fill); DeleteObject(edge); DeleteObject(bg); DeleteObject(border);
        // 5) 줄이기: 네×네 칸의 평균. 열쇠색이 아닌 칸의 비율이 알파다.
        UINT32 *out = (UINT32 *)loBits;
        for (int y = 0; y < sz; y++)
            for (int x = 0; x < sz; x++) {
                unsigned r = 0, g = 0, b = 0, cnt = 0;
                for (int dy = 0; dy < K; dy++)
                    for (int dx = 0; dx < K; dx++) {
                        UINT32 c = px[(y * K + dy) * S + x * K + dx] & 0xFFFFFFu;
                        if (c == LI_KEY) continue;
                        r += (c >> 16) & 0xFF; g += (c >> 8) & 0xFF; b += c & 0xFF; cnt++;
                    }
                out[y * sz + x] = cnt ? (((cnt * 255u / (K * K)) << 24) | ((r / cnt) << 16) | ((g / cnt) << 8) | (b / cnt)) : 0u;
            }
        ICONINFO ii = { 0 };
        ii.fIcon = TRUE; ii.hbmColor = lo; ii.hbmMask = mask;
        icon = CreateIconIndirect(&ii);
    }
    if (hi) DeleteObject(hi);
    if (lo) DeleteObject(lo);
    if (mask) DeleteObject(mask);
    if (maskBits) HeapFree(GetProcessHeap(), 0, maskBits);
    if (dc) DeleteDC(dc);
    return icon;
}

