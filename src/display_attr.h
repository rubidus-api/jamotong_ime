#pragma once
// TSF display attribute for the in-progress Hangul composition (KLDP know-how): a composing
// range must carry GUID_PROP_ATTRIBUTE pointing at a registered display-attribute atom, and the
// TIP must implement ITfDisplayAttributeProvider so the app can resolve that atom into a
// TF_DISPLAYATTRIBUTE (here: a solid underline, TF_ATTR_INPUT). Without this the composing
// syllable shows no visual "in-progress" feedback.
//
// This MinGW's msctf.h exposes ITfDisplayAttributeInfo / IEnumTfDisplayAttributeInfo /
// TF_DISPLAYATTRIBUTE / GUID_PROP_ATTRIBUTE but NOT ITfDisplayAttributeProvider, so we declare
// the provider's minimal C vtable here (ABI-matched to msctf.h).

#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <msctf.h>

typedef struct ITfDisplayAttributeProvider ITfDisplayAttributeProvider;
typedef struct ITfDisplayAttributeProviderVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ITfDisplayAttributeProvider*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(ITfDisplayAttributeProvider*);
    ULONG   (STDMETHODCALLTYPE *Release)(ITfDisplayAttributeProvider*);
    HRESULT (STDMETHODCALLTYPE *EnumDisplayAttributeInfo)(ITfDisplayAttributeProvider*, IEnumTfDisplayAttributeInfo**);
    HRESULT (STDMETHODCALLTYPE *GetDisplayAttributeInfo)(ITfDisplayAttributeProvider*,
                                                         REFGUID,
                                                         ITfDisplayAttributeInfo**);
} ITfDisplayAttributeProviderVtbl;
struct ITfDisplayAttributeProvider { const ITfDisplayAttributeProviderVtbl *lpVtbl; };

// Standard TSF IID for ITfDisplayAttributeProvider.
extern const IID IID_ITfDisplayAttributeProvider_J;
// Jamotong's own composing display-attribute GUID.
extern const GUID GUID_JamotongComposingDA;

// The provider vtable, implemented on the TIP object (offset lpVtblDAP). Assigned in
// JamotongTextService_Create; QI-reachable via IID_ITfDisplayAttributeProvider_J.
extern const ITfDisplayAttributeProviderVtbl g_JamotongDAPVtbl;

// Register GUID_JamotongComposingDA with the category manager → a TfGuidAtom (0 on failure).
TfGuidAtom DA_RegisterAtom(ITfThreadMgr *threadMgr);
// Tag `range` as a composition with the given atom (sets GUID_PROP_ATTRIBUTE). No-op if atom 0.
void DA_ApplyToRange(ITfContext *context, TfEditCookie ec, ITfRange *range, TfGuidAtom atom);
