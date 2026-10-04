#include "langbar.h"
#include "compartment.h"
#include "jamotong.h"
#include "settings_ui.h"
#include "version.h"
#include "disk_version.h"   // 0.69.1: 업그레이드 전부터 떠 있던 프로세스면 About 에 알린다
#include "layout_icon.h"   // 자판마다 다른 아이콘 (2026-10-04)
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
        Jamotong_FlushForExternalSwitch(obj->pService);   // 조합 중 음절 확정 (실기 B-3: 클릭 전환은 확정을 안 했다)
        Config_RotateLayout(&obj->pService->config);
        LangBar_Update(obj);
        Compart_Publish(obj->pService);   // RFC-0012 Phase 1
    } else if (click == TF_LBI_CLK_RIGHT) {
        // 우클릭: 자체 컨텍스트 메뉴. BTN_BUTTON 스타일은 우클릭도 OnClick으로 오며
        // InitMenu(ITfMenu)는 호출되지 않는다(BTN_MENU 전용) — Mozc와 동일 방식.
        HMENU menu = CreatePopupMenu();
        if (menu) {
            AppendMenuW(menu, MF_STRING, 1, L"Settings...");
            AppendMenuW(menu, MF_STRING, 2, L"Next layout");
            // 무간섭(직접 입력) 모드 — 원격 데스크톱 등에서 모든 키를 앱에 그대로 통과.
            AppendMenuW(menu, MF_STRING | (obj->pService->passthrough ? MF_CHECKED : 0),
                        4, L"Pass-through (direct input) mode");
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
        JamotongLangBarItem *item = IMPL_LBI_BUTTON(pThis);
        DWORD ptFlags = (item->pService && item->pService->passthrough) ? 0x1 /*TF_LBMENUF_CHECKED*/ : 0;
        menu->lpVtbl->AddMenuItem(menu, 1, 0, NULL, NULL, L"Settings...", 11, NULL);
        menu->lpVtbl->AddMenuItem(menu, 2, 0, NULL, NULL, L"Next layout", 11, NULL);
        menu->lpVtbl->AddMenuItem(menu, 4, ptFlags, NULL, NULL, L"Pass-through (direct input) mode", 32, NULL);
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
        Jamotong_FlushForExternalSwitch(obj->pService);   // 조합 중 음절 확정 (실기 B-3: 클릭 전환은 확정을 안 했다)
        Config_RotateLayout(&obj->pService->config);   // 다음 자판
        LangBar_Update(obj);
        Compart_Publish(obj->pService);   // RFC-0012 Phase 1
    } else if (wID == 3) {
        extern HINSTANCE g_hInst;   // dllmain.c
        wchar_t note[320], msg[512];
        DiskVersion_StaleNote(g_hInst, L"this program", note, 320);
        _snwprintf(msg, 512, L"Jamotong IME  " JAMOTONG_VERSION L"\n\n"
            L"Pure-C Korean/Hangul IME (Text Services Framework).\n"
            L"Left-click the tray icon to cycle layouts;\n"
            L"right-click for this menu.%ls", note);
        msg[511] = L'\0';
        MessageBoxW(NULL, msg, L"About Jamotong IME", MB_OK | MB_TOPMOST | MB_SETFOREGROUND | MB_ICONINFORMATION);
    } else if (wID == 4) {
        // 무간섭(직접 입력) 모드 토글 — 원격 데스크톱 등. 상태는 레지스트리로 프로세스 간 공유.
        Jamotong_SetPassthrough(obj->pService, !obj->pService->passthrough);
        LangBar_Update(obj);
    }
}

static HRESULT STDMETHODCALLTYPE LBI_OnMenuSelect(ITfLangBarItemButton *pThis, UINT wID) {
    JamotongLangBarItem *obj = IMPL_LBI_BUTTON(pThis);
    if (!obj->pService) return S_OK;   // Deactivate 후 — 서비스 접근 금지(UAF 방어)
    ExecMenuCmd(obj, wID);
    return S_OK;
}

// 자판 아이콘은 layout_icon.c (2026-10-04 사용자 요청: 자판마다 다른 모양·색, 오른쪽 아래 언어, 밝은 회색 바탕)
static HICON CreateAbbrevIcon(const wchar_t *abbrev) { return LayoutIcon_Create(abbrev, 0); }

static HRESULT STDMETHODCALLTYPE LBI_GetIcon(ITfLangBarItemButton *pThis, HICON *phIcon) {
    JamotongLangBarItem *obj = IMPL_LBI_BUTTON(pThis);
    if (!phIcon) return E_INVALIDARG;
    if (!obj->pService) { *phIcon = NULL; return S_OK; }   // Deactivate 후 — UAF 방어
    if (obj->pService->passthrough) {   // 무간섭 모드: 자판 대신 "--" 표시
        *phIcon = CreateAbbrevIcon(L"--");
        return S_OK;
    }
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
