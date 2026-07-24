#include "langbar.h"
#include "jamotong.h"
#include "settings_ui.h"
#include "version.h"
#include "icon_font.h"   // 아이콘용 5x8 비트맵 글리프 (Spleen, BSD-2 — 헤더 상단 고지 참조)
#include <stddef.h>

#ifndef TF_LBI_ICON
#define TF_LBI_ICON 0x00000001   // 이 MinGW msctf.h엔 없음 (표준값). 아이콘 갱신 통지 플래그.
#endif

// TSF ITfMenu (langbar right-click menu). This MinGW's msctf.h does not expose it under
// CINTERFACE, so declare the minimal C vtable we need. ABI-matched to msctf.h: AddMenuItem
// is the 4th slot after IUnknown.
typedef struct ITfMenu ITfMenu;
typedef struct ITfMenuVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ITfMenu*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(ITfMenu*);
    ULONG   (STDMETHODCALLTYPE *Release)(ITfMenu*);
    HRESULT (STDMETHODCALLTYPE *AddMenuItem)(ITfMenu*, UINT, DWORD, HBITMAP, HBITMAP, const WCHAR*, ULONG, ITfMenu**);
} ITfMenuVtbl;
struct ITfMenu { const ITfMenuVtbl *lpVtbl; };

// GUID_LBI_INPUTMODE {2C77A81E-41CC-4178-A3A7-5F8A987568E6} — Win8+에서 랭바 아이템의
// guidItem이 이 값이어야만 트레이 '입력 표시기'가 아이템을 호스팅한다(다른 GUID는 무시됨).
// MinGW ctfutb.h에 없어 직접 정의 (값 출처: Windows SDK 메타데이터/windows-rs, MIT).
static const GUID GUID_LBI_INPUTMODE_J =
{ 0x2c77a81e, 0x41cc, 0x4178, { 0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6 } };

const GUID IID_ITfLangBarItemButton = 
{ 0x28c7f1d0, 0xde25, 0x11d2, { 0xaf, 0xdd, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 } };

#define IMPL_LBI_BUTTON(ptr) ((JamotongLangBarItem*)((char*)(ptr) - offsetof(JamotongLangBarItem, lpVtblButton)))
#define IMPL_LBI_SOURCE(ptr) ((JamotongLangBarItem*)((char*)(ptr) - offsetof(JamotongLangBarItem, lpVtblSource)))

// ------------------------------------------------------------------
// ITfLangBarItemButton (inherits the ITfLangBarItem vtable prefix)
// ------------------------------------------------------------------

static ULONG LBI_AddRefObject(JamotongLangBarItem *obj) {
    return (ULONG)InterlockedIncrement(&obj->refCount);
}

static ULONG LBI_ReleaseObject(JamotongLangBarItem *obj) {
    ULONG res = (ULONG)InterlockedDecrement(&obj->refCount);
    if (res == 0) {
        if (obj->pSink) obj->pSink->lpVtbl->Release(obj->pSink);
        HeapFree(GetProcessHeap(), 0, obj);
    }
    return res;
}

static HRESULT LBI_QueryInterfaceObject(JamotongLangBarItem *obj, REFIID riid,
                                        void **ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_ITfLangBarItem) ||
        IsEqualIID(riid, &IID_ITfLangBarItemButton)) {
        *ppvObject = &obj->lpVtblButton;
    } else if (IsEqualIID(riid, &IID_ITfSource)) {
        *ppvObject = &obj->lpVtblSource;
    } else {
        return E_NOINTERFACE;
    }
    LBI_AddRefObject(obj);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE LBI_QueryInterface(ITfLangBarItemButton *pThis,
                                                    REFIID riid,
                                                    void **ppvObject) {
    return LBI_QueryInterfaceObject(IMPL_LBI_BUTTON(pThis), riid, ppvObject);
}

static ULONG STDMETHODCALLTYPE LBI_AddRef(ITfLangBarItemButton *pThis) {
    return LBI_AddRefObject(IMPL_LBI_BUTTON(pThis));
}

static ULONG STDMETHODCALLTYPE LBI_Release(ITfLangBarItemButton *pThis) {
    return LBI_ReleaseObject(IMPL_LBI_BUTTON(pThis));
}

static HRESULT STDMETHODCALLTYPE LBI_GetInfo(ITfLangBarItemButton *pThis,
                                             TF_LANGBARITEMINFO *pInfo) {
    (void)pThis;
    if (!pInfo) return E_INVALIDARG;
    pInfo->clsidService = CLSID_JamotongIME;
    pInfo->guidItem = GUID_LBI_INPUTMODE_J;   // ★트레이 입력 표시기 호스팅의 필수 조건
    // Keep the official legacy tray-style bit, but modern input-indicator hosting is keyed by
    // GUID_LBI_INPUTMODE_J above; SHOWNINTRAY alone is not a visibility guarantee.
    pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
    pInfo->ulSort = 0;
    lstrcpyW(pInfo->szDescription, L"Jamotong Layout");
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE LBI_GetStatus(ITfLangBarItemButton *pThis,
                                               DWORD *pdwStatus) {
    (void)pThis;
    if (!pdwStatus) return E_INVALIDARG;
    *pdwStatus = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE LBI_Show(ITfLangBarItemButton *pThis, BOOL fShow) {
    (void)pThis; (void)fShow;
    return S_OK;   // 표시 요청 수락 (E_NOTIMPL을 돌려주면 셸 랭바 처리가 꼬일 수 있음)
}

static HRESULT STDMETHODCALLTYPE LBI_GetTooltipString(ITfLangBarItemButton *pThis,
                                                      BSTR *pbstrToolTip) {
    (void)pThis;
    if (!pbstrToolTip) return E_INVALIDARG;
    *pbstrToolTip = SysAllocString(L"Jamotong IME");
    return *pbstrToolTip ? S_OK : E_OUTOFMEMORY;
}

static void ExecMenuCmd(JamotongLangBarItem *obj, UINT wID);   // 아래 정의 (우클릭 팝업에서 사용)

static HRESULT STDMETHODCALLTYPE LBI_OnClick(ITfLangBarItemButton *pThis, TfLBIClick click, POINT pt, const RECT *prcArea) {
    JamotongLangBarItem *obj = IMPL_LBI_BUTTON(pThis);
    (void)pt; (void)prcArea;
    if (!obj->pService) return S_OK;   // Deactivate 후 셸이 잡고 있던 아이템 — 서비스 접근 금지(UAF 방어)

    if (click == TF_LBI_CLK_LEFT) {
        // 좌클릭: 레이아웃 순환
        Config_RotateLayout(&obj->pService->config);
        LangBar_Update(obj);
        Jamotong_PublishStatus(&obj->pService->config);
    } else if (click == TF_LBI_CLK_RIGHT) {
        // 우클릭: 자체 컨텍스트 메뉴. BTN_BUTTON 스타일은 우클릭도 OnClick으로 오며
        // InitMenu(ITfMenu)는 호출되지 않는다(BTN_MENU 전용) — Mozc와 동일 방식.
        HMENU menu = CreatePopupMenu();
        if (menu) {
            AppendMenuW(menu, MF_STRING, 1, L"Settings...");
            AppendMenuW(menu, MF_STRING, 2, L"Next layout");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING, 3, L"About Jamotong IME...");
            POINT p = pt;
            HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);   // 가장자리 클램프
            if (mon) {
                MONITORINFO mi; mi.cbSize = sizeof(mi);
                if (GetMonitorInfoW(mon, &mi)) {
                    if (p.x < mi.rcWork.left)  p.x = mi.rcWork.left;
                    if (p.x > mi.rcWork.right) p.x = mi.rcWork.right;
                }
            }
            HWND owner = GetFocus();                     // 메뉴는 owner 창 필요(표시기는 안 줌)
            if (!owner) owner = GetForegroundWindow();
            // TPM_NONOTIFY: owner 앱이 메뉴 상태를 건드리는 부작용 차단(Mozc가 IE10에서 겪은 이슈)
            int cmd = (int)TrackPopupMenu(menu, TPM_NONOTIFY | TPM_RETURNCMD | TPM_LEFTBUTTON |
                                          TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, 0, owner, NULL);
            DestroyMenu(menu);
            if (cmd > 0) ExecMenuCmd(obj, (UINT)cmd);
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE LBI_InitMenu(ITfLangBarItemButton *pThis, void *pMenu) {
    (void)pThis;
    // 우클릭 메뉴에 "Settings..." 항목 추가 (id 1 → OnMenuSelect에서 SettingsUI_Show 호출).
    // 버그 수정: 기존엔 AddMenuItem이 주석 처리돼 설정창을 여는 유일한 경로가 막혀 있었음.
    // ITfMenu는 msctf.h(CINTERFACE)가 제공하므로 void* 파라미터를 캐스트해 사용한다.
    ITfMenu *menu = (ITfMenu*)pMenu;
    if (menu) {
        menu->lpVtbl->AddMenuItem(menu, 1, 0, NULL, NULL, L"Settings...", 11, NULL);
        menu->lpVtbl->AddMenuItem(menu, 2, 0, NULL, NULL, L"Next layout", 11, NULL);
        menu->lpVtbl->AddMenuItem(menu, 3, 0, NULL, NULL, L"About Jamotong IME...", 21, NULL);
    }
    return S_OK;
}

// 메뉴 명령 실행 (우클릭 자체 팝업과 레거시 OnMenuSelect가 공유)
static void ExecMenuCmd(JamotongLangBarItem *obj, UINT wID) {
    if (!obj->pService) return;
    if (wID == 1) {
        SettingsUI_Show(&obj->pService->config);   // 설정창 (별도 스레드)
    } else if (wID == 2) {
        Config_RotateLayout(&obj->pService->config);   // 다음 자판
        LangBar_Update(obj);
        Jamotong_PublishStatus(&obj->pService->config);
    } else if (wID == 3) {
        MessageBoxW(NULL,
            L"Jamotong IME  " JAMOTONG_VERSION L"\n\n"
            L"Pure-C Korean/Hangul IME (Text Services Framework).\n"
            L"Left-click the tray icon to cycle layouts;\n"
            L"right-click for this menu.",
            L"About Jamotong IME", MB_OK | MB_TOPMOST | MB_SETFOREGROUND | MB_ICONINFORMATION);
    }
}

static HRESULT STDMETHODCALLTYPE LBI_OnMenuSelect(ITfLangBarItemButton *pThis, UINT wID) {
    JamotongLangBarItem *obj = IMPL_LBI_BUTTON(pThis);
    if (!obj->pService) return S_OK;   // Deactivate 후 — 서비스 접근 금지(UAF 방어)
    ExecMenuCmd(obj, wID);
    return S_OK;
}

// ── 아이콘 색 (사용자 지정 2026-07-24): 투명 배경 + 흰 외곽선 + 어두운 보라 글자 ──
//   ARGB(리틀엔디언 UINT32 = 0xAARRGGBB). 프로필 아이콘 생성기(gen_profile_icon.py)와 동일 값.
#define ICON_GLYPH_ARGB   0xFF4B0082u   // dark purple (#4B0082)
#define ICON_OUTLINE_ARGB 0xFFFFFFFFu   // white outline

// 글리프의 폰트 픽셀 (r,c)가 켜져 있는가 (범위 밖=꺼짐 — 외곽선 패스의 패딩 접근용).
static int GlyphBit(int glyphIndex, int r, int c) {
    if (r < 0 || r >= ICONFONT_H || c < 0 || c >= ICONFONT_W) return 0;
    return (g_iconFontBits[glyphIndex][r] & (0x80u >> c)) != 0;
}

// 확대 블록 채우기 (캔버스 경계 클립). onlyTransparent=외곽선용: 이미 칠해진(글자) 픽셀 보존.
static void FillArgbBlock(UINT32 *px, int sz, int x0, int y0, int w, int h,
                          UINT32 color, BOOL onlyTransparent) {
    for (int y = y0; y < y0 + h; y++) {
        if (y < 0 || y >= sz) continue;
        for (int x = x0; x < x0 + w; x++) {
            if (x < 0 || x >= sz) continue;
            if (!onlyTransparent || px[y * sz + x] == 0)
                px[y * sz + x] = color;
        }
    }
}

// 비트맵 글꼴 아이콘: 32bpp ARGB DIB에 직접 그린다 — 투명 배경, 어두운 보라 글자,
// 외곽선은 '캔버스 픽셀' 1px 팽창(투명 픽셀 중 8방향 이웃에 글자가 있는 곳만 흰색).
// 폰트 픽셀 단위 팽창은 글자 사이를 흰 덩어리로 메워서 기각(2026-07-24).
// 정수 배율(x/y 독립)이라 픽셀 경계가 또렷하다(안티에일리어스 없음, 의도된 비트맵 룩).
static HICON CreateGlyphAbbrevIcon(const wchar_t *text, int len, const RECT *cell, int sz) {
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sz;
    bi.bmiHeader.biHeight = -sz;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP hbmColor = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    // 32bpp ARGB 아이콘은 알파가 가시성을 결정하지만 CreateIconIndirect는 마스크가 필수 —
    // 0으로 채운 1bpp 마스크를 명시적으로 만든다(CreateBitmap(NULL)은 내용 미정의).
    int maskStride = ((sz + 15) / 16) * 2;   // 1bpp, WORD 정렬
    void *maskBits = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)maskStride * sz);
    HBITMAP hbmMask = maskBits ? CreateBitmap(sz, sz, 1, 1, maskBits) : NULL;
    HICON hIcon = NULL;
    if (hbmColor && bits && hbmMask) {
        UINT32 *px = (UINT32*)bits;   // DIB는 0으로 초기화 = 전부 투명
        for (int i = 0; i < len; i++) {   // 1) 글자(보라)
            int gi = IconFont_Index((unsigned int)text[i]);
            int cellW = cell[i].right - cell[i].left, cellH = cell[i].bottom - cell[i].top;
            int sx = cellW / ICONFONT_W; if (sx < 1) sx = 1;
            int sy = cellH / ICONFONT_H; if (sy < 1) sy = 1;
            int ox = cell[i].left + (cellW - ICONFONT_W * sx) / 2;
            int oy = cell[i].top  + (cellH - ICONFONT_H * sy) / 2;
            for (int r = 0; r < ICONFONT_H; r++)
                for (int c = 0; c < ICONFONT_W; c++)
                    if (GlyphBit(gi, r, c))
                        FillArgbBlock(px, sz, ox + c * sx, oy + r * sy, sx, sy,
                                      ICON_GLYPH_ARGB, FALSE);
        }
        // 2) 외곽선: 투명 픽셀 중 8방향 이웃에 글자 색이 있는 곳만 흰색.
        //    글자 색만 이웃 판정에 쓰므로 in-place로도 순서 무관.
        for (int y = 0; y < sz; y++) {
            for (int x = 0; x < sz; x++) {
                if (px[y * sz + x] != 0) continue;
                int touching = 0;   /* 'near'는 windows.h 레거시 매크로(빈 정의)라 쓰면 안 됨 */
                for (int dy = -1; dy <= 1 && !touching; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int ny = y + dy, nx = x + dx;
                        if (ny >= 0 && ny < sz && nx >= 0 && nx < sz &&
                            px[ny * sz + nx] == ICON_GLYPH_ARGB) { touching = 1; break; }
                    }
                if (touching) px[y * sz + x] = ICON_OUTLINE_ARGB;
            }
        }
        ICONINFO ii = { 0 };
        ii.fIcon = TRUE;
        ii.hbmColor = hbmColor;
        ii.hbmMask = hbmMask;
        hIcon = CreateIconIndirect(&ii);
    }
    if (hbmColor) DeleteObject(hbmColor);
    if (hbmMask) DeleteObject(hbmMask);
    if (maskBits) HeapFree(GetProcessHeap(), 0, maskBits);
    return hIcon;
}

// 현재 자판 식별자(abbrev, 2~4글자)를 실시간 렌더한 언어바/트레이 아이콘.
// - 글자를 2x2 격자로 배치해 작은 아이콘에서도 각 글자를 최대 크기로 → 3글자도 판독 가능.
//   1글자=꽉 채움, 2글자=가로 2칸, 3글자=위 2·아래 1(가운데), 4글자=2x2.
// - 1차: abbrev 전 글자가 A-Z/0-9/'?'면 내장 5x8 비트맵 글꼴(Spleen, BSD-2 — icon_font.h)로
//   **투명 배경 + 흰 외곽선 + 어두운 보라 글자**를 그린다(사용자 지정 2026-07-24). 기본 자판
//   (ENQW/ENDV/KO2B/KO3B)과 프로필 아이콘(JMTO)이 같은 글꼴·색·규칙을 공유한다.
// - 폴백: 한글 등 미수록 글자가 있으면 종전 스타일(흑배경·백글자, 시스템 '돋움' GDI 이름
//   참조 — 파일 미번들이라 폰트 재배포 라이선스 없음, COPYRIGHT.md)로 렌더.
// - 캔버스는 DPI 반영(SM_CXSMICON) 하되 최소 32px로 렌더 → 언어 전환창(24~32px)에서 선명, 트레이(16px)는
//   셸이 축소. 호출자(셸)가 아이콘을 소유·파괴하므로 매 호출 새 HICON.
static HICON CreateAbbrevIcon(const wchar_t *text) {
    int len = (int)wcslen(text); if (len < 1) { text = L"?"; len = 1; }
    if (len > 4) len = 4;   // 2x2 격자 = 최대 4글자
    int sm = GetSystemMetrics(SM_CXSMICON);
    int sz = (sm > 32) ? sm : 32;   // 전환창 선명도 위해 최소 32
    int half = sz / 2;

    // 글자별 셀(사각형)과 공통 글꼴 높이 결정 (셀 짧은 변에 맞춤).
    RECT cell[4]; int fontH;
    if (len == 1) {
        SetRect(&cell[0], 0, 0, sz, sz);
        fontH = (int)(sz * 0.82);
    } else if (len == 2) {
        SetRect(&cell[0], 0, 0, half, sz);
        SetRect(&cell[1], half, 0, sz, sz);
        fontH = (int)(half * 0.95);
    } else if (len == 3) {
        SetRect(&cell[0], 0, 0, half, half);           // 위-좌
        SetRect(&cell[1], half, 0, sz, half);           // 위-우
        SetRect(&cell[2], sz / 4, half, sz / 4 + half, sz); // 아래-가운데
        fontH = (int)(half * 0.95);
    } else {
        SetRect(&cell[0], 0, 0, half, half);
        SetRect(&cell[1], half, 0, sz, half);
        SetRect(&cell[2], 0, half, half, sz);
        SetRect(&cell[3], half, half, sz, sz);
        fontH = (int)(half * 0.95);
    }

    // 비트맵 글꼴 경로: 전 글자가 수록 글리프면 투명 배경 ARGB 아이콘 (아래 GDI 경로는 폴백)
    {
        bool bitmapOk = true;
        for (int i = 0; i < len; i++)
            if (IconFont_Index((unsigned int)text[i]) < 0) { bitmapOk = false; break; }
        if (bitmapOk) return CreateGlyphAbbrevIcon(text, len, cell, sz);
    }

    HDC hdcScr = GetDC(NULL);
    if (!hdcScr) return NULL;
    HDC hdc = CreateCompatibleDC(hdcScr);
    HBITMAP hbmColor = CreateCompatibleBitmap(hdcScr, sz, sz);
    HBITMAP hbmMask  = CreateBitmap(sz, sz, 1, 1, NULL);
    HICON hIcon = NULL;
    if (hdc && hbmColor && hbmMask) {
        HGDIOBJ oldBmp = SelectObject(hdc, hbmColor);
        RECT full = { 0, 0, sz, sz };
        HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));   // 모드 아이콘 지침: 흑백 전용
        FillRect(hdc, &full, bg);
        DeleteObject(bg);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        // '돋움' 우선, 없으면 GDI가 유사 글꼴 대체. 굵게+안티에일리어스(32px 렌더→축소 시 매끈).
        HFONT hf = CreateFontW(fontH, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                               DEFAULT_PITCH, L"\xB3CB\xC6C0" /* 돋움 */);
        HGDIOBJ oldFont = SelectObject(hdc, hf);
        for (int i = 0; i < len; i++)
            DrawTextW(hdc, &text[i], 1, &cell[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        SelectObject(hdc, oldFont);
        DeleteObject(hf);
        SelectObject(hdc, oldBmp);
        // 마스크 전부 불투명(0) → 색 비트맵 전체가 보임.
        HDC hdcM = CreateCompatibleDC(hdcScr);
        if (hdcM) {   // DC 고갈 시 NULL — 마스크 초기화 실패면 아이콘 생성 자체를 포기(미정의 마스크 방지)
            HGDIOBJ oldM = SelectObject(hdcM, hbmMask);
            PatBlt(hdcM, 0, 0, sz, sz, BLACKNESS);
            SelectObject(hdcM, oldM);
            DeleteDC(hdcM);
            ICONINFO ii = { 0 };
            ii.fIcon = TRUE;
            ii.hbmColor = hbmColor;
            ii.hbmMask = hbmMask;
            hIcon = CreateIconIndirect(&ii);
        }
    }
    if (hbmColor) DeleteObject(hbmColor);
    if (hbmMask) DeleteObject(hbmMask);
    if (hdc) DeleteDC(hdc);
    ReleaseDC(NULL, hdcScr);
    return hIcon;
}

static HRESULT STDMETHODCALLTYPE LBI_GetIcon(ITfLangBarItemButton *pThis, HICON *phIcon) {
    JamotongLangBarItem *obj = IMPL_LBI_BUTTON(pThis);
    if (!phIcon) return E_INVALIDARG;
    if (!obj->pService) { *phIcon = NULL; return S_OK; }   // Deactivate 후 — UAF 방어
    EnterCriticalSection(&g_configLock);
    LayoutConfig *layout = Config_GetCurrentLayout(&obj->pService->config);
    const wchar_t *ab = (layout && layout->abbrev[0]) ? layout->abbrev : L"?";
    *phIcon = CreateAbbrevIcon(ab);   // 셸이 소유·파괴. 현재 자판 축약 표시.
    LeaveCriticalSection(&g_configLock);
    return S_OK;   // API contract permits a successful NULL icon.
}

static HRESULT STDMETHODCALLTYPE LBI_GetText(ITfLangBarItemButton *pThis, BSTR *pbstrText) {
    JamotongLangBarItem *obj = IMPL_LBI_BUTTON(pThis);
    if (!pbstrText) return E_INVALIDARG;
    if (!obj->pService) { *pbstrText = SysAllocString(L"?"); return *pbstrText ? S_OK : E_OUTOFMEMORY; }
    EnterCriticalSection(&g_configLock);   // 설정 적용이 name을 free하는 것과 직렬화 (UAF 방지)
    LayoutConfig *layout = Config_GetCurrentLayout(&obj->pService->config);
    *pbstrText = SysAllocString(layout && layout->name ? layout->name : L"?");
    LeaveCriticalSection(&g_configLock);
    return *pbstrText ? S_OK : E_OUTOFMEMORY;
}

static struct ITfLangBarItemButtonVtbl LangBarItemButtonVtbl = {
    LBI_QueryInterface, LBI_AddRef, LBI_Release,
    LBI_GetInfo, LBI_GetStatus, LBI_Show, LBI_GetTooltipString,
    LBI_OnClick, LBI_InitMenu, LBI_OnMenuSelect, LBI_GetIcon, LBI_GetText
};

// ------------------------------------------------------------------
// ITfSource
// ------------------------------------------------------------------

static HRESULT STDMETHODCALLTYPE LBS_QueryInterface(ITfSource *pThis, REFIID riid, void **ppvObject) {
    return LBI_QueryInterfaceObject(IMPL_LBI_SOURCE(pThis), riid, ppvObject);
}

static ULONG STDMETHODCALLTYPE LBS_AddRef(ITfSource *pThis) {
    return LBI_AddRefObject(IMPL_LBI_SOURCE(pThis));
}

static ULONG STDMETHODCALLTYPE LBS_Release(ITfSource *pThis) {
    return LBI_ReleaseObject(IMPL_LBI_SOURCE(pThis));
}

static HRESULT STDMETHODCALLTYPE LBS_AdviseSink(ITfSource *pThis, REFIID riid, IUnknown *punk, DWORD *pdwCookie) {
    JamotongLangBarItem *obj = IMPL_LBI_SOURCE(pThis);
    ITfLangBarItemSink *sink = NULL;
    HRESULT hr;
    if (!punk || !pdwCookie) return E_INVALIDARG;
    *pdwCookie = 0;
    if (!IsEqualIID(riid, &IID_ITfLangBarItemSink))
        return CONNECT_E_CANNOTCONNECT;
    if (obj->pSink) return CONNECT_E_ADVISELIMIT;

    hr = punk->lpVtbl->QueryInterface(
        punk, &IID_ITfLangBarItemSink, (void**)&sink);
    if (SUCCEEDED(hr) && !sink) hr = E_NOINTERFACE;
    if (FAILED(hr)) {
        if (sink) sink->lpVtbl->Release(sink);
        return CONNECT_E_CANNOTCONNECT;
    }
    obj->pSink = sink;
    obj->sinkCookie = 1;
    *pdwCookie = obj->sinkCookie;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE LBS_UnadviseSink(ITfSource *pThis, DWORD dwCookie) {
    JamotongLangBarItem *obj = IMPL_LBI_SOURCE(pThis);
    if (dwCookie == obj->sinkCookie && obj->pSink) {
        obj->pSink->lpVtbl->Release(obj->pSink);
        obj->pSink = NULL;
        obj->sinkCookie = 0;
        return S_OK;
    }
    return CONNECT_E_NOCONNECTION;
}

static ITfSourceVtbl SourceVtbl = {
    LBS_QueryInterface, LBS_AddRef, LBS_Release,
    LBS_AdviseSink, LBS_UnadviseSink
};

// ------------------------------------------------------------------
// Public Functions
// ------------------------------------------------------------------

JamotongLangBarItem* LangBar_Create(JamotongTextService *pService) {
    JamotongLangBarItem *obj = (JamotongLangBarItem*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(JamotongLangBarItem));
    if (obj) {
        obj->lpVtblButton = &LangBarItemButtonVtbl;
        obj->lpVtblSource = &SourceVtbl;
        obj->refCount = 1;
        obj->pService = pService;
    }
    return obj;
}

void LangBar_Update(JamotongLangBarItem *pItem) {
    if (pItem && pItem->pSink) {
        pItem->pSink->lpVtbl->OnUpdate(pItem->pSink, TF_LBI_TEXT | TF_LBI_ICON);   // 자판 바뀌면 아이콘도 갱신
    }
}
