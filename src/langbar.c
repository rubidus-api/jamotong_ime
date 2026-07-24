#include "langbar.h"
#include "jamotong.h"
#include "settings_ui.h"
#include "version.h"
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

// 현재 자판 식별자(abbrev, 2~4글자)를 파란 배지에 흰 글씨로 실시간 렌더한 언어바/트레이 아이콘.
// - 글자를 2x2 격자로 배치해 작은 아이콘에서도 각 글자를 최대 크기로 → 3글자도 판독 가능.
//   1글자=꽉 채움, 2글자=가로 2칸, 3글자=위 2·아래 1(가운데), 4글자=2x2.
// - 글꼴은 '돋움'(Dotum): 작은 크기용 힌팅/내장비트맵이 좋은 시스템 한글 글꼴. 파일을 번들하지 않고
//   GDI로 시스템 설치본을 '이름 참조'만 하므로 폰트 재배포 라이선스가 발생하지 않음(→ COPYRIGHT.md).
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
