#pragma once
#include <windows.h>
#include <msctf.h>

#define TF_LBI_STYLE_BTN_BUTTON 0x00010000
#define TF_LBI_STYLE_SHOWNINTRAY 0x00000002
#define TF_LBI_TEXT 0x00000002

typedef enum {
    TF_LBI_CLK_RIGHT = 1,
    TF_LBI_CLK_LEFT = 2
} TfLBIClick;

typedef struct ITfLangBarItemButton ITfLangBarItemButton;

struct ITfLangBarItemButtonVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ITfLangBarItemButton*, REFIID, void**);
    ULONG (STDMETHODCALLTYPE *AddRef)(ITfLangBarItemButton*);
    ULONG (STDMETHODCALLTYPE *Release)(ITfLangBarItemButton*);
    HRESULT (STDMETHODCALLTYPE *GetInfo)(ITfLangBarItemButton*, TF_LANGBARITEMINFO*);
    HRESULT (STDMETHODCALLTYPE *GetStatus)(ITfLangBarItemButton*, DWORD*);
    HRESULT (STDMETHODCALLTYPE *Show)(ITfLangBarItemButton*, BOOL);
    HRESULT (STDMETHODCALLTYPE *GetTooltipString)(ITfLangBarItemButton*, BSTR*);
    HRESULT (STDMETHODCALLTYPE *OnClick)(ITfLangBarItemButton*, TfLBIClick, POINT, const RECT*);
    HRESULT (STDMETHODCALLTYPE *InitMenu)(ITfLangBarItemButton*, void*);
    HRESULT (STDMETHODCALLTYPE *OnMenuSelect)(ITfLangBarItemButton*, UINT);
    HRESULT (STDMETHODCALLTYPE *GetIcon)(ITfLangBarItemButton*, HICON*);
    HRESULT (STDMETHODCALLTYPE *GetText)(ITfLangBarItemButton*, BSTR*);
};

struct ITfLangBarItemButton {
    struct ITfLangBarItemButtonVtbl *lpVtbl;
};

// IID_ITfLangBarItemButton is missing from MinGW, declare as extern
extern const GUID IID_ITfLangBarItemButton;

typedef struct JamotongTextService JamotongTextService;

typedef struct JamotongLangBarItem {
    struct ITfLangBarItemButtonVtbl *lpVtblButton;
    ITfSourceVtbl *lpVtblSource;
    LONG refCount;
    JamotongTextService *pService;
    ITfLangBarItemSink *pSink;
    DWORD sinkCookie;
} JamotongLangBarItem;

JamotongLangBarItem* LangBar_Create(JamotongTextService *pService);
void LangBar_Update(JamotongLangBarItem *pItem);
