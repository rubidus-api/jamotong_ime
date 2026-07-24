# Building a Windows Korean IME with Nothing but WinAPI and C — A Field Manual

[한국어](winapi-c-ime-manual.ko.md) | **English**

*Last updated: 2026-07-24 (AkelPad R3 structural result and R4 same-binary probe)*

This document explains how to build a Korean input method (IME) for Windows from
scratch **in pure C (C23) and the Win32 API only** — no C++, no ATL/MFC, no frameworks —
including the trial-and-error record from the `jamotong` project and the final answers
we arrived at.

Audience: developers who know WinAPI and C but are new to COM/TSF.
Goal: to let you build another IME from the ground up using this document alone.

> **One-line conclusion (spoiler)**: Windows has TSF and IMM32, and the effective contract
> changes with the compatibility layer, edit control, and custom renderer. We found **no
> single composition or injection strategy that works in every application**. The product
> therefore combines a commit-only engine, per-application injection, and a separate
> preview. This is a reliability choice for the tested matrix, not proof that TSF
> composition is impossible in principle. Sections 8, 12.7, and 13 separate failures,
> evidence, and remaining hypotheses.

---

## Table of contents
0. [Prerequisites](#0-prerequisites)
0.5 [★Start Here — a minimal IME in 30 minutes](#05-start-here--build-an-ime-that-shows-up-and-eats-one-key)
1. [COM basics — implementing COM in C](#1-com-basics)
2. [The TSF landscape — TSF, IMM32, CUAS](#2-the-tsf-landscape)
3. [TIP skeleton — a minimal text service](#3-tip-skeleton)
4. [Registration](#4-registration)
5. [Key input handling](#5-key-input-handling)
6. [The Hangul automaton (composition logic)](#6-the-hangul-automaton)
7. [Putting text into the document — edit sessions](#7-edit-sessions)
8. [★The big lesson: compatibility-host limits and "commit-only"](#8-the-big-lesson)
9. [Extras: hanja, boundary keys, settings](#9-extras)
10. [Gotchas](#10-gotchas)
11. [Minimal IME checklist](#11-minimal-checklist)
12. [★Field lessons after commit-only](#12-field-lessons-after-commit-only)
13. [★★Text injection is not one thing — the per-app-class strategy](#13-text-injection-per-app-class)

Appendix A. [jamotong source map](#appendix-a-jamotong-source-map)
Appendix B. [★References (official docs)](#appendix-b-references)

---

## 0. Prerequisites

- **Compiler**: MinGW-w64 (`x86_64-w64-mingw32-gcc`, `i686-w64-mingw32-gcc`). No MSVC needed.
- **Output**: a `.dll` (a TSF IME is an in-proc COM server DLL). Build both 32- and 64-bit
  if you want to support 32-bit apps.
- **Key headers**: `<windows.h>`, `<msctf.h>` (TSF), `<initguid.h>` (GUID definitions), `<olectl.h>`.
- **Linking**: `-lole32 -loleaut32 -luuid` for COM/OLE Automation. The examples use
  `SysAllocStringLen` and `VariantClear`, which require `oleaut32`; most TSF interface
  declarations come from `msctf.h`, while interface GUIDs come through `-luuid`.
  `-municode` is not needed for a DLL.
- **Build shape**:
  `gcc -shared -o jamotong.dll *.c jamotong.def -lole32 -loleaut32 -luuid -lgdi32 ...`
  - Export `DllGetClassObject`, `DllCanUnloadNow`, `DllRegisterServer`,
    `DllUnregisterServer` through a `.def` file (on 32-bit, name decoration makes the
    `.def` effectively mandatory).

---

## 0.5 Start Here — Build an IME That Shows Up and Eats One Key

TSF documentation is vast and it is hard to know where to begin. So build **the smallest thing
that works** first. The code lives in `examples/minimal-tip/`; its local build commands and
tests are the verified reference. Do not mistake that for an automatic CI guarantee.

Goal: **appear in the Windows language list, and turn the `a` key into `ㄱ` in the document.**
That is all. Two files: `minimal.c` (~300 lines, including registration and COM plumbing)
and `minimal.def` (6 lines).

### Why this order

An IME is not "a program that receives keys and inserts text" — it is **a COM server that the
OS loads**. Three gates stand before you can insert anything.

```
① Be a COM server   → DllGetClassObject must hand out an object
② Be registered     → appear in the language list so the user can pick you
③ Receive keys      → advise a key sink, or no keys arrive
④ Only then insert  → and only inside an edit session
```

Miss any of ①–③ and **nothing happens — with no error message**. That silence is what hurts
beginners most. The minimal example exists to get ①–③ working before anything else.

### Step 1 — Build

```sh
cd examples/minimal-tip
make            # minimal.dll   (x64)
make win32      # minimal32.dll (x86)
```

Use the repository Makefile as the build command. It deliberately does **not** pass
`-municode`: that driver option selects a Unicode executable entry-point convention and is
irrelevant to a DLL whose exported COM entry points come from the `.def` file.

Why the `.def` file: on 32-bit, `DllRegisterServer` is exported decorated as
`_DllRegisterServer@0`. `regsvr32` looks for the undecorated name and will not find it.
The `.def` pins the name.

### Step 2 — Register

From an **elevated** command prompt:

```
regsvr32 minimal.dll
```

A dialog confirms success. Failures are almost always one of these:

| Symptom | Cause |
|---|---|
| "The module could not be loaded" | Bitness mismatch — 64-bit `regsvr32` on a 32-bit DLL |
| "Entry point not found" | Missing `.def`, or name mismatch |
| Succeeds but never appears | **Missing category registration** — `GUID_TFCAT_TIP_KEYBOARD` |

### Step 3 — Verify

Settings → Time & language → Language & region → Korean → Options → Keyboards should list
**"Minimal TIP"**. That means ① and ② passed. If not:

```
Does the CLSID exist?   HKCR\CLSID\{7B1F4C20-...}\InprocServer32
Is the path correct?    (a stale path fails silently)
```

### Step 4 — Type

Open Notepad, switch to "Minimal TIP", press `a`. You should get `ㄱ`.

**If nothing appears** — this is where the real work starts. See the app-class table in §2.2.
The tested Notepad setup is a known-good comparison host for this example; failure there
often points to registration or key-sink wiring, but the application name is not a
Windows-wide capability guarantee. The fastest check is
`OutputDebugStringW(L"activated\n")` in `Activate` plus DebugView (§12.6, diagnostics).

The minimal key handler must not swallow `a` when its only edit failed:

```c
if (w == 'A' && ctx) {
    HRESULT hr = InsertChar(ctx, service->clientId, L'ㄱ');
    *pfEaten = SUCCEEDED(hr);    /* failure: let the original key reach the app */
}
```

This teaching example uses the final HRESULT only as a **scoped policy for the tested
Notepad comparison host**; it has no later optional step that could hide a partial failure.
Even here, `S_OK` is call acceptance—not universal proof of a visible mutation. A
production route needs the postcondition-aware mutation-state rule in §5.1.

### Step 5 — Unregister

```
regsvr32 /u minimal.dll
```

**During development, always unregister before re-registering.** If the DLL moves but the
registry still points at the old path, already-loaded processes keep using the old DLL. The
symptom reads as "I fixed it but it is not fixed."

### What this example deliberately omits

| Omitted | Why | Section |
|---|---|---|
| Hangul composition (automaton) | keep the key→text path visible | §6 |
| Composition preview (underline) | keep the first example small; some tested compatibility hosts terminate it | §8 |
| Hangul/English toggle | state management stops it being minimal | §12.7.3 |
| Per-app-class strategies | Notepad working feels like "done". It is not. | §13 |
| Full setup-error propagation | the teaching sample omits some profile/category registration and key-sink `Advise` HRESULT propagation; production code must check and unwind every step | §3.2, §4 |

**Read §8 before adding preview.** A composition that works in Notepad can still be
terminated by a different text store after every edit session. That host-specific boundary
consumed twenty-plus rounds of this project's field work, so the minimal example deliberately
proves registration, key delivery, and editing before it adds composition lifetime.

---

## 1. COM basics

Everything in TSF is a COM interface. In C++ you would inherit; **C has no classes, so you
build the COM ABI by hand.** The substance of COM is actually very simple.

### 1.1 A COM interface = "a struct whose first member is a table of function pointers (vtbl)"

At runtime, C++'s `interface IFoo` looks like this:

```
object ─► [ vtbl pointer ] ─► [ QueryInterface, AddRef, Release, Method1, Method2, ... ]
          [ object data...  ]
```

- Every COM interface inherits **`IUnknown`** = the vtbl's **first three functions are
  always** `QueryInterface`, `AddRef`, `Release`.
- `msctf.h` already defines the vtbl struct types such as `ITfTextInputProcessorVtbl`.

### 1.2 Implementing one interface in C

Make the **vtbl pointer the first member** of your object struct — then an interface
pointer *is* the object pointer:

```c
typedef struct {
    ITfKeyEventSinkVtbl *lpVtbl;   // ★ must be the first member
    LONG refCount;
    /* ... your data ... */
} MyKeyEventSink;

static HRESULT STDMETHODCALLTYPE KES_QueryInterface(ITfKeyEventSink *This, REFIID riid, void **ppv){
    MyKeyEventSink *self = (MyKeyEventSink*)This;   // first member is the vtbl, so a cast suffices
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfKeyEventSink)) {
        *ppv = self; This->lpVtbl->AddRef(This); return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE KES_AddRef(ITfKeyEventSink *This){
    return InterlockedIncrement(&((MyKeyEventSink*)This)->refCount);
}
static ULONG STDMETHODCALLTYPE KES_Release(ITfKeyEventSink *This){
    MyKeyEventSink *self = (MyKeyEventSink*)This;
    LONG n = InterlockedDecrement(&self->refCount);
    if (n == 0) HeapFree(GetProcessHeap(), 0, self);
    return n;
}
static HRESULT STDMETHODCALLTYPE KES_OnKeyDown(ITfKeyEventSink *This, ITfContext *pic,
                                               WPARAM w, LPARAM l, BOOL *pfEaten){ /* ... */ }
/* ... remaining methods ... */

// vtbl instance (function order = interface definition order; get it wrong and you crash)
static ITfKeyEventSinkVtbl g_KES_Vtbl = {
    KES_QueryInterface, KES_AddRef, KES_Release,
    KES_OnSetFocus, KES_OnTestKeyDown, KES_OnTestKeyUp,
    KES_OnKeyDown, KES_OnKeyUp, KES_OnPreservedKey
};
// at object creation: obj->lpVtbl = &g_KES_Vtbl;
```

Getting the **calling convention (`STDMETHODCALLTYPE` = `__stdcall`) and the vtbl function
order exactly right** is the whole game.

### 1.3 One object, many interfaces (the IMPL_TO_OBJ trick)

A single IME object must implement `ITfTextInputProcessor`, `ITfKeyEventSink`, and more,
simultaneously. In C, give the object **one vtbl-pointer member per interface**, and
recover the object's base address from the member's address with `offsetof`:

```c
typedef struct JamotongTextService {
    ITfTextInputProcessorVtbl *lpVtblTIP;   // interface #1
    ITfKeyEventSinkVtbl       *lpVtblKES;   // interface #2
    /* ... */
    LONG refCount;
    /* data */
} JamotongTextService;

// any interface pointer → object pointer
#define IMPL_TO_OBJ(Name, pThis) \
    ((JamotongTextService*)((char*)(pThis) - offsetof(JamotongTextService, lpVtbl##Name)))

// e.g. inside a KeyEventSink method
JamotongTextService *obj = IMPL_TO_OBJ(KES, pThis);   // pThis points at &obj->lpVtblKES
```

- `QueryInterface` returns **the address of the vtbl member** matching the requested IID:
  `*ppv = &obj->lpVtblKES;` (the member's address, not the object's).
- All interfaces delegate AddRef/Release to the object's **single refCount** (usually the TIP's).

### 1.4 The class factory and the four DLL entry points

TSF calls `CoCreateInstance` with our CLSID → the OS calls our DLL's `DllGetClassObject`
to obtain a **class factory** (`IClassFactory`), whose `CreateInstance` creates our TIP object.

The four functions the DLL must export:

```c
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv);  // returns the factory
STDAPI DllCanUnloadNow(void);                 // S_OK when live-object count is 0
STDAPI DllRegisterServer(void);               // registry registration (regsvr32)
STDAPI DllUnregisterServer(void);             // unregistration (regsvr32 /u)
```

Implement `IClassFactory` in C the same way as §1.2
(`QueryInterface/AddRef/Release/CreateInstance/LockServer`). A global object counter
(`g_cRefDll`) drives `DllCanUnloadNow`.

> **Pitfall**: omit these four from the `.def` and `regsvr32` fails with
> "DllRegisterServer not found." On 32-bit, `__stdcall` decoration
> (`_DllRegisterServer@0`) makes exports mismatch without a `.def`.

---

## 2. The TSF landscape

### 2.1 Windows has two input systems

| System | Era | What we build |
|---|---|---|
| **TSF** (Text Services Framework) | modern (XP+) | ← **build this** (a TIP = Text Input Processor) |
| **IMM32** (Input Method Manager) | legacy (9x) | the old IME model; our tested Win11 deployment did not activate the side-loaded implementation (§8.3) |

- **CUAS** (Cicero Unaware Application Support): Windows can mediate TSF input for an
  application that consumes IMM32-style input. AkelPad's transitory context and direct
  `WM_IME_*` handling make that model a strong inference for the field result below.
  The trace cannot prove that every classic control takes the same path, nor can it name
  which compatibility component requested termination.

### 2.2 ★Apps come in three kinds, and each supports different things (the heart of this document)

| App class | What it is | TSF composition (underline preview) | Range editing (replace inserted text) | Plain insertion |
|---|---|:---:|:---:|:---:|
| **Native TSF** | fully TSF-aware editors and web content controls | ✅ | ✅ | ✅ |
| **Compatibility text store/CUAS path** | classic Win32 paths where TSF is mediated for an IMM32 application | ⚠️ retained or terminated depending on host | ⚠️ highly implementation-dependent | ⚠️ often works, with exceptions |
| **Terminals/custom renderers** | apps that draw their own text with no standard edit control | ⚠️ highly app-dependent | ⚠️ usually limited | ⚠️ may require real keys/messages |

- **Native TSF** is the most predictable class because it exposes the composition and
  range-editing contract directly.
- Do not collapse compatibility stores and custom renderers into one rule. The same TIP
  retained one composition in Notepad but was terminated after every key in AkelPad;
  terminals may require physical-key semantics instead of text insertion.
- Caret insertion covered many hosts, but was not a literal universal denominator (§13).
  Commit-only is a **fallback policy** that reduced failures in the tested matrix, not a
  guarantee stated by the Windows API.

> **Field note, 2026-07-23.** A separate standard-TSF lab retained composition correctly
> in Windows Notepad. In 64-bit AkelPad, each compatibility jamo was finalized separately
> instead of producing `가나다`. Every `StartComposition`, `SetText`, display-attribute,
> `SetSelection`, and edit-session call returned `S_OK`; immediately after each session
> closed, however, an extra `OnEndEdit` with no selection change was followed by
> `OnCompositionTerminated`. The AkelPad context also reported `TF_SS_TRANSITORY`.
> Together with AkelEdit's direct IMM32 composition-message handling, this is strong
> evidence of a CUAS/IMM compatibility path. It does not identify the terminator or prove
> that `SetSelection` alone is the trigger. A four-profile follow-up produced the same
> separated result in AkelPad and the correct result in Notepad for control,
> `TF_AE_NONE`, insert-before-compose, and no-explicit-selection. The extra edit changed
> text, `GUID_PROP_COMPOSING`, and `GUID_PROP_ATTRIBUTE`, but not `GUID_PROP_READING`.
> This rules out those three protocol choices as sufficient fixes. In the corrected
> metadata run, Reading-only retained one AkelPad composition while both profiles that
> included LANGID still terminated per key. R3 did **not** reproduce that positive control:
> in every structurally valid AkelPad profile, including two Reading-only runs, six new
> compositions were followed by six external terminations and no reuse. All four valid
> Notepad profiles reused one composition, although several profiles shared a Notepad
> process and therefore are not a clean inherited-environment control. No R3 visual result
> was collected, so this manual makes no claim about what appeared on screen. R3 added an
> immediate property read-back and extra synchronous trace writes, as well as new identities
> and deployment state; R2 versus R3 was therefore a confounded positive-control comparison.
> Section 12.7.8 describes R4, which holds one binary identity fixed and separates those
> observer effects with baseline bookends.

---

## 3. TIP skeleton

### 3.1 Required interfaces (minimal IME)

| Interface | Role | Required? |
|---|---|:---:|
| `ITfTextInputProcessor` | `Activate`/`Deactivate` entry point | ✅ |
| `ITfKeyEventSink` | key input | ✅ |
| (class factory `IClassFactory`) | object creation | ✅ |
| `ITfDisplayAttributeProvider` | composition underline color/style | only when the TIP supplies display attributes |
| `ITfCompositionSink` | notification when composition is terminated externally | optional; recommended for observing termination |
| `ITfFunctionProvider`+`ITfFnConfigure` | "Options" button → settings window | optional |
| `ITfThreadMgrEventSink`/`ITfTextEditSink` | document focus/edit tracking | optional |

> **A commit-only IME (§8) generally needs neither `ITfDisplayAttributeProvider` nor
> `ITfCompositionSink`** — it never composes. In the current jamotong product,
> `ITfCompositionSink` was removed, while `ITfDisplayAttributeProvider` remains exposed
> through `QueryInterface` and category registration as unused compatibility residue; it
> does not participate in the commit-only path. A composing TIP can pass `NULL` for the
> optional sink, but a valid sink is strongly recommended when you need to observe and
> clean up external termination (§7.3).

### 3.2 Activate / Deactivate

```c
HRESULT STDMETHODCALLTYPE
TIP_Activate(ITfTextInputProcessor *This, ITfThreadMgr *ptim,
             TfClientId tid) {
    JamotongTextService *obj = (JamotongTextService *)This;
    ITfKeystrokeMgr *ksm = NULL;
    HRESULT hr;

    if (ptim == NULL) return E_INVALIDARG;
    if (obj->threadMgr != NULL) return E_UNEXPECTED;
    obj->threadMgr = ptim;
    ptim->lpVtbl->AddRef(ptim);                /* retain for Deactivate */
    obj->clientId = tid;
    obj->keySinkAdvised = FALSE;

    hr = ptim->lpVtbl->QueryInterface(
        ptim, &IID_ITfKeystrokeMgr, (void **)&ksm);
    if (SUCCEEDED(hr) && ksm == NULL) hr = E_NOINTERFACE;
    if (SUCCEEDED(hr)) {
        ITfKeyEventSink *sink =
            (ITfKeyEventSink *)&obj->lpVtblKES;
        hr = ksm->lpVtbl->AdviseKeyEventSink(
            ksm, tid, sink, TRUE /* fForeground */);
        if (SUCCEEDED(hr)) obj->keySinkAdvised = TRUE;
    }
    if (ksm != NULL) ksm->lpVtbl->Release(ksm);

    if (FAILED(hr)) {
        /* No successful Advise remains. Undo every field published above. */
        ResetTransientComposition(obj);
        obj->clientId = 0;
        obj->keySinkAdvised = FALSE;
        obj->threadMgr->lpVtbl->Release(obj->threadMgr);
        obj->threadMgr = NULL;
        return hr;
    }
    return S_OK;
}
/* Deactivate: if keySinkAdvised, Unadvise and record its HRESULT;
   then clear the flag/client ID, Release threadMgr, NULL it, and reset
   all transient composition state even when an earlier cleanup call failed. */
```

`clientId` (TfClientId) is **your TIP's ID card** — pass it to nearly every TSF call,
including edit-session requests.

If activation adds more advised sinks or UI after the key sink, use one reverse-order
failure path: unadvise every step that succeeded, release every acquired interface, then
clear `clientId`, `threadMgr`, cookies, and transient state. Returning `S_OK` after a failed
`QueryInterface` or `AdviseKeyEventSink` creates an “active” TIP that can never receive keys.

---

## 4. Registration

An IME registers in **three layers**, all handled in `DllRegisterServer`.

### 4.1 COM server registration (registry)
```
HKCR\CLSID\{our-clsid}\InprocServer32  (default) = full DLL path,  ThreadingModel = Apartment
```

- Treat `HKCR` as the logical COM view and register with the matching tool:
  `System32\regsvr32` for the 64-bit DLL and `SysWOW64\regsvr32` for the 32-bit DLL.
  **Register per bitness** so each bitness of app can load its DLL. For inspection only,
  the redirected 32-bit machine-wide class view is normally visible under
  `HKLM\Software\Classes\Wow6432Node` (or the equivalent symbolic view); do not use that
  physical path as the registration contract.

### 4.2 TSF profile registration (attach the IME to a language)
```c
ITfInputProcessorProfiles *pp;
CoCreateInstance(&CLSID_TF_InputProcessorProfiles, ..., &IID_ITfInputProcessorProfiles, &pp);
// the modern way (required for the Win11 tray branding icon):
ITfInputProcessorProfileMgr *mgr;
pp->lpVtbl->QueryInterface(pp, &IID_ITfInputProcessorProfileMgr, (void**)&mgr);
mgr->lpVtbl->RegisterProfile(mgr, &CLSID_Ours, LANGID_KO /*0x0412*/, &GUID_Profile,
        desc, descLen, iconDllPath, iconDllPathLen,
        iconIndex /* ★negative = resource ID */, NULL, 0, TRUE /*enabled*/, 0);
```

- **Icon**: pass a **negative resource ID** as `iconIndex` (resource ID 100 → `-100`).
  Avoid `-1` (special value). Embed the icon in the DLL via `.rc` (`100 ICON "app.ico"`).
- For your icon to show in the Win11 tray input indicator you must use
  **`RegisterProfile` (ProfileMgr), not the old `AddLanguageProfile`** (measured).
- **Two different icons, don't confuse them.** This one is the **profile branding icon** —
  a *static* icon that identifies the IME in the language list and input switcher, set once
  at registration. The **live mode icon** that sits in the tray and changes with state
  (한/A, current layout) is a separate, run-time thing — the language-bar item in §12.4.

### 4.3 Category registration (capability declaration)
```c
/* Independently byte-checked SDK constants omitted by this MinGW msctf.h. */
static const GUID kTfcatTipcapImmersiveSupport = {
    0x13a016df, 0x560b, 0x46cd,
    { 0x94, 0x7a, 0x4c, 0x3a, 0xf1, 0xe0, 0xe3, 0x5d }
};
static const GUID kTfcatTipcapSystraySupport = {
    0x25504fb4, 0x7bab, 0x4bc1,
    { 0x9c, 0x69, 0xcf, 0x81, 0x89, 0x0f, 0x0e, 0xf5 }
};

static HRESULT SetCategories(BOOL add) {
    const GUID *categories[] = {
        &GUID_TFCAT_TIP_KEYBOARD,
        /* Omit this entry unless display attributes are supplied or retained. */
        &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
        &kTfcatTipcapImmersiveSupport,
        &kTfcatTipcapSystraySupport
    };
    ITfCategoryMgr *cat = NULL;
    HRESULT first = CoCreateInstance(
        &CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
        &IID_ITfCategoryMgr, (void **)&cat);
    if (FAILED(first)) return first;
    if (cat == NULL) return E_UNEXPECTED;

    for (size_t i = 0; i < sizeof categories / sizeof categories[0]; ++i) {
        HRESULT step = add
            ? cat->lpVtbl->RegisterCategory(
                  cat, &CLSID_Ours, categories[i], &CLSID_Ours)
            : cat->lpVtbl->UnregisterCategory(
                  cat, &CLSID_Ours, categories[i], &CLSID_Ours);
        if (FAILED(step) && SUCCEEDED(first)) first = step;
        if (add && FAILED(step)) break; /* wrapper below performs best-effort unwind */
    }
    cat->lpVtbl->Release(cat);
    return first;
}

HRESULT RegisterCategories(void) {
    HRESULT hr = SetCategories(TRUE);
    if (FAILED(hr)) (void)SetCategories(FALSE);
    return hr;
}
HRESULT UnregisterCategories(void) { return SetCategories(FALSE); }
```

jamotong registers the systray category alongside immersive support as a project-declared
compatibility capability. This records the project's tested deployment choice; it is
**not** a claim that every TIP must register that category. Use the same byte-checked
symbols for registration and unregistration; do not copy a missing header name in one
direction and a different local value in the other.

### 4.4 Install scripts
- `install.bat`: check admin rights → unblock files (Mark-of-the-Web) →
  `regsvr32 app.dll` (64-bit) → `SysWOW64\regsvr32 app32.dll` (32-bit). A sign-out or
  reboot makes it fully take effect.
- **DLL file-lock trap** (§10): a TSF DLL is mapped into many processes (`ctfmon` etc.),
  locking the file. To update: uninstall → **sign out/reboot** → overwrite → install.

---

## 5. Key input handling

### 5.1 The OnTestKeyDown / OnKeyDown double act (★important)

TSF delivers each key twice:
1. **`OnTestKeyDown`**: a **prediction only** — "will you eat this key?" Answer via
   `*pfEaten`. Keep ordinary key processing side-effect-free here. Section 13.2 records
   one narrow, field-tested pass-through flush exception; do not use that exception for
   normal FSM updates.
2. **`OnKeyDown`**: the real processing. **But it is only called for keys that
   `OnTestKeyDown` answered `*pfEaten=TRUE` for.**

→ **The eat-or-not logic of the two functions must match exactly.** Answer TRUE in
TestKeyDown but not eat in OnKeyDown and the key vanishes; the other way around and
OnKeyDown is never called.

```c
HRESULT OnTestKeyDown(..., WPARAM w, LPARAM l, BOOL *pfEaten){
    *pfEaten = FALSE;
    if (is_our_synthetic_input(GetMessageExtraInfo())) return S_OK;   // re-entry guard (§5.4)
    if (hangul_mode && is_jamo_key(w)) *pfEaten = TRUE;               // predict only
    if (composing && w == VK_BACK)     *pfEaten = TRUE;
    /* ... same order, same conditions as OnKeyDown ... */
    return S_OK;
}
```

Treat `OnKeyDown` as a small transaction. Compute the next automaton state in a copy,
attempt the document edit, and publish the copy only after the required postcondition is
proven. Also track whether no mutation occurred, the postcondition was observed, or a
mutation remains possible but unproved: an edit-session failure does **not** roll back a
successful earlier `SetText`, and `S_OK` alone does not prove visible mutation.

This is the required hardened contract, not a claim that every path in the current main
product already implements it. The migration status and remaining gaps are explicit in
§11.

```c
typedef enum MutationState {
    MUTATION_NONE,     /* no write was dispatched */
    MUTATION_DONE,     /* after-session or host-validated postcondition proved */
    MUTATION_UNKNOWN   /* accepted/partial mutation is possible but unproved */
} MutationState;

Fsm draft = obj->fsm;                         // private candidate state
FsmResult out = Fsm_ProcessKey(&draft, vk);
MutationState mutation = MUTATION_NONE;
HRESULT hr = ApplyOutput(obj, pic, &out, &mutation);
BOOL needs_document_change = OutputNeedsDocumentChange(&out);
BOOL proven = SUCCEEDED(hr) &&
              (needs_document_change
                   ? mutation == MUTATION_DONE
                   : mutation == MUTATION_NONE);

if (proven) {
    obj->fsm = draft;                         // publish exactly once
    *pfEaten = TRUE;
} else {
    ResetComposition(obj);                    // discard the unpublished candidate
    *pfEaten = (mutation == MUTATION_NONE) ? FALSE : TRUE;
    /* none: pass the untouched original key.
       done or unknown: eat it rather than risk applying one key twice. */
}
```

Do not run this in `OnTestKeyDown`: that callback predicts only. `mutation` must be updated
from observed postconditions, not inferred from the final HRESULT. If a host-native
message offers no reliable per-call proof, report `MUTATION_UNKNOWN`, not
`MUTATION_DONE` or `MUTATION_NONE`. The strict rule never publishes the trial FSM for
`UNKNOWN`. A same-lock read-back of the range returned by TSF can confirm range acceptance
and content at that instant, but it is not universal proof of durable, application-visible
host mutation. Eating an unproved key can avoid duplication, but it is failure
containment—not success.

### 5.2 VK → character mapping
Convert virtual key (VK) + Shift into the QWERTY character first (`'R'`→`'r'`,
Shift+`'2'`→`'@'`), then map that character to a jamo through the layout table. You can
use raw VK constants (0xBA etc.) if you prefer not to depend on `windows.h` names.

### 5.3 Keys you must pass through
- **Ctrl/Alt/Win combinations**: they are app shortcuts (Ctrl+C). Don't eat them
  (`*pfEaten=FALSE`). (**Shift is the exception** — needed for capitals and tense consonants.)
- **Modifier/lock keys** (Shift/Ctrl/Alt/Caps/Hangul/Hanja...): must not commit the composition.

### 5.4 Re-entry guard for our own synthetic input
A separately validated non-EDIT boundary route (§9.2) may use `SendInput`, and that key
comes back into `OnKeyDown`. Plant a **magic value** in `ki.dwExtraInfo` (for example
`0x4A414D4F`) when calling `SendInput`, and at the top of the handlers: if
`(ULONG_PTR)GetMessageExtraInfo() == magic`, return immediately. Do not treat this guard
as permission to synthesize modifier shortcuts or unvalidated boundary keys (§13.2–13.3).

---

## 6. The Hangul automaton

The IME's "composition logic" is a pure state machine, independent of TSF. TSF is only
**the pipe that puts characters into the document**; **what** to put is decided by this FSM.

- States: `EMPTY → CHO (initial) → CHO_JUNG (with vowel = 가) → CHO_JUNG_JONG (with final = 간)`.
- Each input yields `{commitChar, preeditChar}`:
  - `preeditChar`: the composed syllable currently in progress (e.g. `가`).
  - `commitChar`: the syllable finalized and sent to the document (0 = none).
- **Dokkaebibul (final-consonant carry-over)**: while composing `옥`, input `ㅜ` →
  `commit=오, preedit=구`. The commit can **differ from the previous preedit**
  (important in §7.2).
- Dubeolsik final-consonant decisions, double finals, and carry-over are implemented via
  Unicode Hangul syllable (0xAC00~) composition/decomposition arithmetic.

> The FSM is **easy to unit-test as native code** (no Windows needed). The portable
> `examples/standard-tsf-lab/tests/hangul2_test.c` suite currently reports **123 passing
> Hangul assertions** through that lab's `make test` target.

---

## 7. Edit sessions

### 7.1 Edits happen only inside an edit session
Modifying the document (`ITfContext`) requires an **edit cookie**, and the cookie is
only handed to you via the `RequestEditSession` callback:

```c
typedef struct EditOutcome {
    HRESULT request_hr;       /* return from RequestEditSession itself */
    HRESULT session_hr;       /* DoEditSession result returned in phrSession */
    HRESULT inner_hr;         /* result of our required document edit */
    BOOL callback_ran;
    MutationState mutation;   /* NONE, after-session/host-proven DONE, or UNKNOWN */
} EditOutcome;

static HRESULT STDMETHODCALLTYPE
ES_DoEditSession(ITfEditSession *iface, TfEditCookie ec) {
    EditSession *es = CONTAINING_RECORD(iface, EditSession, iface);
    es->out->callback_ran = TRUE;
    es->out->inner_hr =
        ApplyRequiredTextEdit(es, ec, &es->out->mutation);
    return es->out->inner_hr;
}

static BOOL RequestSyncWrite(ITfContext *ctx, TfClientId clientId,
                             EditSession *es, EditOutcome *out) {
    ZeroMemory(out, sizeof *out);
    out->request_hr = out->session_hr = out->inner_hr = E_UNEXPECTED;
    out->mutation = MUTATION_NONE;
    es->out = out;                 /* both remain alive through this sync call */

    out->request_hr = ctx->lpVtbl->RequestEditSession(
        ctx, clientId, &es->iface,
        TF_ES_SYNC | TF_ES_READWRITE, &out->session_hr);

    BOOL ok = SUCCEEDED(out->request_hr) &&
              out->callback_ran &&
              SUCCEEDED(out->session_hr) &&
              SUCCEEDED(out->inner_hr);
    es->out = NULL;                /* no late callback after a TF_ES_SYNC return */
    return ok;
}
```

- `EditSession` is a heap COM object with the vtable/reference-count plumbing from §1.2.
  Its create function copies the input buffer and `AddRef`s every COM pointer that it
  retains; its final `Release` releases those pointers and frees the object.
- `TF_ES_SYNC` means **grant the session synchronously or refuse it**. It does not promise
  that the callback always runs. Microsoft documents keystroke handling as a situation
  where a synchronous request can be expected; other notifications can return
  `TF_E_SYNCHRONOUS`.
- The method return (`request_hr`) and the out parameter (`session_hr`) are different
  verdicts. `request_hr == S_OK` means to inspect `session_hr`; for an established
  synchronous session, `session_hr` is the return from `DoEditSession`. In particular,
  `session_hr == TF_E_SYNCHRONOUS` means TSF could not grant the requested synchronous
  session.
- Keep `inner_hr`, `callback_ran`, and `mutation` too. This makes “request accepted,”
  “callback ran,” “the operation returned success,” and “the required postcondition was
  proved after the session or by a host-validated acknowledgement” separate facts.
  `RequestSyncWrite` reports only the first three; `S_OK` with `MUTATION_UNKNOWN` is not
  promoted to document success.
- `TF_ES_ASYNCDONTCARE` may run synchronously or later at the manager's discretion. It did
  not solve the composition-lifetime behavior observed in AkelPad.

### 7.2 ★The commit-only engine — required hardened form

**Insert only finalized syllables with `InsertTextAtSelection`. Never draw the syllable
being composed.** jamotong's current product uses commit-only output, but the
trial-state/outcome-propagation example below is the required production-hardening form
and standard-lab/example contract; the main product migration is still pending (§11).

```c
static HRESULT MoveCaretToEnd(ITfContext *ctx, TfEditCookie ec,
                              ITfRange *range) {
    TF_SELECTION selection;
    HRESULT hr = range->lpVtbl->Collapse(range, ec, TF_ANCHOR_END);
    if (FAILED(hr)) return hr;

    selection.range = range;
    selection.style.ase = TF_AE_NONE;
    selection.style.fInterimChar = FALSE;
    return ctx->lpVtbl->SetSelection(ctx, ec, 1, &selection);
}

static HRESULT ApplyRequiredTextEdit(EditSession *es, TfEditCookie ec,
                                     MutationState *mutation) {
    ITfInsertAtSelection *ins = NULL;
    ITfRange *inserted = NULL;
    HRESULT hr;

    if (es == NULL || mutation == NULL) return E_POINTER;
    *mutation = MUTATION_NONE;
    if (es->committed_len == 0) return S_OK; /* never insert the preedit */

    hr = es->ctx->lpVtbl->QueryInterface(
        es->ctx, &IID_ITfInsertAtSelection, (void **)&ins);
    if (SUCCEEDED(hr) && ins == NULL) hr = E_NOINTERFACE;
    if (FAILED(hr)) return hr;

    /*
     * Even a failed dispatch may leave a partial write, so become conservative
     * immediately before calling into the host. Do not promote to DONE merely
     * by re-reading the returned range under this same write lock.
     */
    *mutation = MUTATION_UNKNOWN;
    hr = ins->lpVtbl->InsertTextAtSelection(
        ins, ec, 0, es->committed, es->committed_len, &inserted);

    if (SUCCEEDED(hr)) {
        if (inserted == NULL)
            hr = E_UNEXPECTED; /* text may nevertheless have been partly applied */
        else
            hr = MoveCaretToEnd(es->ctx, ec, inserted);
    }

    if (inserted != NULL) inserted->lpVtbl->Release(inserted);
    ins->lpVtbl->Release(ins);
    return hr;
}
```

Treat the document edit and publication of the FSM state as one logical transaction.
Compute the next state in a copy, and publish it only after every required edit succeeds:

```c
/*
 * After the write session returns, open a new READ session to inspect durable
 * document state, or check an acknowledgement proven to correspond to
 * application-visible mutation in this host. Re-reading only the range returned
 * under the original write lock is not this proof.
 */
static HRESULT VerifyCommittedPostconditionAfterSession(
    CTextService *obj, ITfContext *pic,
    const WCHAR *expected, LONG expected_len, BOOL *proven);

BOOL HandleTextKey(CTextService *obj, ITfContext *pic, UINT vk,
                   BOOL *pfEaten) {
    HangulState trial = obj->hangul;            /* unpublished candidate */
    FsmResult result = Hangul_Step(&trial, vk);
    WCHAR committed[2] = { result.commitChar, L'\0' };
    EditSession *es = EditSession_Create(
        pic, committed, result.commitChar != 0 ? 1 : 0);
    EditOutcome out;

    if (es == NULL) {
        ResetTransientComposition(obj);           /* do not leave stale preedit/UI */
        *pfEaten = FALSE;                       /* no document mutation */
        return FALSE;
    }

    LONG committed_len = result.commitChar != 0 ? 1 : 0;
    BOOL session_ok = RequestSyncWrite(pic, obj->clientId, es, &out);
    es->iface.lpVtbl->Release(&es->iface);
    if (out.mutation == MUTATION_UNKNOWN) {
        BOOL proven = FALSE;
        HRESULT verify_hr = VerifyCommittedPostconditionAfterSession(
            obj, pic, committed, committed_len, &proven);
        if (verify_hr == S_OK && proven)
            out.mutation = MUTATION_DONE;
        else
            TraceUnprovenPostcondition(verify_hr); /* never trace document content */
    }

    BOOL no_write_needed = committed_len == 0;
    if (session_ok &&
        (no_write_needed || out.mutation == MUTATION_DONE)) {
        obj->hangul = trial;                    /* publish only after defined proof */
        *pfEaten = TRUE;
        return TRUE;
    }

    ResetTransientComposition(obj);
    *pfEaten = out.mutation == MUTATION_NONE ? FALSE : TRUE;
    TraceEditFailure(&out);                     /* HRESULTs/outcome enum, never content */
    return FALSE;
}
```

This code does not pretend that failure rolled the edit back. An edit session is not a
database transaction: if insertion succeeds and moving the caret then fails, the text can
remain. An immediate same-lock read-back of the returned range is useful evidence that TSF
accepted that content, but it does not establish durable or application-visible mutation
in every host. `MUTATION_DONE` here requires the defined after-session read or a
host-validated acknowledgement; otherwise the result stays `MUTATION_UNKNOWN`. Pass the
original key only for `MUTATION_NONE`; after `DONE`/`UNKNOWN`, eat it and reset/reconcile
private state rather than applying it twice.

- The composing syllable appears **when the next syllable begins or a boundary key
  (space/enter) forces a commit** — i.e., one beat late. That is commit-only's one price
  (no preview).
- **Beware the reversal bug**: if you don't move the caret to the **end** of the inserted
  text (`MoveCaretToEnd`), the next insert lands in front and `가나다` becomes `다나가`.
  `Collapse(TF_ANCHOR_END)` the range that `InsertTextAtSelection` returned, then
  `SetSelection`.

### 7.3 (Reference) How you *would* do composition preview — and why we didn't
Two protocol shapes, whose support must be tested in each target host:
- **TSF composition**: `ITfContextComposition::StartComposition` → update via
  `ITfRange::SetText` → apply display attributes (underline). In the tested AkelPad
  compatibility path, the composition was terminated right after each session (§8.1);
  Notepad retained it.
- **In-place replacement**: insert the composing syllable as plain text, then delete and
  re-insert as it changes (`ShiftStart`+`SetText`). Some tested restricted text stores did
  not honor the backward range edit, so text accumulated (`ㄱ가간...`) (§8.2).

`StartComposition` has two easy-to-miss success cases:

```c
static HRESULT TryStartComposition(ITfContextComposition *cc,
                                   TfEditCookie ecWrite, ITfRange *range,
                                   ITfCompositionSink *sink_or_null,
                                   ITfComposition **out) {
    if (cc == NULL || range == NULL || out == NULL) return E_INVALIDARG;
    *out = NULL;
    HRESULT hr = cc->lpVtbl->StartComposition(
        cc, ecWrite, range, sink_or_null, out);
    if (FAILED(hr)) return hr;                 /* API failure */
    if (!*out) return S_FALSE;                 /* wrapper: owner rejected */
    return S_OK;
}

ITfComposition *composition = NULL;
HRESULT start_hr = TryStartComposition(
    cc, ecWrite, range, sink_or_null, &composition);
if (start_hr == S_OK) {
    state->composition = composition;          /* transfer returned reference */
    composition = NULL;
    state->composition_started = TRUE;         /* publish only here */
} else if (start_hr == S_FALSE) {
    /* Owner rejection is not an API error. If text was inserted first, it was
       not automatically rolled back; use the required partial-edit policy. */
} else {
    /* API failure. No composition was published. */
}
```

`pSink` is **optional** and may be `NULL`. Supply a real `ITfCompositionSink` when you need
`OnCompositionTerminated` diagnostics or cleanup. A supplied sink must be a valid COM
object with working `QueryInterface`/`AddRef`/`Release`; TSF releases it when the
composition terminates. Separately, the context owner may reject the new composition: the
documented result is `S_OK` with `*ppComposition == NULL`. That is not a failing HRESULT,
but it still means “no composition was started,” so do not publish local composing state.
The non-NULL composition pointer is your returned COM reference. Centralize state cleanup
so that reference is released exactly once, whether explicit finalization or
`OnCompositionTerminated` clears the state; do not release it independently on both paths.

These host-specific failures led jamotong to a **commit-only product fallback**. They do
not prove that all CUAS paths reject either protocol.

---

## 8. The big lesson

This chapter is the reason this document exists — what jamotong learned over **20+ rounds
of on-device testing.**

### 8.1 In some compatibility hosts, TSF composition dies right after the session
- The lab exercised display attribute `TF_ATTR_INPUT`, category registration,
  insert-then-compose, with/without `SetSelection`, sync/async, and
  `ITfThreadMgrEventSink`/`ITfTextEditSink`. Those changes did not alter the fixed AkelPad
  x64 reproduction. That is a host observation, not a Windows-wide composition rule.
- Confirmed by logging: `StartComposition` succeeds (hr=0) and the composition text goes
  in, but **immediately after the edit session ends** an out-of-transaction edit and
  `OnCompositionTerminated` end the composition. The trace cannot identify whether that
  edit was initiated by the application, text store, or CUAS. AkelPad's transitory
  context and IMM32 handling make the compatibility-path model strong, but do not prove
  it (§12.7).
- **AkelPad x64 reproduction (2026-07-23)**: after one of our edit sessions completed
  successfully and closed, `OnEndEdit(selection_changed=0)` then
  `OnCompositionTerminated` repeated at the same tick for every key. That moves the
  investigation away from bitness, COM ABI, and immediate API failure to the
  **post-session host compatibility boundary**. `TF_SS_TRANSITORY` plus AkelEdit's
  `WM_IME_*`/`ImmGetCompositionStringW` implementation supports the CUAS/IMM inference.
- **The startup-order hypothesis did not fix AkelPad (2026-07-24).** Following the
  insert-first sequence seen in Mozilla's Korean-IME field record, changing selection
  style to `TF_AE_NONE`, and omitting explicit `SetSelection` all behaved exactly like
  the control. All four profiles still worked in Notepad. R2 then correlated
  Reading-only metadata with survival, but R3 did not reproduce that control; §12.7.8
  preserve both results and the confounders.
- ~~Passing a NULL `ITfCompositionSink` is invalid and proves the sink is mandatory.~~
  **Correction (2026-07-24):** the official `StartComposition` contract explicitly makes
  `pSink` optional. One earlier tested call with a NULL sink returned `E_INVALIDARG`, but
  that non-isolated observation does not prove the sink caused the error or override the
  API contract. A real sink remains the portable diagnostic choice because without it the
  TIP does not receive `OnCompositionTerminated`.
- A different edge case is documented: `StartComposition` can return `S_OK` and set
  `*ppComposition` to `NULL` when the context owner rejects the composition. Always check
  both (§7.3).

### 8.2 Some tested compatibility stores did not honor backward range editing
- We tried composition-less "in-place replacement" (delete the inserted `ㄱ`, write `가`)
  → it worked in the tested Notepad comparison host, while the tested restricted
  EDIT/text-store paths
  did not replace the old range and accumulated text like `ㄱ가간가나낟...`.
- Therefore do not assume that `ITfRange::ShiftStart` followed by `SetText` is honored by
  every text store merely because the calls succeed. Verify document/lifetime behavior in
  each supported host and provide a fallback.
- This result makes inline replacement unsuitable as jamotong's universal preview path.
  It does **not** prove that every application reached through CUAS lacks range editing.

### 8.3 Bypass via an IMM32 IME? — the tested Win11 deployment did not activate it
- "Then let's build a proper legacy (IMM32) IME" is the natural detour. We built the
  `.ime` (a DLL) and implemented `ImeInquire`, `ImeProcessKey`, `ImeToAsciiEx`. In the
  tested Windows 11 environment:
  - `ImmInstallIME` returned NULL with `GetLastError()==0`.
  - Registering directly via the registry makes the IME **appear in the settings list but
    grayed out (disabled)**, and it could not be activated.
  - Side discovery: `ImmInstallIME` reads the IME's **version resource (VERSIONINFO)** —
    without one you get `1813 ERROR_RESOURCE_TYPE_NOT_FOUND`.

This made IMM32 a dead end for this product and deployment. It is not evidence of a
documented, universal Windows policy that every third-party IMM32 package is rejected.

### 8.4 Product decision: finalized-only state, then route insertion per tested host
At this stage of development, the observed matrix suggested:

- composition lifetime was unreliable in the failing compatibility hosts;
- backward range replacement was unreliable in the tested restricted stores;
- the experimental IMM32 package did not activate on the tested Windows 11 machine; and
- caret insertion covered the then-tested applications.

The resulting product decision was **commit-only**: keep the preedit in IME-owned state and
put only finalized text in the document. ~~Insertion at the caret is universal, so one TSF
path works in every app.~~ That stronger conclusion was later disproved by a tested
AkelEdit-family control that returned `S_OK` without visibly applying the insertion (§13).
The current implementation keeps commit-only state but routes the finalized text through a
host-specific policy. That describes route selection, not completion of the lossless
mutation-outcome migration in §11.

The price: no inline preview (underline) of the syllable being composed. In Korean the
composing syllable is normally just the last character, so it is tolerable in practice.
If you need inline preview, validate TSF composition in each supported host and retain an
IME-owned overlay or commit-only fallback. The current class-name routing in §13 is a
product heuristic, not a general TSF-versus-CUAS detector. Destructive detection
("start a composition and watch it die") can mangle the first keystroke, so jamotong did
not use it as a capability probe.

> **Update (v0.12, see §13).** `InsertTextAtSelection` covered the tested comparison and
> terminal routes, while one EDIT-family host accepted it with `hr=0` and did not show the
> text. The product therefore uses TSF insert, `EM_REPLACESEL`, or key delivery according
> to its tested routing policy. Do not infer support for an untested application from the
> class label alone.

### 8.5 Chronicle of trial and error (summary)
1. Inline TSF composition → terminated on every key in the tested failing compatibility
   hosts and terminal scenarios. Initially overgeneralized as a "CUAS limitation."
2. `SendInput` unicode-append fallback → worked in terminals, but EDIT controls overwrote
   synthetic input as if it were composition.
3. Proper IMM32 IME → did not activate in the tested Win11 deployment (§8.3).
4. A failed composition experiment revealed that composition-less insertion could work in
   the tested compatibility host. The original explanation—“NULL sink is invalid”—was
   later corrected because the sink is optional.
5. In-place replacement preview → accumulated text in tested restricted stores (§8.2).
6. **Commit-only finalized** → consistent in the then-tested matrix.
7. A later EDIT-family result disproved universal TSF insertion → add the tested routing
   policy in §13.

---

## 9. Extras

### 9.1 Hanja conversion (under the commit-only model)
- The composing syllable is **not in the document** (commit-only), so hanja conversion is
  **insertion, not replacement**: Hanja key → look up the current FSM syllable in the
  dictionary → candidate window (our own popup) → on selection, **insert the chosen
  hanja** through the same host-specific `CommitText` routing used for ordinary finalized text
  (§13.1) + reset the FSM. Do not bypass that routing by assuming
  `InsertTextAtSelection` works in every host.
- Make the candidate window a popup that never steals focus, and route keys through the
  IME (while the window is up, eat all keys and forward them to its handler).
- Word-level conversion by reading already-typed text and replacing it is host-dependent.
  The TSF backward-range path works in the tested Notepad comparison; the tested EDIT-family
  route uses the selection-message ladder in §13.4. Selecting the word first reduces the
  operation to replacement of an existing selection, but still requires validation in the
  target host (§12.5).

### 9.2 Non-jamo boundary keys (space/enter/arrows)
When a non-jamo key arrives mid-composition, first flush the current syllable. The boundary
delivery then follows the tested host route:

- space is ordinary text, so commit **syllable + space in one operation** (§12.3);
- EDIT-family Enter/arrows use the verified `PostMessage` route (§13.3);
- a self-rendering terminal may require the separately verified `SendInput` route with the
  §5.4 marker; and
- Ctrl/Alt/Win shortcuts keep the original event rather than receiving a synthetic copy
  (§13.2).

There is no single boundary-key resend that is guaranteed for every host.

### 9.3 Opening the settings window (ITfFnConfigure)
- In the **tested Windows 11 environment**, the modern Settings app did not offer an
  “Options” button for this third-party TIP; `ITfFnConfigure` remained reachable through
  a classic dialog. Treat that as a measured UI/deployment result, not a universal Windows
  policy.
- Practical alternatives: a **hotkey** (e.g. Ctrl+Alt+K) that opens your settings window,
  or a **separate settings exe** that edits a shared config file
  (`%APPDATA%\App\config.ini`) which the IME loads on next activation.

### 9.4 Language-bar icon (ITfLangBarItemButton) — optional; barely visible on modern Windows.

---

## 10. Gotchas

- **vtbl order / calling convention**: one mistake = instant crash. Follow the interface
  definition order exactly.
- **Do not invent COM parameters missing from the documentation**: a hand-declared C
  `ITfDisplayAttributeProvider::GetDisplayAttributeInfo` takes only `This`, `REFGUID`,
  and `ITfDisplayAttributeInfo **`. One failed declaration added a fourth
  `TfGuidAtom *` parameter, crossing this provider lookup with the separate GUID-to-atom
  registration operation. That unbalances the x86 stack and violates the ABI contract on
  x64 as well.

  ```c
  static HRESULT STDMETHODCALLTYPE
  Dap_GetDisplayAttributeInfo(ITfDisplayAttributeProvider *This,
                              REFGUID guid,
                              ITfDisplayAttributeInfo **ppInfo);

  static const ITfDisplayAttributeProviderVtbl g_dap_vtbl = {
      Dap_QueryInterface, Dap_AddRef, Dap_Release,
      Dap_EnumDisplayAttributeInfo,
      Dap_GetDisplayAttributeInfo              /* exactly three C parameters */
  };
  ```

  Copy the method order from the SDK interface definition; never infer a vtable from prose.
  The provider IID is
  `{FEE47777-163C-4769-996A-6E9C50AD8F54}`. Reusing the
  `ITfDisplayAttributeMgr` IID under the provider's variable name is a name/value-crossing
  bug: the declaration can look plausible while `QueryInterface` asks for a different
  interface.
- **`StartComposition`'s sink is optional.** Pass `NULL` if you do not need termination
  callbacks; pass a real sink when you do. In both cases initialize
  `ITfComposition *result = NULL`, then require both a successful HRESULT and a non-NULL
  result. `S_OK + NULL` is the documented context-owner rejection case.
- **Reversed input**: move the caret to the end after inserting (`MoveCaretToEnd`), or
  `가나` becomes `나가`.
- **OnTestKeyDown ↔ OnKeyDown condition mismatch**: lost keys / handler never called.
- **Synthetic-input re-entry**: plant `dwExtraInfo` magic in `SendInput` and filter at entry.
- **DLL file locking**: a TSF DLL is mapped into many processes. Update = uninstall →
  sign out/reboot → overwrite → install. If you changed the log format but see the old
  format, the old DLL is still loaded (deployment failed).
- **Both 32- and 64-bit**: app bitness must match DLL bitness. Register each.
- **`RegisterProfile` vs `AddLanguageProfile`**: only the former gets your icon into the
  Win11 tray.
- **Negative icon index convention**: `-resourceID`. Avoid `-1` (special value).
- **Registry redirection**: `HKCR` is a logical, bitness-sensitive view. Register with the
  matching `regsvr32`; inspect the 32-bit machine class view under
  `HKLM\Software\Classes\Wow6432Node` only when diagnosing redirection.
- **Unicode/locale**: keep sources and strings UTF-16 (`wchar_t`, `-W` APIs). Non-ASCII in
  `.rc` needs windres codepage care (safest: keep resources ASCII).
- **`TF_ES_SYNC` is a request, not a promise that the callback ran**: the method can return
  `S_OK` while `phrSession` is `TF_E_SYNCHRONOUS`. If established, `phrSession` receives
  `DoEditSession`'s return value. Keep `request_hr`, `session_hr`, `inner_hr`, and
  `callback_ran` separate, and edit only with the callback's `ec` (§7.1).
- **`S_OK` does not guarantee that a composition survived**: the host may call
  `OnCompositionTerminated` during or just after the session. Correlate request/session
  HRESULTs with a transaction generation, termination epoch, and the post-session
  composition state.
- **Every returned `VARIANT` needs lifecycle handling**: call `VariantInit` before
  `GetValue` and `VariantClear` on every exit after the call. `VT_EMPTY` is data, not
  necessarily Boolean false; its meaning depends on the API (§12.7.3).
- **wide-scanf conversion specifiers**: per the C standard, `%[`/`%c`/`%s` in `swscanf`
  target **narrow (char) buffers** unless prefixed with `l`. Only MSVCRT treats them as
  wide (an MS quirk), so it happens to work on Windows — write `%l[`/`%lc`/`%ls`
  explicitly (identical behavior on both CRTs, and portable).
- **Window-class ownership**: register classes with the **DLL's hInstance** (registering
  with the EXE instance mismatches owner and WndProc), and **`UnregisterClassW` on dynamic
  unload** (MSDN: DLL classes are not auto-unregistered). Otherwise a reloaded DLL crashes
  through a stale WndProc.
- **`TF_IAS_NOQUERY` may not fill ppRange**: if you need replacement, get the selection
  range with `TF_IAS_QUERYONLY`, then `ShiftStart`+`SetText` (avoids a NULL dereference).
- **Lock coverage**: if `OnTestKeyDown` reads config/layout data, it needs **the same lock**
  as OnKeyDown. If the settings thread frees layout resources mid-keystroke, that's a UAF.

---

## 11. Minimal checklist

Minimum parts for a commit-only Korean TSF IME:

- [ ] COM: `IClassFactory`, `DllGetClassObject/CanUnloadNow/RegisterServer/UnregisterServer` + `.def`
- [ ] `ITfTextInputProcessor` (call `AdviseKeyEventSink` in Activate)
- [ ] `ITfKeyEventSink` (OnTestKeyDown/OnKeyDown conditions in lockstep; reset FSM in OnSetFocus)
- [ ] Hangul FSM (dubeolsik composition → `{commit, preedit}`)
- [ ] Edit session (`ITfEditSession`) + `InsertTextAtSelection` for the minimal/validated TSF route
      (finalized syllables only; move caret to end; add tested host routing per §13)
- [ ] Lossless failure contract: compute the FSM in a trial copy, propagate document
      mutation as `NONE`/`DONE`/`UNKNOWN`, and publish/eat/pass only from that outcome
- [ ] Boundary delivery policy (space in the same text operation; tested EDIT/terminal
      routes for control keys; magic marker on any `SendInput`)
- [ ] Registration: COM CLSID + `RegisterProfile` + `RegisterCategory(TIP_KEYBOARD)` + install/uninstall.bat
- [ ] Build & register both 32- and 64-bit

**Nice to have**: hanja candidate window, settings (hotkey / separate exe), tray branding icon.
**Not needed if commit-only**: `ITfCompositionSink`, `ITfDisplayAttributeProvider`,
composition/underline logic (general rule; see the current compatibility residue in §3.1).

> **Main-product migration status (2026-07-24):** the trial-FSM, mutation-outcome, and
> publish/eat-after-success examples in §§5.1, 7.2, and 13 are the required hardening
> contract, exercised by the standard-lab/example work. They are not yet a guarantee of
> every current `src/` path: `CommitText` and `OutputResult` discard outcomes, the main key
> path mutates the FSM before document success, and `EditCtl_ReplaceSelection` cannot turn
> synchronous `SendMessage` dispatch into proof of document mutation. Keep this checklist
> item open until outcomes propagate through the product and failure-injection tests pass.

> A language-bar mode icon needs its own release gate: exact SDK
> `ITfLangBarItemButton : ITfLangBarItem` vtable order, separate `ITfSource`, the inherited
> item prefix passed to `AddItem`, one-release Advise/Unadvise sink ownership, correct
> `TF_LBI_ICON=0x1` and `TF_LBI_STYLE_SHOWNINTRAY=0x2`, and `GetIcon`'s
> `S_OK + NULL` no-icon result.

---

## 12. Field lessons after commit-only

The story did not end with commit-only (§8). While adding preview, hanja and the tray
icon, compatibility hosts exposed three more failures. Each lesson below is
"problem → evidence → product fix," verified in the named support matrix (v0.9–v0.11)
without claiming the same mechanism for every CUAS path.

### 12.1 Composition preview lives *outside* the document — a floating overlay
- **Problem**: the price of commit-only is that the syllable being composed is invisible.
- **Fix**: don't touch the document — **draw the composing syllable yourself in a small
  translucent chip window at the caret** (the same industry-standard fallback as the
  classic IMM32 "default composition window"). This removes document-composition lifetime
  from the preview path; caret discovery and text commit remain host-dependent.
- **Implementation notes**:
  - Style: `WS_POPUP` + `WS_EX_LAYERED|NOACTIVATE|TOPMOST|TOOLWINDOW|TRANSPARENT` (click-through).
  - Translucency via **uniform alpha** (`SetLayeredWindowAttributes`) + normal WM_PAINT.
    Per-pixel alpha (`UpdateLayeredWindow`) makes GDI text vanish — GDI doesn't write alpha.
  - Caret-rect fallback chain: `GetActiveView`→`GetTextExt` **inside the same edit session**
    as the commit insert → on failure `GetGUIThreadInfo` (system caret; old EDIT controls
    and many terminals set it) → if both fail, just skip the preview.
  - Create lazily on the input thread; hide on focus change, destroy on Deactivate.

### 12.2 ★A tested compatibility store's GetTextExt returned a stale successful rect
- **Problem**: the chip overlaps the just-committed character, or trails the caret by
  exactly one keystroke.
- **Observed mechanism**: in the affected compatibility-host run, calling `GetTextExt`
  right after the insert — in the same session — returned `S_OK` with the pre-insert
  coordinates. A failure-only fallback cannot catch a stale success. The tested comparison
  control advanced the rect immediately; this does not classify either host as universally
  TSF-native or establish that other hosts have the same timing.
- **Final fix — staleness detection (accumulating compensation)**:
  1. Remember the **raw rect** every time you draw the chip.
  2. "A commit happened this event, **and** the rect equals the previous raw rect" =
     stale → draw shifted right by one full-width advance (~line height), **accumulated**.
  3. Reset the accumulator when the rect actually moves.
  - Accumulation also covers fast typing (rect frozen across several keys). In the tested
    comparison control the rect moved, so the compensation did not fire.
  - Pitfall: store the **raw** rect for comparison — storing the compensated one breaks
    the next comparison.

### 12.3 ★The vanishing-last-syllable incident — racing your own synthetic key
- **Problem**: in the affected compatibility-host runs the **last syllable of a word
  intermittently disappears**
  (the 국 of 대한민국 one day, the 라 of 가나다라 the next). Content-independent,
  timing-dependent.
- **Diagnosis**: with logging in place, **every `InsertTextAtSelection` returned S_OK**.
  That proves call acceptance, not visible delivery. The loss always coincided with
  "flush (insert last syllable) + `SendInput` boundary-key resend".
- **Strong field inference**: the synthetic boundary key (system input queue) raced the
  compatibility path's result-character delivery inside the app. The correlation and the
  one-channel fix support this model, but the log did not expose the internal scheduler.
- **Final fix**: **space is a character.** When space arrives mid-composition, don't
  resend it — insert **"syllable + space" in one edit session** (one delivery channel,
  nothing left to race). Enter/arrows remain resent as real keys (they're control keys:
  terminals, auto-indent, etc. need native handling).
- **Lesson for this support matrix**: avoid mixing edit-session insertion and `SendInput`
  in one key event when ordering matters. Successful API returns do not establish an
  application-visible order between those delivery paths.

### 12.4 The tray input-indicator mode icon (MS IME's 한/A)

This is the small icon in the tray that shows the IME's current state and updates live. It
is **not** the profile branding icon (§4.2) — it is a run-time *language-bar item* your TIP
adds while active. The mechanism looks intimidating in the docs, but it's five small pieces.

Use the Windows SDK `<ctfutb.h>` declaration as the ABI oracle. The current
[Microsoft SDK IDL](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/ctfutb.idl)
declares `ITfLangBarItemButton : ITfLangBarItem`, so its C vtable order is
`IUnknown`, the four item methods, then the five button methods. The Learn interface page
currently says “inherits from IUnknown”; that summary conflicts with the SDK IDL and is not
safe as a hand-written-vtable source. If the tested MinGW header lacks this interface,
copy the complete SDK order above, byte-check IID
`{28C7F1D0-DE25-11D2-AFDD-00105A2799B5}`, and compile both x86 and x64.

```c
/* Prefer <ctfutb.h>. This is the exact fallback shape for a tested MinGW
 * that has ITfLangBarItem in msctf.h but lacks the button declaration. */
#ifndef __ITfLangBarItemButton_INTERFACE_DEFINED__
typedef struct ITfMenu ITfMenu;
typedef enum J_TfLBIClick {
    J_TF_LBI_CLK_RIGHT = 1,
    J_TF_LBI_CLK_LEFT = 2
} J_TfLBIClick;
typedef struct ITfLangBarItemButton ITfLangBarItemButton;
typedef struct ITfLangBarItemButtonVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(
        ITfLangBarItemButton *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ITfLangBarItemButton *);
    ULONG (STDMETHODCALLTYPE *Release)(ITfLangBarItemButton *);
    HRESULT (STDMETHODCALLTYPE *GetInfo)(
        ITfLangBarItemButton *, TF_LANGBARITEMINFO *);
    HRESULT (STDMETHODCALLTYPE *GetStatus)(
        ITfLangBarItemButton *, DWORD *);
    HRESULT (STDMETHODCALLTYPE *Show)(
        ITfLangBarItemButton *, BOOL);
    HRESULT (STDMETHODCALLTYPE *GetTooltipString)(
        ITfLangBarItemButton *, BSTR *);
    HRESULT (STDMETHODCALLTYPE *OnClick)(
        ITfLangBarItemButton *, J_TfLBIClick, POINT, const RECT *);
    HRESULT (STDMETHODCALLTYPE *InitMenu)(
        ITfLangBarItemButton *, ITfMenu *);
    HRESULT (STDMETHODCALLTYPE *OnMenuSelect)(
        ITfLangBarItemButton *, UINT);
    HRESULT (STDMETHODCALLTYPE *GetIcon)(
        ITfLangBarItemButton *, HICON *);
    HRESULT (STDMETHODCALLTYPE *GetText)(
        ITfLangBarItemButton *, BSTR *);
} ITfLangBarItemButtonVtbl;
struct ITfLangBarItemButton {
    const ITfLangBarItemButtonVtbl *lpVtbl;
};
static const IID IID_ITfLangBarItemButton_J = {
    0x28c7f1d0, 0xde25, 0x11d2,
    { 0xaf, 0xdd, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 }
};
#define IID_LBI_BUTTON IID_ITfLangBarItemButton_J
#else
#define IID_LBI_BUTTON IID_ITfLangBarItemButton
#endif
```

**The recipe — what actually makes the icon appear:**

1. **Implement one inherited button plus a separate `ITfSource`.** The button must be the
   first interface field so its `ITfLangBarItem` vtable prefix is also the pointer passed
   to `AddItem`. `QueryInterface` returns that same button address for
   `IUnknown`/`ITfLangBarItem`/`ITfLangBarItemButton`; only `ITfSource` has a separate
   address. All wrappers recover the same object and share one reference count.

   ```c
   typedef struct ModeLangBarItem {
       ITfLangBarItemButton button;     /* first: includes ITfLangBarItem prefix */
       ITfSource source;                /* separate sink-connection interface */
       LONG refCount;
       BOOL active;
       ITfLangBarItemSink *sink;        /* owned QI reference */
       DWORD sinkCookie;
   } ModeLangBarItem;

   static HRESULT ModeItem_QI(ModeLangBarItem *self, REFIID riid, void **ppv) {
       if (ppv == NULL) return E_POINTER;
       *ppv = NULL;
       if (IsEqualIID(riid, &IID_IUnknown) ||
           IsEqualIID(riid, &IID_ITfLangBarItem) ||
           IsEqualIID(riid, &IID_LBI_BUTTON))
           *ppv = &self->button;       /* one inherited/canonical pointer */
       else if (IsEqualIID(riid, &IID_ITfSource))
           *ppv = &self->source;
       else
           return E_NOINTERFACE;
       InterlockedIncrement(&self->refCount);
       return S_OK;
   }

   static HRESULT ModeItem_AdviseSink(
       ModeLangBarItem *self, REFIID riid, IUnknown *unknown, DWORD *cookie) {
       ITfLangBarItemSink *sink = NULL;
       HRESULT hr;
       if (unknown == NULL || cookie == NULL) return E_INVALIDARG;
       *cookie = TF_INVALID_COOKIE;
       if (!IsEqualIID(riid, &IID_ITfLangBarItemSink))
           return CONNECT_E_CANNOTCONNECT;
       if (self->sink != NULL) return CONNECT_E_ADVISELIMIT;
       hr = unknown->lpVtbl->QueryInterface(
           unknown, &IID_ITfLangBarItemSink, (void **)&sink);
       if (SUCCEEDED(hr) && sink == NULL) hr = E_NOINTERFACE;
       if (FAILED(hr)) return hr;
       self->sink = sink;             /* release on Unadvise/final destruction */
       self->sinkCookie = 1;
       *cookie = self->sinkCookie;
       return S_OK;
   }
   ```
   The `ITfSource::UnadviseSink` wrapper accepts only the live cookie, releases `sink`
   exactly once, clears it, and resets the cookie. Final destruction performs the same
   cleanup if the manager did not unadvise first.
2. **Add it when the TIP activates, remove it when it deactivates.** In `ActivateEx`, get
   `ITfLangBarItemMgr` from the thread manager and `AddItem`; in `Deactivate`, `RemoveItem`.
   ```c
   ITfLangBarItemMgr *mgr = NULL;
   HRESULT hr;

   obj->langBarItemAdded = FALSE;
   if (modeItem == NULL) {
       hr = E_POINTER;
   } else {
       hr = ptim->lpVtbl->QueryInterface(
           ptim, &IID_ITfLangBarItemMgr, (void **)&mgr);
       if (SUCCEEDED(hr) && mgr == NULL) hr = E_NOINTERFACE;
   }
   if (SUCCEEDED(hr)) {
       hr = mgr->lpVtbl->AddItem(
           mgr, (ITfLangBarItem *)&modeItem->button); /* inherited item prefix */
   }
   if (mgr != NULL) mgr->lpVtbl->Release(mgr);

   if (FAILED(hr)) {
       ActivationUnwind(obj); /* reverse successful advises/AddRefs; reset fields */
       return hr;
   }
   obj->langBarItemAdded = TRUE;
   ```
   `ActivationUnwind` removes only items whose “added” flag is set, unadvises every sink
   that succeeded, releases the retained thread manager, and clears activation/transient
   state. `Deactivate` uses the same flags and cleanup order. Never return activation
   success after `QueryInterface` or `AddItem` failed.
3. **In `GetInfo`, set exactly two things that matter** — the item's identity and style:
   ```c
   pInfo->guidItem = GUID_LBI_INPUTMODE;                          // ★must be this GUID
   pInfo->dwStyle  = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
   pInfo->clsidService = CLSID_Ours;  lstrcpyW(pInfo->szDescription, L"...");
   ```
   - `GUID_LBI_INPUTMODE = {2C77A81E-41CC-4178-A3A7-5F8A987568E6}` is not in the tested
     MinGW headers, so define it from independently checked bytes. In the tested/product
     Win8+ design, this identity was decisive for modern tray visibility; a different item
     GUID could leave `AddItem` successful but show only in the legacy language bar.
   - `TF_LBI_STYLE_SHOWNINTRAY` has value `0x00000002`. It is a legacy style request and
     does **not** by itself force visibility in the modern Win8+ tray.
4. **In `GetIcon`, validate and initialize the out pointer, then return the current
   `HICON`.** The shell takes ownership and destroys it, so hand back a fresh icon each
   call (for example, draw the current layout's abbreviation).

   ```c
   static HRESULT STDMETHODCALLTYPE
   Button_GetIcon(ITfLangBarItemButton *This, HICON *phIcon) {
       ModeLangBarItem *self = ButtonToModeItem(This);
       if (phIcon == NULL) return E_INVALIDARG;
       *phIcon = NULL;
       if (!self->active) return S_OK;          /* documented no-icon shape */
       *phIcon = CreateFreshModeIcon(self);     /* shell owns a non-NULL result */
       return S_OK;                             /* NULL is also documented success */
   }
   ```

   `S_FALSE` is not the documented “no icon” result for this method; use `S_OK` with
   `*phIcon == NULL`.
5. **When state changes, tell the shell to re-query** via the owned sink installed through
   the separate `ITfSource::AdviseSink` interface:
   `sink->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT)`. `TF_LBI_ICON` is the update
   flag `0x00000001`; do not confuse it with the `0x00000002` style flag above. That
   `OnUpdate` call is the entire “refresh the icon” path.

That's the whole thing: one item, add/remove around activation, the right GUID+style, an
icon on demand, and an OnUpdate when it changes.

**Right-click menu (optional).** With `TF_LBI_STYLE_BTN_BUTTON`, **right-clicks arrive as
`OnClick(TF_LBI_CLK_RIGHT)`** — you build the menu yourself. (The `InitMenu`/`ITfMenu` COM
path is `BTN_MENU`-only, and `BTN_MENU` also opens the menu on *left*-click, which kills the
"left-click = toggle" UX. So use `BTN_BUTTON` and handle right-click yourself.)
```
OnClick(RIGHT, point):
    CreatePopupMenu + InsertMenuItem
    clamp point.x to the monitor work area
    cmd = TrackPopupMenu(TPM_NONOTIFY|TPM_RETURNCMD|TPM_LEFTBUTTON,
                         point, owner=GetFocus())   // ★owner required, NONOTIFY required
    dispatch cmd
```

**Icon guidelines (MS requirement)**: **black & white only** (white glyph with a dark
outline / dark badge), 16px base with DPI scaling.

### 12.5 Hanja conversion on selected text — "replacement" without range editing
- **Insight**: with an active selection, `InsertTextAtSelection` **replaces the
  selection** when the host honors that TSF operation.
- **Result**: the "select text → press Hanja" UX gives you **word-level hanja conversion
  through one operation in the tested comparison path. EDIT-family hosts in jamotong's matrix
  use the explicit selection-message ladder in §13.4. Do not claim coverage for an
  untested host from either result.
- Read the selection with `GetSelection`→`GetText` in a READ session. The candidate
  window doesn't steal focus (NOACTIVATE), so the selection survives; cancelling touches
  nothing, so the selection also survives.

### 12.6 Diagnostics: logging beats guessing
- Dead compositions (§8), vanishing syllables (§12.3), and lagging chips (§12.2) can all
  be diagnosed with a **structural log in %TEMP%**. You do not need entered characters,
  key values, document contents, or pointer values to diagnose object lifetime.
- **Minimum composition fields**: monotonic sequence, PID/TID, context generation,
  transaction id, current phase, each HRESULT, input/result lengths, composition-alive
  flag, and termination epoch. Put `StartComposition`, `SetText`, `SetSelection`,
  `ShiftStart`, and `EndComposition` beside `OnCompositionTerminated` and `OnEndEdit`
  on the same sequence axis.
- **Record the state after `S_OK`.** A host callback can terminate the composition while
  the API itself reports success. `composition=0` or an incremented termination epoch
  after the session disproves a success verdict based only on HRESULT.
- Use `ITfEditRecord::GetTextAndPropertyUpdates` in `OnEndEdit` to ask separately whether
  text, `GUID_PROP_COMPOSING`, `GUID_PROP_ATTRIBUTE`, `GUID_PROP_LANGID`, or
  `GUID_PROP_READING` has a changed range. Record only presence; never read the range text
  or property value. Also record `ITfContext::InWriteSession` so a post-session callback
  can be distinguished from work still owned by the TIP.
- **Change one variable per comparison.** The first AkelPad suite used four independent
  CLSID/profiles: (0) control, (1) only `TF_AE_NONE`, (2) only insert-before-compose,
  and (3) only omission of explicit `SetSelection`. Combining them in one DLL could make
  input work without revealing why. Conversely, if binary identity is itself a confounder,
  use one binary with a validated run-time mode as R4 does. Match the experimental unit to
  the causal question.
- **First A/B result (2026-07-24):** all four profiles produced separated jamo in AkelPad
  and proper syllables in Notepad. The post-session text/composing/display-attribute
  changes and termination order remain valid. After the second field run, however, we
  found that `changed_langid` and `changed_reading` queried manually mistranscribed GUIDs.
  Discard the old “reading unchanged” claim. The independent findings that selection
  style, explicit selection, and startup order are not sufficient remain valid.
- **Second A/B result (2026-07-24):** it did not test the effect of real LANGID/READING.
  Every metadata-application event returned `E_FAIL`, and an independent audit also found
  that both copied property GUIDs were wrong. That schema did not separate `GetProperty`
  from `SetValue`, so it cannot identify the failing subcall or prove that the wrong GUID
  directly caused the failure. Notepad's mixed Latin/jamo output came from treating a
  failure after successful `SetText` as rollback and then passing the original key too
  (§12.7.7).
- **Corrected R2 field result (2026-07-24):** all eight cells passed structural
  validation. AkelPad Reading-only created one composition, reused it five times, had
  zero per-key external terminations, and produced the correct syllables. Control,
  LANGID-only, and LANGID→Reading recreated and externally lost the composition on all
  six keys, visibly separating jamo. All four Notepad profiles were correct. The actual
  property operations succeeded over nonempty ranges, so this run applied the metadata
  hypothesis. LANGID presence, property order, and repeated writes were still confounded;
  R3 attempted to isolate them but lost its positive control (§12.7.8).
- **R3 structural result (2026-07-24):** all eight intended matrix cells passed the
  property, range, and session checks, and one additional valid AkelPad Reading-only
  repeat passed them too. The positive control did not reproduce: every AkelPad profile
  created six compositions, reused none, and received six out-of-transaction terminations;
  the Reading repeat had the same structure. Every valid Notepad profile created one and
  reused it five times, but several runs shared one Notepad process. Two earlier attempts
  were aborted because no new trace was collected; they are harness history, not field
  evidence. The captures contain no visual R3 result. Because R3
  Reading added immediate `GetValue` verification and extra synchronous trace writes, the
  cross-revision result cannot be attributed to Reading or read-back alone.
- **R4 design:** keep one DLL/CLSID/profile/display-attribute identity and run fresh
  AkelPad processes in this order: baseline A, trace-only control, immediate read-back,
  baseline B. Equal trace markers surround the control and read-back paths; only the latter
  calls `GetValue`. The baseline bookend detects order or environment drift (§12.7.8).
- Keep the comparison lab separate from the production IME: distinct DLL/display name,
  CLSID, and profile GUID, plus one JSONL file per process. Within the lab, choose separate
  identities or same-binary modes according to the causal question. Run one fixed scenario
  in Notepad and in the failing application.
- **Privacy rule**: never record entered text or memory addresses in any diagnostic
  variant. Keep only the structural fields, delete logs after the test, and disable
  logging in the normal release.
- **The stale-DLL trap, again**: when symptoms appear and vanish inexplicably, suspect
  deployment before code (§10's file locking — it played ghost twice in this project).

### 12.7 Failure ledger — discarded AkelPad hypotheses and the remaining causal model

This section preserves **how failures were classified**, not only the last code that
worked. Its purpose is to stop the next implementer from randomly reordering API calls,
declaring success from `S_OK` alone, or retesting a selection-style hypothesis that has
already been isolated and rejected.

#### 12.7.1 Classify failure before trying to fix it

| Class | Decision rule | Example | Response |
|---|---|---|---|
| **Our implementation defect** | ABI, HRESULT, lifetime, or state publication violates the contract | wrong vtable signature, publishing `S_OK + NULL` as a live composition, passing a key again after a partial edit | fix it, then restart the same scenario from a clean deployment |
| **Protocol hypothesis rejected** | the isolated change executes as intended and succeeds, but visible and lifetime results match the control | `TF_AE_NONE`, insert-first, omitting `SetSelection` | record “not sufficient”; do not generalize beyond that host |
| **Harness/deployment defect** | the new code never ran or the trace could not be collected | stale mapped DLL, deleting a locked log, batch-variable expansion | stop IME diagnosis and repair the experiment first |

Without this split, “the collector failed” and “the host terminated the composition” turn
into the same vague failure. In particular, **API request success**, **edit-session
success**, and **composition survival after the session** are three different verdicts.

#### 12.7.2 Fixed reproduction and what the trace actually proved

The experiment compared 64-bit AkelPad with Windows Notepad. Each profile ran in a fresh
process and document and received `rkskek` once. The expected visible result was `가나다`;
all four AkelPad profiles instead finalized compatibility jamo separately. Each DLL also
had an independent filename, CLSID, profile GUID, display-attribute GUID, and trace stem,
reducing the risk of selecting an old or different build.

One fixed six-key AkelPad run produced the following structure for every profile:

| Structural event | Control | `TF_AE_NONE` | Insert First | No Selection |
|---|---:|---:|---:|---:|
| Edit-session results | 6 | 6 | 6 | 6 |
| Request/session/inner failures | 0 | 0 | 0 | 0 |
| Successful `StartComposition` | 6 | 6 | 6 | 6 |
| Out-of-transaction `txn=0` `OnEndEdit` after close | 6 | 6 | 6 | 6 |
| Text/composing/attribute changes in that edit | 6/6/6 | 6/6/6 | 6/6/6 | 6/6/6 |
| Reading probe in that edit (later invalidated) | 0* | 0* | 0* | 0* |
| `OnCompositionTerminated` | 6 | 6 | 6 | 6 |

Each key repeated this order. The numbers below are illustrative; no process identifier or
entered text is needed:

```text
txn=N  range.set_text.after       S_OK  composition=1
txn=N  display_attribute.apply    S_OK  composition=1
txn=N  edit_session.result        all-S_OK, callback_ran=1
txn=N  edit_transaction.close           composition=1
txn=0  text_edit.end                    selection_changed=0
txn=0  changed_text/composing/attribute = 1, changed_reading = 0*
txn=0  composition_terminated           termination_epoch += 1
```

`*` The second-run audit found that `changed_reading` and `changed_langid` queried
mistranscribed GUIDs, not the predefined properties. Discard those two zero values.
Text, `GUID_PROP_COMPOSING`, `GUID_PROP_ATTRIBUTE`, HRESULTs, transaction ids, and the
termination epoch are independent, correctly identified fields, so the lifetime sequence
above remains valid evidence.

The important fact is that termination did not occur **inside** `SetText` or **inside** our
edit callback. The local composition was alive when our transaction closed; a separate
edit record and termination callback followed. Immediate API failure, a skipped callback,
and our own direct `EndComposition` call are therefore not the direct cause of this
reproduction.

All four variants visibly composed proper syllables in Notepad. Its trace also reused the
composition created for the first key through `composition.get_range.existing` on later
keys. A termination caused by process shutdown or explicit finalization is a normal
lifetime event, so the mere presence of one `OnCompositionTerminated` record is not a
failure verdict. The AkelPad fingerprint is the **per-key pair of an out-of-transaction
edit and termination**.

The context status differed too. This AkelPad run reported `static_flags=4`
(`TF_SS_TRANSITORY`) and `dynamic_flags=0x40000000`; Notepad reported
`static_flags=18` and `dynamic_flags=0`. That proves the applications expose different
kinds of text store, but does not prove that a status flag itself ordered termination.

#### 12.7.3 Defects we fixed that were not the final AkelPad cause

> **Status caveat:** “correction” below can mean the lab or required hardened example, not
> full migration of the current main product. In particular, trial-FSM publication and
> mutation-outcome propagation remain pending as described in §11.

| Defect or bad assumption | Symptom | Correction and verdict |
|---|---|---|
| Historical lab declaration added a nonexistent fourth `BSTR *` to `ITfDisplayAttributeProvider::GetDisplayAttributeInfo` | COM registers/stack can be misaligned: ABI-level undefined behavior | the lab changed to the documented three-parameter method; its corrected build still terminated in the AkelPad field run, excluding that historical ABI error as the cause of that reproduction |
| Current-product audit found a different fourth parameter, `TfGuidAtom *`, plus the `ITfDisplayAttributeMgr` IID value under the provider name | the product could request the wrong interface and expose a mismatched C vtable | corrected to the three-parameter method and provider IID `{FEE47777-163C-4769-996A-6E9C50AD8F54}`; T009/static checks and x64/x86 builds pass, but no new Windows/AkelPad field verdict exists for this product correction |
| Reading the Learn inheritance summary as proof that item/button are sibling vtables | a temporary audit patch would put `OnClick` where the Windows SDK caller expects the inherited item prefix | compared the Microsoft SDK IDL, rejected and reverted the temporary patch before commit, and made the SDK `IUnknown`→item→button order the T010 oracle |
| Guessing language-bar sink ownership, copied constants, and the no-icon result | sink leaks/UAF, wrong update mask, or a successful `AddItem` that never refreshes the tray | keep inherited button/item plus separate `ITfSource`, own the advised sink by cookie, use `TF_LBI_ICON=0x1` and `TF_LBI_STYLE_SHOWNINTRAY=0x2`, and return `S_OK + NULL` for no icon; Windows tray field verification remains separate |
| Attributing one `StartComposition` `E_INVALIDARG` to its NULL sink | we incorrectly declared the sink mandatory from a non-isolated call | the official contract says `pSink` is optional; check every argument/lock, record the host result separately, and use a real sink when termination callbacks are needed |
| Treating `StartComposition == S_OK` as sufficient even if the returned composition is NULL | later range failure or divergence between local and document state | `S_OK + NULL` means context-owner rejection; publish no local composition and take the product's fallback path |
| Treating the `RequestEditSession` result, callback `phrSession`, and inner operation result as one HRESULT | a successful request can hide a failed or unexecuted session | retain `request_hr`, `session_hr`, `inner_hr`, and `callback_ran` separately |
| Publishing the FSM before knowing the document result | internal state advances while the host did not, leaving scattered or stale jamo | required/lab correction: compute in a copy and publish only after success; pass the original key only when `MUTATION_NONE` is proven (§5.1). Main-product migration remains pending; the AkelPad lab later still failed with zero session failures |
| Reading compartment `VT_EMPTY` as “off” | Hangul/English toggle works only once in hosts such as PuTTY | treat it as “unset” and retain local fallback state; unrelated to composition termination |
| Swapped capability-GUID labels and a malformed immersive GUID | registration/activation conclusions can be confused on some environments | corrected to Microsoft constants. The desktop AkelPad profile already loaded, so this is unlikely to explain per-key termination, but it removes a confounder |
| Suspecting the x86 DLL or COM bitness | a 64-bit application might have loaded a different server | confirmed 64-bit AkelPad and verified PE32+ x86-64 DLLs and required exports; symptom unchanged |
| Overwriting a TSF DLL while it remains mapped in multiple processes | old and new symptoms appear to alternate like ghosts | use independent identities and trace stems; close editors, unregister, and log out when necessary |
| Deleting every temporary JSONL before collection | one old file held open by another process aborts the test | never delete globally; copy only matching logs modified after a run marker, skipping a locked candidate individually |
| Reading `%TARGET%` immediately after `set /p` in the same parenthesized `cmd.exe` block | the pasted AkelPad path is evaluated as the old empty value | moved input/use outside the block, then eliminated manual input by using the default install location |

These were not worthless fixes. Removing them made the later trace trustworthy. But because
the same out-of-session termination remained **after** each correction, continuing to use
them as a sufficient explanation for current AkelPad jamo separation would waste time.

`VT_EMPTY` deserves an explicit implementation pattern. For a compartment,
`GetValue == S_FALSE` with `VT_EMPTY` means “no value has been set,” not “the value is
false.” Preserve a known local fallback until an explicit `VT_I4` arrives:

```c
static HRESULT ReadOpenCompartment(ITfCompartment *comp,
                                   BOOL fallback_open, BOOL *open) {
    VARIANT v;
    VariantInit(&v);
    HRESULT hr = comp->lpVtbl->GetValue(comp, &v);

    if (hr == S_FALSE || (SUCCEEDED(hr) && v.vt == VT_EMPTY)) {
        *open = fallback_open;                 /* unset, not false */
        VariantClear(&v);
        return S_OK;
    }
    if (FAILED(hr) || v.vt != VT_I4) {
        VariantClear(&v);
        return FAILED(hr) ? hr : E_UNEXPECTED;
    }
    *open = (v.lVal != 0);
    VariantClear(&v);
    return S_OK;
}
```

**Additional defects discovered in the second field run and corrected in Meta R2:**

- The names `GUID_PROP_LANGID` and `GUID_PROP_READING` were attached to 16-byte values
  that do not match the Windows SDK. The contract byte-checked capability GUIDs but only
  checked source shape for these properties. A manually declared WinAPI constant needs an
  official byte-level oracle, not a plausible symbol name.
- The same wrong constants were used for both property application and the `OnEndEdit`
  probes. The failed application and “unchanged” observation therefore appeared to
  corroborate each other. Self-consistency against one wrong value is not validation.
- Optional metadata failure after successful `SetText` was returned as whole-session
  failure. A Windows edit session is not a database transaction and did not roll back the
  earlier document edit. Passing the original key with `pfEaten=FALSE` after that mutation
  applied one physical key through two paths.
- The collector retained activation-only files and empty profile directories without
  marking the cells incomplete. The next suite must require build identity and expected
  transaction/property-event counts.

The safe shape for genuinely optional metadata is explicit:

```c
HRESULT text_hr = range->lpVtbl->SetText(range, ec, 0, text, length);
if (FAILED(text_hr)) return text_hr;           /* no claimed document mutation */

HRESULT metadata_hr = ApplyOptionalMetadata(context, ec, range);
TraceMetadataResult(metadata_hr);
if (FAILED(metadata_hr)) {
    /* SetText already happened. Do not report imaginary rollback and do not pass
       the physical key through a second path. Metadata was declared optional. */
    return S_OK;
}
return S_OK;
```

If the metadata is protocol-critical instead, either reverse every completed mutation
explicitly or keep the key eaten and reconcile state. Merely returning a failure cannot
undo the document.

At this historical checkpoint, Meta R2 corrected all four defects and used new CLSID,
profile, display-attribute, and trace identities without changing the product or the v1
field artifact. Its build/package checks passed, but Windows execution was still pending.
Section 12.7.8 records the later completed R2 field run.

#### 12.7.4 AkelPad fixes rejected by one-variable A/B

| Hypothesis | Isolated change | Expected result | Actual result | Supported conclusion |
|---|---|---|---|---|
| active-end selection pushes the caret outside composition | change only `TF_AE_END` to `TF_AE_NONE` | host retains composition | same AkelPad failure; Notepad remains correct | selection style alone is not sufficient |
| the first text must be inserted in the order observed for MS Korean IME | insert with `TF_IAS_NO_DEFAULT_COMPOSITION`, then start composition on its nonempty returned range in the same write session | compatibility path accepts startup order | same AkelPad failure, all calls `S_OK` | insert-first alone is insufficient; this does not reproduce every private behavior of MS IME |
| explicit `SetSelection` causes termination | omit only that call | extra edit and termination disappear | in-session `selection_changed` changed from 1 to 0, but the later edit and termination were identical | the call itself is not the trigger |
| the control already fails at an API boundary | record every step and session HRESULT | identify a failing call | all six requests/sessions/inner results are `S_OK`, callback runs | reject immediate-call failure; investigate lifetime after close |

These conclusions apply to **this fixed AkelPad x64 scenario**. They do not establish that
selection style or startup order is irrelevant to every text store.

#### 12.7.5 Time lost to earlier legacy-host workarounds

Before the AkelPad A/B suite, PuTTY, messaging applications, and classic controls prompted
several workarounds. Those hosts may have different causes, so do not merge the observations
into one “CUAS bug.” They still identify approaches with low value for a repeat attempt:

| Attempt | Failure mode | Lesson |
|---|---|---|
| use flag 0 instead of `TF_ST_CORRECTION` | standards-correct, but termination continues in that host | the correct flag is necessary, not a compatibility fix by itself |
| establish an empty composition in a separate session | empty composition survives, text update is followed by termination | session separation alone is insufficient |
| switch between selection range, composition clone, and caret ranges | Notepad works; failing host does not improve | range provenance alone does not explain the failure |
| reorder display attribute and caret application | same termination | stop permuting successful calls without new evidence |
| set `GUID_PROP_LANGID` in PuTTY | no improvement | LANGID alone was insufficient there; it does not substitute for an unrun AkelPad test |
| replace a range without composition | old text is not replaced and output accumulates like `ㄱ가간…` | do not assume backward range editing on a restricted text store |
| send `VK_BACK`, then reinject Unicode | remote-shell echo delay and wide-character width cause loss/overlap | IME accounting cannot repair terminal rendering and remote line editing |
| append only committed output | often works slowly, loses characters under fast input | racing synthetic input is not an accuracy foundation |
| call `ImmSetCompositionStringW` directly | preedit appears, but `CPS_COMPLETE` does not insert committed text, so intermediate syllables vanish | a TSF TIP driving an IMM context directly lacks an atomic commit contract |

One mature third-party TSF IME also showed the PuTTY symptom in a field check, but **one
sample does not prove that pure TSF is impossible in every CUAS application**. Commit-only
was an engineering decision to reduce risk in the current support matrix; the independent
standard lab remains the place to continue protocol research.

#### 12.7.6 Strongest current causal model

| Confidence | Statement | Evidence and limitation |
|---|---|---|
| **Confirmed** | a separate edit and external termination callback arrive after our successful write transaction closes | the `txn=N` success → `txn=0` update → epoch increment sequence repeats for all four variants and all six keys |
| **Confirmed** | AkelPad and Notepad text stores have different lifetime behavior in the validated R3 structure | every valid AkelPad profile creates six compositions and loses six; every valid Notepad profile creates one and reuses five |
| **Confirmed** | the R2 Reading-only positive control did not reproduce in R3 | two valid R3 AkelPad Reading-only runs both show 6 new / 0 reused / 6 terminated, with successful property and session operations |
| **Strong inference** | AkelPad's TSF context interacts with an IMM32 compatibility path that rewrites text/composing/attribute state into a result and ends composition | `TF_SS_TRANSITORY`, AkelEdit's direct `WM_IME_*`/`ImmGetCompositionStringW` handling, and simultaneous post-session property changes. The trace cannot identify the actor or internal CUAS decision |
| **Open hypothesis** | R3's immediate property read-back or added synchronous trace I/O changed timing/reentrancy in this host | both were added to the R3 Reading path; R2/R3 also changed identity and deployment, so neither observer effect is isolated yet |
| **Not supported as the R3 differentiator** | LANGID presence, order, or repeated LANGID writes explain the R3 profiles | all four R3 AkelPad plans have the same lifetime result; because the Reading positive control itself failed, the planned R3 causal matrix cannot adjudicate the R2 correlation |
| **Rejected as sufficient** | active-end style, explicit selection, insert-first order, immediate API failure, or application bitness | isolated A/B or binary verification leaves the result unchanged |
| **Not knowable from current evidence** | “AkelPad itself terminated it,” “CUAS is conclusively the bug,” or “MS IME uses a secret API” | the trace has no caller stack or compatibility-layer internals |

The most accurate current statement is therefore: **after a successful TIP edit, a second
edit on the compatibility-host side ends the composition in the fixed AkelPad reproduction;
R2's one Reading-only survival run has not yet been causally explained**. Shortening this
to “CUAS is the culprit” or “Reading fixes AkelPad” turns an inference or correlation into
a fact. R4 isolates the two new in-transaction observer effects before any new metadata
policy is proposed.

#### 12.7.7 Second metadata A/B — executed, but failed before applying the hypothesis

The suite was designed to hold all other protocol choices fixed and compare four profiles:

1. Control: no added metadata
2. LANGID: set `GUID_PROP_LANGID=ko-KR` (`VT_I4`) over the current **nonempty**
   composition range
3. Reading: set the current reading string (`VT_BSTR`) over the same range
4. Both: set both

The uploaded structural trace shows that the lab failed before applying that hypothesis:

| Complete trace | Edit sessions | Metadata apply result | Session/inner | `hangul_step.failed` |
|---|---:|---:|---:|---:|
| AkelPad LANGID | 6 | all 6 `E_FAIL` | all 6 `E_FAIL` | 6 |
| AkelPad Both | 6 | all 12 `E_FAIL` | all 6 `E_FAIL` | 6 |
| Notepad LANGID (two complete runs) | 6 each | all 6 per run `E_FAIL` | all 6 per run `E_FAIL` | 6 each |
| Notepad Both | 6 | all 12 `E_FAIL` | all 6 `E_FAIL` | 6 |

The AkelPad Control file contains activation only, and both Reading-only directories lack
a JSONL payload. Preserve the visible observations, but do not invent structural results
for those three cells.

A byte comparison against Microsoft's Win32 metadata found the setup defect that makes the
experiment invalid:

| Property | Value in the tested DLL source | Windows SDK value |
|---|---|---|
| `GUID_PROP_LANGID` | `3280CE20-8032-11D2-B603-00C04F93D015` | `3280CE20-8032-11D2-B603-00105A2799B5` |
| `GUID_PROP_READING` | `5463F7C0-8E31-11D3-A9F3-00805F8EFFF8` | `5463F7C0-8E31-11D2-BF46-00105A2799B5` |

```c
static const GUID kPropLangId = {
    0x3280ce20, 0x8032, 0x11d2,
    { 0xb6, 0x03, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 }
};
static const GUID kPropReading = {
    0x5463f7c0, 0x8e31, 0x11d2,
    { 0xbf, 0x46, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 }
};
```

Test a manually declared GUID against an independently transcribed oracle, not against
another object built from the same macro:

```c
/* Oracle copied independently from Microsoft Win32 metadata; production code
   under test uses LAB_GUID_PROP_READING_INITIALIZER. */
static const unsigned char expected_data4[8] =
    { 0xbf, 0x46, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 };
GUID actual = LAB_GUID_PROP_READING_INITIALIZER;

assert(actual.Data1 == 0x5463f7c0);
assert(actual.Data2 == 0x8e31);
assert(actual.Data3 == 0x11d2);
assert(memcmp(actual.Data4, expected_data4, sizeof expected_data4) == 0);
```

Do not use the production initializer to construct both `actual` and `expected`; that only
tests self-consistency. Keep the official Win32-metadata link beside the oracle and review
every field, including the `11D2`/`11D3` field that failed here.

`ITfContext::GetProperty` accepts custom GUIDs, so a plausible object path does not validate
a predefined-property identifier. This trace event retained only the final HRESULT from the
combined `GetProperty`/`SetValue` path; all were `E_FAIL`, but the trace cannot identify the
failing subcall. `OnEndEdit` also queried changed ranges with those same wrong GUIDs.

The Notepad display follows directly from the event order:

```text
StartComposition / SetText     S_OK  (the document is already changed)
wrong-property apply path      E_FAIL
edit session                   E_FAIL (the earlier SetText is not rolled back)
display attribute/selection    skipped by early return
release local composition
pfEaten = FALSE                pass the original physical key to Notepad too
```

Because the function returned before moving the selection to the range end, original keys
accumulated to the caret's left while jamo inserted repeatedly at the same boundary pushed
older jamo to the right. This matches the reported
`rkskek(caret)ㅏㄷㅏㄴㅏㄱ` shape. It is a **partial-edit failure-handling defect**, not a
Notepad TSF composition limit.

The previous result must therefore be split:

- **Retained:** in the first A/B suite, AkelPad ended each composition in a separate edit
  after every TIP call and edit session had succeeded. The selection-style, insert-first,
  and no-selection findings remain valid.
- **Undetermined:** this run still did not test whether real LANGID/READING changes that
  termination. Do not report that both properties are ineffective.

A corrected suite must satisfy all of these requirements:

1. Replace both GUIDs in application and changed-range observation, with executable
   byte-for-byte tests against the official values.
2. Reacquire `ITfComposition::GetRange` after `SetText`, record only `IsEmpty` and length,
   then call `SetValue`. Microsoft SampleIME also reacquires its composition range before
   setting the language property.
3. Do not propagate optional metadata failure as fake rollback of successful `SetText`.
   Keep the valid base composition update, or explicitly reverse every mutation before
   passing the original key.
4. Use new DLL/schema identities and reject activation-only or empty profile cells.
5. Re-run all eight Control/LANGID/Reading/Both × AkelPad/Notepad cells.

The corrected `make akel-metadata-r2-suite` build implements requirements 1–4. A portable
executable test compares the production initializers field-by-field with an independent
official-value oracle, while every schema-2 update separates:

```text
metadata.range.get / is_empty / length
metadata.langid.get_property / set_value
metadata.reading.get_property / set_value
metadata.summary
```

A property failure remains in `metadata.summary` but no longer turns successful `SetText`
into edit-session failure. The lab therefore cannot repeat v1's partial edit followed by
`pfEaten=FALSE` for the same physical key. The collector requires the expected schema,
build and variant, at least six updates, nonempty ranges, both successful property calls,
successful sessions, and zero `hangul_step.failed`; it writes a content-free
`validation.json`. A visually plausible cell that fails this gate is invalid. Requirement
5 was evaluated with this gate in the fresh eight-cell Windows run.

Then interpret the result as follows:

| Result | Interpretation |
|---|---|
| exactly one single-property profile succeeds repeatedly | candidate necessary condition for this host; repeat and test other applications before porting |
| only Both succeeds | candidate interaction; rerun singles and combination separately |
| all four fail and property `SetValue` is `S_OK` | LANGID/READING is not sufficient; move to ownership/host-boundary hypotheses |
| a property operation returns failure | the variant never applied its hypothesis; do not report “property has no effect” |
| Control suddenly succeeds too | deployment, selected profile, application version, or order changed; re-establish reproduction before concluding |

`ITfProperty::SetValue` fails for an empty range and requires a write edit cookie. This
trace's `InWriteSession=TRUE` only proves that our client owned a write lock; it does not
prove that the range was nonempty or the GUID correct. `GUID_PROP_READING` is unsupported
in Store apps, so even a successful corrected run must not turn it into an unconditional
product requirement.

#### 12.7.8 Corrected R2, R3 structural result, and the R4 observer-effect probe

All eight R2 AkelPad/Notepad cells reported `validation.json: pass`. The validator therefore
confirmed the exact schema, build and variant, six metadata updates, nonempty ranges, expected
`GetProperty`/`SetValue` calls, successful edit sessions, and zero `hangul_step.failed`.
Unlike v1, this execution can compare real metadata effects without the wrong GUIDs, partial
edit failure, or duplicate original-key pass-through.

| Profile | AkelPad display and lifetime | Notepad |
|---|---|---|
| Control | separated jamo; 6 new compositions, 0 reuse, 6 external terminations | correct; 1 new and 5 reused |
| LANGID | separated jamo; 6 new compositions, 0 reuse, 6 external terminations | correct; 1 new and 5 reused |
| Reading | **correct syllables; 1 new, 5 reused, 0 per-key terminations** | correct; 1 new and 5 reused |
| LANGID → Reading | separated jamo; 6 new compositions, 0 reuse, 6 external terminations | correct; 1 new and 5 reused |

In each failing AkelPad profile, a `txn=0` composing change and
`OnCompositionTerminated` followed the successful TIP transaction on every key.
Reading-only had neither event. Its visible success therefore reflects one composition
surviving all six keys, not a coincidentally correct sequence of per-key commits.

The supported conclusion remains narrow:

- LANGID-only is not a sufficient fix for this fixed AkelPad scenario.
- Reading-only is a valid positive control in this execution.
- Failure of LANGID→Reading means that mere presence of Reading is not sufficient.
- The run does not yet distinguish whether LANGID itself, property order, or a repeated
  per-key LANGID write causes termination; it also does not make Reading universal.

The planned R3 suite used new identities and schema 3 to try to isolate those variables:

| R3 profile | Metadata plan within one update | Question |
|---|---|---|
| Reading | Reading | does the R2 positive control reproduce |
| LANGID Reading | LANGID → Reading | does the R2 negative control reproduce |
| Reading LANGID | Reading → LANGID | does reversing only property order change the result |
| LANGID Once Reading | LANGID → Reading on the context's first update, then Reading only | do repeated LANGID writes retrigger termination |

The last profile means once per context, not once per new composition. If LANGID ends the
first composition, it must not be written again on the next key; otherwise the experiment
cannot distinguish a repeated write from the first write's lasting effect.

The schema-3 validator requires exactly six updates, the selected plan event, property
order and counts per transaction, an expected-value match from `GetValue` immediately
after `SetValue`, a nonempty range, and successful sessions. It records no actual property
value, input character, key value, document content, pointer, path, or command line.
A passing `validation.json` establishes the structural result. A visible-behavior verdict
also requires a separately recorded screen observation.

The **pre-run** decision table was: if Reading→LANGID also fails, LANGID presence becomes
the leading candidate; if only the reversed order passes, order becomes the leading
candidate. If the once profile recovers after the first key, repeated writes are implicated;
if it stays broken, the first LANGID write may leave context-lifetime state. No single
AkelPad run is enough to turn any of these outcomes into a universal product policy.

**R3 structural result and the R4 same-binary observer-effect probe.**

R3 produced the eight intended structurally valid matrix cells, plus one additional valid
AkelPad Reading-only repeat. Two earlier collection attempts correctly failed validation
because no new trace was copied. Preserve those aborted attempts as harness history; they
are not field evidence.

| R3 profile | Valid AkelPad runs | AkelPad new / reused / externally terminated | Post-write composing changes | Notepad new / reused / externally terminated |
|---|---:|---:|---:|---:|
| Reading | 2 | 6 / 0 / 6 in each run | 6 in each run | 1 / 5 / 0 |
| LANGID → Reading | 1 | 6 / 0 / 6 | 6 | 1 / 5 / 0 |
| Reading → LANGID | 1 | 6 / 0 / 6 | 6 | 1 / 5 / 0 |
| LANGID once → Reading | 1 | 6 / 0 / 6 | 6 | 1 / 5 / 0 |

Every valid cell had six nonempty metadata ranges, the expected property order/count,
successful `GetProperty`, `SetValue`, immediate value verification, successful edit
sessions, and no `hangul_step.failed`. The captures contain **no R3 screen-result record**,
so the table says nothing about the exact visible characters. Several Notepad profiles
also ran in one already-loaded process; their per-profile structure is useful, but they
cannot control an inherited run-time selector.

The planned R3 matrix cannot answer its LANGID/order question because its Reading positive
control did not reproduce. The cross-revision comparison changed more than one thing:

- R3 called `ITfProperty::GetValue` immediately after every successful Reading
  `SetValue`, compared the returned `VARIANT`, and wrote extra synchronous trace events
  from inside the edit transaction. R2 did not.
- Binary/profile/display identities, schema, deployment, and activation-time trace shape
  also changed; for example, the earlier pre-context toggle event was absent from R3.

Therefore **do not conclude that `GetValue` caused termination**. It and synchronous trace
I/O are two observer-effect candidates; identity/process/deployment drift remains another.

R4 isolates the two in-transaction candidates without changing the loaded implementation.
One x64 DLL, CLSID, language profile, display-attribute GUID, schema, and build identity
are held fixed. A numeric mode is parsed once per fresh process; an unknown or absent mode
is invalid, and the trace records only the parsed enum—not raw environment text.

| Slot | Mode | Per-update behavior |
|---:|---|---|
| 0 | Baseline A (`SET_ONLY`) | Reading `GetProperty`/`SetValue`; no probe markers |
| 1 | Trace control | `probe.before` → Reading `GetProperty`/`SetValue` → `probe.result(S_OK, false)` |
| 2 | Read-back | the same before marker and property write → `GetValue` → the same result marker |
| 3 | Baseline B (`SET_ONLY` again) | repeat slot 0 in a fresh process to expose order/environment drift |

Trace control and Read-back put `GetProperty`/`SetValue` in the same marker envelope and
use the same event names, field counts, write path, and flush policy. Their only intended
difference is whether `ITfProperty::GetValue` runs after a successful `SetValue`.

A beginner-safe implementation keeps the base write outcome, diagnostic probe outcome,
and `VARIANT` lifetime separate:

```c
typedef enum R4Mode {
    R4_SET_ONLY,
    R4_TRACE_ONLY,
    R4_READ_BACK
} R4Mode;

typedef struct R4ProbeResult {
    HRESULT get_property_hr;
    HRESULT set_value_hr;
    HRESULT probe_hr;
    BOOL read_called;
    BOOL matched;
} R4ProbeResult;

static void ApplyR4Reading(R4Mode mode, ITfContext *ctx,
                           TfEditCookie ec, ITfRange *range,
                           const VARIANT *value,
                           const VARIANT *expected,
                           R4ProbeResult *out) {
    ITfProperty *property = NULL;
    BOOL traced = (mode == R4_TRACE_ONLY || mode == R4_READ_BACK);

    if (out == NULL) return;
    if (ctx == NULL || range == NULL || value == NULL || expected == NULL) {
        R4ProbeResult invalid = {
            E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, FALSE, FALSE
        };
        *out = invalid;
        return;
    }
    ZeroMemory(out, sizeof *out);
    out->get_property_hr =
        out->set_value_hr = out->probe_hr = E_UNEXPECTED;
    if (mode != R4_SET_ONLY &&
        mode != R4_TRACE_ONLY &&
        mode != R4_READ_BACK) {
        out->get_property_hr = E_INVALIDARG;
        return;
    }

    if (traced)
        TraceR4ProbeBefore(S_OK, FALSE); /* before GetProperty, as shipped */

    out->get_property_hr =
        ctx->lpVtbl->GetProperty(ctx, &GUID_PROP_READING, &property);
    if (FAILED(out->get_property_hr) || property == NULL) {
        if (SUCCEEDED(out->get_property_hr))
            out->get_property_hr = E_UNEXPECTED;
        return;
    }

    out->set_value_hr =
        property->lpVtbl->SetValue(property, ec, range, value);
    if (SUCCEEDED(out->set_value_hr) && traced) {
        VARIANT actual;
        VariantInit(&actual);

        out->probe_hr = S_OK; /* explicit trace-control placeholder */
        if (mode == R4_READ_BACK) {
            out->read_called = TRUE;
            out->probe_hr = property->lpVtbl->GetValue(
                property, ec, range, &actual);
            out->matched = SUCCEEDED(out->probe_hr) &&
                           VariantEquals(&actual, expected);
        }

        /* Same event fields and flush path; never log the actual value. */
        TraceR4ProbeResult(out->probe_hr, out->matched);
        VariantClear(&actual);                /* safe for VT_EMPTY/failure */
    }
    property->lpVtbl->Release(property);

    /*
     * out is diagnostic/validator data. Never turn probe_hr or a mismatch
     * into failure of a SetText/SetValue that has already succeeded.
     */
}
```

`VariantEquals` first compares `V_VT`, then compares the integer for `VT_I4` or the
length and contents for `VT_BSTR`. The caller must also `VariantInit` the input `value`,
allocate its `VT_BSTR`, and `VariantClear` it after this function returns. This function
owns only `actual`; every path after `VariantInit(&actual)` reaches `VariantClear`.

Most importantly, the shipped lab records a metadata summary but does not undo, duplicate,
or report failure for an already successful `SetText` merely because the optional metadata
probe failed. A probe failure can invalidate the **capture** for causal analysis; it cannot
retroactively make the document edit fail.

Each slot uses a fresh AkelPad process/document and the same six updates. Structural
validation checks the mode, operations, ordering, and session success, but **reports**
composition new/reuse/termination counts rather than using them to decide whether the
capture is well formed.

In the decision table, “retain” means one new composition, five reuses, and no per-key
external termination; “terminate” means six new compositions, no reuse, and six per-key
terminations.

| R4 lifetime result | Supported interpretation |
|---|---|
| both baselines retain, trace control retains, read-back terminates | the immediate read-back path (`GetValue`, returned `VARIANT` handling/comparison, and added execution time) is isolated as the trigger in this host scenario |
| both baselines retain, trace control and read-back terminate | added synchronous trace/timing is sufficient; read-back is not isolated |
| either baseline terminates | the earlier R2 positive is not currently reproducible; do not attribute R3 to read-back |
| baseline A and B disagree | order/environment drift invalidates the causal run |
| all four retain | R3 depended on identity, deployment, process state, or another uncontrolled condition |

This is a causal test plan, not a result. Do not promote any branch to product policy until
all four structurally valid field captures exist.

#### 12.7.9 Time-saving procedure for the next implementation

1. **Build a control first.** Pass a minimal TIP in a comparison host whose lifetime is
   known to work in this test, such as the tested Notepad setup. The application name alone
   does not classify a host as universally “native TSF.”
2. **Use fixed input.** Same key sequence, fresh document, fresh process, comparable speed.
3. **Collect visible output and structural trace together.** Either one alone can mislead.
4. **Separate request, session, and inner results.** Never declare success from the
   `RequestEditSession` HRESULT alone.
5. **Record survival after success.** Put callback end, transaction close, `OnEndEdit`,
   and `OnCompositionTerminated` on one sequence axis.
6. **Choose identity isolation deliberately.** Use separate DLL/CLSID/profile identities
   to prevent stale-profile confusion; use the same binary and run-time modes when identity
   must be held constant. Change one causal variable either way.
7. **After the same hypothesis fails twice, record and leave it.** Do not keep shuffling
   call order.
8. **Do not port into the product before the lab passes.** Keep product FSM/UI/config
   defects separate from the Windows protocol experiment.
9. **Verify deployment identity in the trace.** Missing new schema/events means the new
   DLL did not run.
10. **Never delete existing logs as setup.** Copy only the matching family after a marker
    into a new directory and skip locked candidates individually.
11. **Do not log content.** Events, HRESULTs, lengths, booleans, generations,
    transactions, and termination epochs are enough for lifetime diagnosis.
12. **Do not generalize one host failure into a Windows-wide limit.** Keep “confirmed,”
    “strong inference,” “open hypothesis,” and “rejected” distinct in the document.
13. **Byte-check every manually declared WinAPI GUID against an independent source.**
    Never use the same copied value as both the operation and its oracle.
14. **Do not mistake edit-session failure for rollback.** After mutating the document,
    pass the original key only after explicit recovery has succeeded.
15. **Validate collection completeness.** Activation-only files, empty profiles, or cells
    missing expected transactions/events are not successful captures.
16. **Bookend a timing-sensitive comparison.** Repeat the baseline after the treatment;
    disagreeing baselines invalidate the run.

Repeating `TF_AE_NONE`, omitting `SetSelection`, or insert-first **unchanged and in
isolation** has low value now. A rerun should introduce a genuinely distinct variable or
new observation, such as range ownership or metadata lifetime.

---

## 13. Text injection per app class

§8 concluded "insertion is the only universal, so commit-only." A later round of on-device
testing (v0.12) forced a sharper conclusion: **even `InsertTextAtSelection` is not
universal.** Supporting the tested application matrix took a routing policy plus a cluster
of related fixes. This chapter records that product policy; it is not a Windows capability
classifier for every application.

### 13.1 ★★The core reversal — the tested AkelEdit-family host accepted a TSF insert with hr=0 and showed no text

- **Problem**: in the tested **AkelEdit-family editor control on the inferred compatibility
  path**, typing
  Hangul worked for the first few characters, then some syllables simply **never appeared**;
  a repeated character was **swallowed entirely**; a selected 4-char word converted to hanja
  came out with only its **first two characters** replaced. The tested Notepad comparison
  did not reproduce these symptoms.
- **Diagnosis (from the log)**: every `INSERT` line showed `hr=0x00000000` — success — yet
  the captured caret rect **stayed frozen** (`CHIP raw=(14,825…)` across three inserts while
  the "advance" compensation ran up 29→58→87). The tested comparison control advanced its rect;
  this host reported success and showed no corresponding movement. From the application's
  visible state, the insert behaved as a no-op.
- **Supported conclusion**: `S_OK` from `InsertTextAtSelection` did not prove a visible
  insert in this host. The observation does not establish that every RichEdit, EDIT, CUAS,
  or terminal path behaves alike.
- **Product fix — route only validated capabilities.** For matching, field-tested
  EDIT-family controls, drive the control directly with `EM_REPLACESEL` (the standard
  EDIT message; an empty selection means “insert at caret”). Use a TSF session only for a
  non-EDIT host whose TSF-insertion path is in the product's validated support matrix.

The route selection exists in the current product. The `CommitOutcome` API and
mutation-aware caller below are the **migration target**, not a transcription of current
`src/`: today's `CommitText`/`OutputResult` discard results, and
`EditCtl_ReplaceSelection` reports that it dispatched `EM_REPLACESEL` without an
independent mutation acknowledgement (§11).

```c
#include <limits.h>   /* UINT_MAX */
#include <richedit.h> /* CHARRANGE, EM_EXGETSEL */
#include <wchar.h>    /* wcsstr */
#include <wctype.h>   /* towlower */

// Product heuristic: is the focused control in our tested EDIT family?
// Require the class name to contain "edit" so a self-rendering terminal that
// happens to answer EM_GETSEL is not mistaken for an editor (see §13.3).
HWND FocusEditWindow(void){
    GUITHREADINFO gti = { sizeof gti };
    if (!GetGUIThreadInfo(0,&gti) || !gti.hwndFocus) return NULL;
    HWND h = gti.hwndFocus;
    wchar_t cls[64]; int n = GetClassNameW(h, cls, 64);
    if (n<=0) return NULL;
    for (int i=0;i<n;i++) cls[i]=towlower(cls[i]);
    if (!wcsstr(cls, L"edit")) return NULL;          // e.g. Edit, RICHEDIT50W, AkelEdit
    CHARRANGE cr = {-2,-2};
    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);     // RichEdit-family controls answer this
    if (cr.cpMin >= 0) return h;
    DWORD s=UINT_MAX, e=UINT_MAX;
    SendMessageW(h, EM_GETSEL, (WPARAM)&s, (LPARAM)&e); // plain EDIT
    return (s != UINT_MAX && e != UINT_MAX) ? h : NULL;
}

typedef enum InjectRoute {
    INJECT_EDIT_MESSAGE,   /* class + selection probe + field test passed */
    INJECT_TSF,            /* this non-EDIT host visibly applied TSF insertion */
    INJECT_TEXT_KEY_PATH,  /* this renderer's text-key path was field-tested */
    INJECT_UNSUPPORTED
} InjectRoute;

typedef struct CommitOutcome {
    HRESULT hr;
    MutationState mutation;
} CommitOutcome;

// One choke point for finalized text. "!edit" is not an automatic TSF fallback.
CommitOutcome CommitText(Svc *svc, ITfContext *pic, const wchar_t *str) {
    HWND edit = FocusEditWindow();
    InjectRoute route = ClassifyVerifiedRoute(edit, pic);
    CommitOutcome out = { E_NOTIMPL, MUTATION_NONE };

    if (route == INJECT_EDIT_MESSAGE) {
        SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)str);
        svc->lastCaretValid = FALSE;       /* overlay uses the system caret */
        out.hr = S_OK;                     /* synchronous dispatch, not visual ack */
        out.mutation = MUTATION_UNKNOWN;   /* this message has no per-edit proof */
        return out;
    }
    if (route == INJECT_TSF)
        return RequestEditSessionInsert(svc, pic, str);
    if (route == INJECT_TEXT_KEY_PATH)
        return SendTextThroughVerifiedHostPath(str);
    return out;                            /* unsupported: no mutation */
}
```

`ClassifyVerifiedRoute` combines the class/selection probe with an explicit product
capability table. It must not infer “TSF works” merely from the absence of an EDIT match.
An unsupported result leaves `MUTATION_NONE`, so the caller can preserve its FSM and pass
an untouched physical text key. A dispatched EDIT message is `MUTATION_UNKNOWN`, because
`EM_REPLACESEL` gives no independent acknowledgement that the final screen changed.
A strict lossless caller must not translate `S_OK + MUTATION_UNKNOWN` into a proven commit
or publish its trial FSM. A product may consume the key under a host-scoped compatibility
policy to avoid duplication, but that is an explicit risk decision—not success.

- The current support-matrix routing is:

  | Observed route | Required capability match | Finalized-text path |
  |---|---|---|
  | validated non-EDIT TSF-insertion hosts | explicit support-matrix match | `InsertTextAtSelection` in a TSF session |
  | tested plain EDIT / RichEdit / AkelEdit family | class name contains `edit` and selection probe responds | **`EM_REPLACESEL`** |
  | tested self-rendering terminal | explicit terminal capability match | the TSF or text-key path verified for that host |

Class-name matching is neither a TSF-awareness test nor proof that a CUAS bridge is present.
An untested host remains unsupported until visible and structural validation adds it to a
row; silently trying TSF on every unknown host defeats the lossless transaction rule.

- **Lesson**: "it returned `S_OK`" is **not** "the user can see the intended result."
  Correlate the return with document/caret/lifetime observation, then choose a host-native
  mechanism only for the matrix in which it was verified.

### 13.2 Composing + an app shortcut (Ctrl/Alt/Win) — flush *in the test phase*

- **Problem**: pressing Ctrl+S mid-composition saved the document **without the last
  syllable** — the commit-only engine had it in the FSM, not in the document.
- **First (wrong) fix**: eat the combo in `OnTestKeyDown`, then in `OnKeyDown` flush the
  syllable and **re-send the key** with `SendInput`. On device, hammering Ctrl+C/Ctrl+V in
  the tested comparison editor produced **ㅊ and ㅍ** — the injected `C`/`V` landed *behind* already-queued user
  input (the Ctrl-up, the next key), so it was processed **without Ctrl** and interpreted as
  a jamo. Synthetic re-send fundamentally races subsequent user input; it is **wrong for
  modifier combos.**
- **Final fix — flush-in-test, no injection**: if a composition is active and Ctrl/Alt/Win
  is down, **commit the syllable right there in `OnTestKeyDown` (synchronous edit session)
  and do *not* eat the key.** The app then handles the shortcut natively, on its own timing,
  with the original event. No injection ⇒ no race. This is a deliberate, narrow exception
  to §5.1's prediction-only rule: it finalizes already-owned state before passing an
  original shortcut, but it must not process the shortcut as a new FSM key.

```c
// OnTestKeyDown
if (HasCtrlAltWin()){
    if (obj->fsm.state != EMPTY){             // flush a candidate copy synchronously
        HangulState trial = obj->fsm;
        FsmResult r = { Fsm_Flush(&trial), 0 };
        CommitOutcome out = { E_UNEXPECTED, MUTATION_NONE };
        BOOL call_ok = OutputResultSync(obj, pic, r, &out);
        BOOL proven = call_ok && SUCCEEDED(out.hr) &&
                      out.mutation == MUTATION_DONE;
        if (!proven) {
            /*
             * Do not run Save/Paste against a known incomplete document.
             * UNKNOWN is not success. This scoped policy consumes the
             * shortcut once to contain duplicate/incomplete-state risk.
            */
            ResetTransientComposition(obj);
            TraceOutputFailure(&out);
            obj->consumeTestedKey = TRUE;      // OnKeyDown consumes once, no retry
            *pfEaten = TRUE;
            return S_OK;
        }
        obj->fsm = trial;                     // publish only after flush success
    }
    *pfEaten = FALSE;                         // original shortcut passes untouched
    return S_OK;
}
```

`consumeTestedKey` is read and cleared exactly once by the immediately following
`OnKeyDown`. On the failure path, do not flush again or change `pfEaten` back to false:
that would disagree with the test-phase verdict or execute a shortcut against an
unverified document state. Only `MUTATION_DONE`—a proven postcondition—publishes the trial
FSM and permits the original shortcut to pass. Consuming `UNKNOWN` above is an explicit,
host-scoped product-risk policy and remains a failure outcome. This code is the hardened
migration target; the field-tested “flush in test, pass the original shortcut” behavior
does not by itself prove that the current main path propagates every failure.

### 13.3 Boundary-key resend — PostMessage to the app queue, not SendInput

- **Problem**: a boundary key that *must* be a real key (Enter, arrows — terminals need
  them, EDIT controls auto-indent on them) still lost the preceding syllable in the tested
  compatibility hosts,
  **even after delaying the `SendInput` resend by 30 ms**.
- **Strong field model**: `SendInput` uses the system input stream while the committed
  character reached the application through a different path. A delay did not establish
  a deterministic order.
- **Fix**: for an EDIT-family focus window, **post only the allow-listed boundary key
  straight to that window's message queue** with `PostMessage(WM_KEYDOWN/WM_KEYUP)`.
  This preserved the desired order in the tested EDIT route. Terminals (non-EDIT) keep a
  `SendInput` route only where that exact route was separately verified. Do not use this
  as a general character-key or shortcut injector.

```c
typedef struct BoundaryOutcome {
    HRESULT hr;
    MutationState mutation;
    BOOL down_posted;
    BOOL up_posted;
} BoundaryOutcome;

static HRESULT LastPostError(void) {
    DWORD error = GetLastError();
    return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE);
}

BoundaryOutcome ResendBoundaryKey(WPARAM vk, LPARAM lp) {
    BoundaryOutcome out = { E_INVALIDARG, MUTATION_NONE, FALSE, FALSE };
    if (!IsEditBoundaryKey(vk) || HasCtrlAltWin())
        return out; /* no characters, Space, or modified shortcuts here */

    HWND edit = FocusEditWindow();
    if (edit){
        LPARAM base = lp & 0x01FF0000;                        // scancode + extended bit
        out.down_posted = PostMessageW(edit, WM_KEYDOWN, vk, base | 1);
        if (!out.down_posted) {
            out.hr = LastPostError();
            return out;                                      // nothing was queued
        }

        out.mutation = MUTATION_UNKNOWN;                      // queued, not acknowledged
        out.up_posted = PostMessageW(
            edit, WM_KEYUP, vk, base | 0xC0000001);
        out.hr = out.up_posted ? S_OK : LastPostError();
        if (!out.up_posted)
            TraceBoundaryPostFailure(TRUE, FALSE);
        return out;
    }
    if (IsVerifiedNonEditKeyHost())
        return SendKeyThroughVerifiedHostPath(vk, lp);

    out.hr = E_NOTIMPL;                                      // unsupported, untouched
    return out;
}
```

If KEYDOWN was queued but KEYUP failed, the outcome is partial/unknown. Record both
booleans and **do not retry the event or fall back to `SendInput`**; doing so can duplicate
the already queued KEYDOWN. `IsEditBoundaryKey` should be a small explicit allow-list such
as Enter and the arrows. Space stays on the single “syllable + space” text operation from
§12.3, and Ctrl/Alt/Win combinations keep their original physical event (§13.2).

- **Lesson for the tested route**: putting both operations onto the application's route
  was effective; delaying a different route was not. This is field evidence, not a general
  cross-queue FIFO guarantee. (Space is still handled as §12.3 — "syllable+space" in one
  operation — which sidesteps resend entirely.)

### 13.4 Reading and replacing a selection — the EM_EXGETSEL ladder

Selection-based hanja (§12.5) reads the selection, and word conversion replaces a run before
the caret. Both need the selection API — and **some RichEdit-family controls do not answer the
classic `EM_GETSEL`; only the RichEdit `EM_EXGETSEL`.** So every selection op tries the RichEdit
message first, then the plain-EDIT message:

- **Read selection**: `EM_EXGETSEL`+`EM_GETSELTEXT` (the control copies the selected text
  itself — offset-unit agnostic) → else `EM_GETSEL`+`WM_GETTEXT` (plain EDIT, char offsets).
  The first path also fixed "대한민국 read as 대한": with `WM_GETTEXT` we sliced by an offset
  whose unit the control disagreed about; `EM_GETSELTEXT` hands back the exact text.
- **Replace a word before the caret** (the tested EDIT-family route, replacing the TSF
  `ShiftStart`): get the caret via `EM_EXGETSEL` (fallback `EM_GETSEL`), select the run with
  `EM_EXSETSEL`/`EM_SETSEL`, **read it back and verify it equals the expected word** (don't
  trust an observed offset convention), then `EM_REPLACESEL`. Windows `wchar_t` lengths
  are already UTF-16 code-unit counts. A doubled span observed in one host is therefore
  a host-specific offset candidate—not a general “character versus UTF-16” rule.

```c
#define MAX_REPLACE_WORD 64

static BOOL ReplaceBeforeCaretRich(HWND h, const WCHAR *expected,
                                   const WCHAR *replacement) {
    CHARRANGE saved = { -1, -1 };
    WCHAR got[MAX_REPLACE_WORD * 2 + 1];
    size_t expected_len = wcslen(expected);
    LONG spans[2];

    if (expected_len == 0 || expected_len > MAX_REPLACE_WORD) return FALSE;
    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&saved);
    if (saved.cpMin < 0 || saved.cpMin != saved.cpMax) return FALSE;

    spans[0] = (LONG)expected_len;
    spans[1] = (LONG)expected_len * 2; /* candidate seen only in this host */

    for (int i = 0; i < 2; ++i) {
        CHARRANGE target;
        if (saved.cpMin < spans[i]) continue;
        target.cpMin = saved.cpMin - spans[i];
        target.cpMax = saved.cpMin;

        SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&target);
        ZeroMemory(got, sizeof got);
        LRESULT copied =
            SendMessageW(h, EM_GETSELTEXT, 0, (LPARAM)got);
        got[MAX_REPLACE_WORD * 2] = L'\0';

        if (copied == (LRESULT)expected_len &&
            wmemcmp(got, expected, expected_len) == 0) {
            SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)replacement);
            return TRUE;              /* dispatch complete, not a visual ack */
        }

        /* Never leave the user's selection changed after a bad candidate. */
        SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&saved);
    }
    return FALSE;
}

static BOOL ReplaceBeforeCaretPlainEdit(HWND h, const WCHAR *expected,
                                        const WCHAR *replacement) {
    DWORD saved_start = UINT_MAX, saved_end = UINT_MAX;
    DWORD verify_start = UINT_MAX, verify_end = UINT_MAX;
    size_t expected_len = wcslen(expected);
    LRESULT text_len;
    WCHAR *all;
    BOOL match = FALSE;

    if (expected_len == 0 || expected_len > MAX_REPLACE_WORD) return FALSE;
    SendMessageW(h, EM_GETSEL, (WPARAM)&saved_start, (LPARAM)&saved_end);
    if (saved_start == UINT_MAX || saved_end == UINT_MAX ||
        saved_start != saved_end || saved_end < expected_len)
        return FALSE;

    text_len = SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);
    if (text_len < 0 || (ULONGLONG)text_len > 1024u * 1024u) return FALSE;
    all = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                    ((SIZE_T)text_len + 1) * sizeof *all);
    if (all == NULL) return FALSE;

    LRESULT copied = SendMessageW(
        h, WM_GETTEXT, (WPARAM)((SIZE_T)text_len + 1), (LPARAM)all);
    if (copied >= 0 && (ULONGLONG)saved_end <= (ULONGLONG)copied) {
        size_t begin = (size_t)saved_end - expected_len;
        match = wmemcmp(all + begin, expected, expected_len) == 0;
    }
    HeapFree(GetProcessHeap(), 0, all);
    if (!match) return FALSE;

    DWORD target_start = saved_end - (DWORD)expected_len;
    SendMessageW(h, EM_SETSEL, target_start, saved_end);
    SendMessageW(h, EM_GETSEL,
                 (WPARAM)&verify_start, (LPARAM)&verify_end);
    if (verify_start != target_start || verify_end != saved_end) {
        SendMessageW(h, EM_SETSEL, saved_start, saved_end); /* restore */
        return FALSE;
    }

    SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)replacement);
    return TRUE;
}

static BOOL ReplaceBeforeCaretExact(HWND h, const WCHAR *expected,
                                    const WCHAR *replacement) {
    CHARRANGE probe = { -1, -1 };
    if (h == NULL || expected == NULL || replacement == NULL) return FALSE;

    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&probe);
    if (probe.cpMin >= 0 && probe.cpMax >= 0)
        return ReplaceBeforeCaretRich(h, expected, replacement);
    return ReplaceBeforeCaretPlainEdit(h, expected, replacement);
}
```

Both paths replace only after an exact match. The RichEdit path restores the original
selection after each wrong span. The plain EDIT path initializes both selection outputs
to sentinels, verifies the document slice, selects the candidate, and reads the selection
back before replacing it. Any failed speculative selection is restored; only a verified
replacement may leave the selection changed.

- **Lesson**: RichEdit-family controls (modern rich-text editors, chat inputs) may
  ignore the legacy EDIT messages — **probe `EM_EXGETSEL` before `EM_GETSEL`** for both
  detection and selection, or you silently fall back to the broken path.

### 13.5 The stuck-character ghost key (moa-chigi / chorded layouts)

- **Problem**: one specific character (e.g. 대) became **permanently un-typable** — it was
  eaten every time and the stale composition stayed pinned in the preview chip. Hiding the
  preview didn't help; the character was stuck *somewhere*.
- **Cause**: the simultaneous-input (moa-chigi) and chord state machines key off a per-vk
  `keyDown[]` flag. If a **key-up is ever lost** (focus change during a chord, a swallowed
  message), the flag stays set; the next press looks like **auto-repeat**, so the machine
  eats the key and re-shows the old composition **forever**, never committing.
- **Tested mitigation**: on the “repeat” branch, compare the private flag with
  `GetKeyState` and self-heal one stale pattern. This is a message-queue-state heuristic,
  not a current physical-key oracle.

```c
if (c->keyDown[vk]){                       // looks like a repeat…
    if (GetKeyState(vk) & 0x8000) return REPEAT;   // queue state is still down here
    c->keyDown[vk] = false;                // tested stale-flag inference
    if (c->activeKeys>0) c->activeKeys--;  // …and treat this as a fresh press
}
```

`GetKeyState` reflects the state associated with the calling thread's input processing;
that state changes as the thread removes key messages from its queue. It does **not**
sample the hardware's current state. The rule above fixed the observed lost-key-up case
under the tested TIP callback ordering, but focus changes, cross-thread queues, and
injected input can invalidate the inference. Keep focus/deactivation reset paths, trace
unexpected transitions, and validate this heuristic in each host; do not promote it to a
universal repeat detector or blindly substitute another global key-state API.

- Plus two escape hatches, both routed through one reset function (§13.7): **Esc cancels the
  composition** (MS-IME convention; sequential *and* moa-chigi), and **Backspace clears a
  moa-chigi composition** (the old gate tested `fsm.state`, which moa-chigi never sets).

### 13.6 Keep the original on cancel — commit *then* replace

- **Problem**: type 가, press the Hanja key, leave the candidate window **without choosing**
  → the 가 **disappeared**. The composing syllable was never in the document; the engine had
  planned to *insert* the hanja over nothing.
- **Fix**: on Hanja-key while composing, **commit the syllable to the document first**
  (making it `replaceLen=1`, targeting that just-committed char), then convert by *replacing*
  it. Cancelling leaves the committed original in place — the same pattern as word conversion.

### 13.7 Required reset choke point + a captured target window

Two small structural patterns that prevent whole classes of the above:

- **Required `ResetComposition()` choke point** — one function should clear the FSM,
  chord state, chip state, caret/compensation state, and overlay. Focus change, Deactivate,
  Esc, backspace-clear, and hanja-commit should all call it. Preview *show* stays solely in
  `OutputResult`. The current main `TIP_Deactivate` still calls `Fsm_Init`, `Chord_Init`,
  and overlay teardown separately and can leave caret/chip compensation fields behind;
  migration to this single choke point is pending.
- **Capture the target EDIT window at Hanja-key time.** The candidate callback fires *after*
  focus has moved, so calling `GetGUIThreadInfo` inside the callback returns the wrong window
  and the replacement falls back to the broken TSF path. Store the `HWND` when you show the
  candidate window; use the stored one in the callback.

> The overlay can hold **more than one character** — leave its buffer generous (jamotong
> uses 64). Preview is not Korean-only: a multi-key romaji sequence or a word-level preedit
> in another language may need several characters shown at once.

### 13.8 State ownership — what is process-global vs per-service, and who frees it

The popup windows and dictionaries are **process-global, input-thread-only** singletons — one
chip window, one candidate window, one dictionary per process, regardless of how many
`CTextService` instances the process has. This is deliberate (a process shows one composition
at a time) but the ownership rules must be explicit or you get the stuck-chip / stale-pointer
class of bug (§13.5, §13.7). The following is the ownership contract the implementation
**must satisfy**, not a current-product guarantee. The current cancel path releases `pic`
but does not yet clear every raw `obj`/`targetHwnd` field; that migration is pending.

| State | Scope | Created | Destroyed | Notes |
|---|---|---|---|---|
| FSM / chord context | per `CTextService` | `Create` | `Deactivate` (`ResetComposition`) | one composition per service |
| Preedit overlay window/font | process-global | lazily on first `Show` | `Deactivate` (`Uninitialize`) | input thread only; `Hide` reuses it |
| Candidate window + `g_CandCtx` | process-global | first Hanja key | **`Cancel` on focus change / Deactivate** | `pic` is AddRef'd; `obj`/`targetHwnd` are raw — must be cleared on Cancel |
| Code-input popup | process-global | first `Ctrl+Alt+U` | `Deactivate` (`Uninitialize`) | input thread only |
| Hanja / hunum dictionaries | process-global | **first hanja use** (lazy) | process exit | shared read-only; load once |
| Settings window (separate thread) | process-global | `SettingsUI_Show` | `Deactivate` joins the thread | edits the live config under `g_configLock` |

Rules that keep it correct:
- **One create, one destroy.** Every popup has an `Uninitialize`/`Cancel` reached from
  `Deactivate`; a callback context that stores a raw service pointer (`g_CandCtx.obj`) **must**
  be cleared when the popup closes, or a later callback dereferences a freed service.
- **Input-thread affinity.** All popup windows live on the input (TIP) thread; never touch them
  from the settings thread. Cross-thread config access is the *only* thing `g_configLock` guards.
- **AddRef what outlives the call.** The candidate callback fires later, so its `pic` is
  AddRef'd at show time and Released in the callback; the target `HWND` is captured then too
  (§13.7), because focus has moved by the time the callback runs.

```c
static void CandidateContext_Clear(CandidateContext *cc) {
    if (cc == NULL) return;
    if (cc->pic != NULL) {
        cc->pic->lpVtbl->Release(cc->pic);
        cc->pic = NULL;
    }
    cc->obj = NULL;          /* raw: clear it, do not Release it here */
    cc->targetHwnd = NULL;   /* never carry an HWND into the next session */
    cc->fromSelection = FALSE;
    SecureZeroMemory(cc->word, sizeof cc->word);
}

/* Selection, cancel, focus-change Cancel, and Deactivate all call this last. */
```

---

## Appendix A: jamotong source map

| Concept | File |
|---|---|
| COM entry points, class factory | `src/dllmain.c` |
| TIP, key sink, OnKeyDown | `src/text_service.c` |
| Edit sessions (commit-only) | `src/edit_session.c` |
| Registration (profile, categories) | `src/register.c` |
| Hangul automaton | `src/fsm.c`, `src/layout.c`, `src/hangul_layout.c` |
| Hanja dictionary, candidate window | `src/hanja_dict.c`, `src/candidate_ui.c` |
| Settings UI, persistence | `src/settings_ui.c`, `src/config.c` |
| Composition preview overlay (§12.1–12.2) | `src/preedit_overlay.c`, `OutputResult` in `text_service.c` |
| Tray mode icon (§12.4) | `src/langbar.c` |
| Per-app-class injection (§13.1), EDIT selection ops (§13.4) | `EditCtl_*` / `CommitText` in `src/edit_session.c`, `text_service.c` |
| Ghost-key self-heal (§13.5), reset choke point (§13.7) | `src/chord.c`, `src/chord_layout.c`, `ResetComposition` in `text_service.c` |
| Codepoint input popup (with character name) | `src/code_input.c` |
| Manager app (`.jmt` editor / validate / settings / TSF-less input test) | `src/tray_app.c` |
| `.jmt` loaders + parse diagnostics (`KlayDiag`) | `src/klay.c`, `src/hangul_layout.c`, `src/chord_layout.c` |
| (dead) IMM32 IME attempt | `src/imm/` |


## Appendix B: References

Every link was checked at the time of writing. Where the official docs do not answer, §8,
§12 and §13 record what field testing showed — **the gap between documented and actual
behaviour is why this manual exists.**

### TSF in general

| Document | Link |
|---|---|
| Text Services Framework overview | https://learn.microsoft.com/en-us/windows/win32/tsf/text-services-framework |
| TSF Architecture | https://learn.microsoft.com/en-us/windows/win32/tsf/architecture |
| Using Text Services Framework | https://learn.microsoft.com/en-us/windows/win32/tsf/using-text-services-framework |
| TSF Reference index | https://learn.microsoft.com/en-us/windows/win32/tsf/text-services-framework-reference |
| msctf.h interface index | https://learn.microsoft.com/en-us/windows/win32/api/msctf/ |

### Interfaces this manual uses

| Interface | Used for | Link |
|---|---|---|
| `ITfTextInputProcessor` | TIP itself (Activate/Deactivate) — §3 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itftextinputprocessor |
| `ITfTextInputProcessorEx` | activation flags extension | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itftextinputprocessorex |
| `ITfThreadMgr` | thread manager — the entry point to everything | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfthreadmgr |
| `ITfKeystrokeMgr` | advise the key sink — §5 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfkeystrokemgr |
| `ITfKeyEventSink` | receiving keys — **watch vtbl order** §5.1 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfkeyeventsink |
| `ITfKeyEventSink::OnTestKeyDown` | predicts whether the service would handle the key — §5.1 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfkeyeventsink-ontestkeydown |
| `ITfEditSession` | edit session — §7 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfeditsession |
| `ITfContext` | document context | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfcontext |
| `ITfContext::RequestEditSession` | separate method return from `phrSession`; sync semantics — §7.1 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession |
| `ITfInsertAtSelection` | **insertion (the heart of commit-only)** — §8.4 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfinsertatselection |
| `ITfRange` | document range; host support must be verified — §8.2 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfrange |
| `ITfComposition` | composition — external termination in some compatibility hosts §8.1 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfcomposition |
| `ITfCompositionSink` | optional composition-end notification sink | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfcompositionsink |
| `ITfCompositionSink::OnCompositionTerminated` | correlate external termination — §12.6 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcompositionsink-oncompositionterminated |
| `ITfContextComposition` | starting a composition | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfcontextcomposition |
| `ITfContextComposition::StartComposition` | optional sink and documented `S_OK + NULL` rejection — §7.3 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontextcomposition-startcomposition |
| `ITfContextOwnerCompositionSink::OnStartComposition` | context owner can accept/reject a new composition | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontextownercompositionsink-onstartcomposition |
| `ITfTextEditSink::OnEndEdit` | observe edit completion order — §12.6 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itftexteditsink-onendedit |
| `ITfEditRecord::GetTextAndPropertyUpdates` | observe text/property changed-range presence without contents — §12.6 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfeditrecord-gettextandpropertyupdates |
| `ITfContext::InWriteSession` | determine whether our client owns a write lock during notification — §12.7 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-inwritesession |
| `ITfContext::GetProperty` | obtain a predefined/custom property object — §12.7 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-getproperty |
| `ITfProperty::SetValue` | set LANGID/READING on a nonempty range — §12.7 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfproperty-setvalue |
| `ITfReadOnlyProperty::GetValue` | read-back, `VT_EMPTY`, and `VariantClear` ownership — §12.7.8 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfreadonlyproperty-getvalue |
| `ITfCompartment::GetValue` | unset compartment returns `S_FALSE`/`VT_EMPTY` — §12.7.3 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcompartment-getvalue |
| `ITfRange::SetText` | successful edit followed by non-atomic failure — §12.7 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfrange-settext |
| `ITfInsertAtSelection::InsertTextAtSelection` | insert-first A/B and `TF_IAS_NO_DEFAULT_COMPOSITION` contract — §8.1 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinsertatselection-inserttextatselection |
| `TF_SELECTIONSTYLE` | `TF_AE_NONE` selection-style A/B — §12.6 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/ns-msctf-tf_selectionstyle |
| `ITfInputProcessorProfiles` | profile registration — §4.2 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfinputprocessorprofiles |
| `ITfCategoryMgr` | category registration — §4.3 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfcategorymgr |
| `ITfCategoryMgr::RegisterCategory` | omit it and the IME lists but never activates | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcategorymgr-registercategory |
| `ITfFnConfigure` | configuration dialog — §9.3 | https://learn.microsoft.com/en-us/windows/win32/api/ctffunc/nn-ctffunc-itffnconfigure |
| `ITfLangBarItemButton` | language bar — §9.4 | https://learn.microsoft.com/en-us/windows/win32/api/ctfutb/nn-ctfutb-itflangbaritembutton |
| `ITfDisplayAttributeInfo` | composition display attributes | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfdisplayattributeinfo |
| `ITfDisplayAttributeProvider::GetDisplayAttributeInfo` | exact C vtbl signature — §10 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfdisplayattributeprovider-getdisplayattributeinfo |

### Concept pages

| Document | Link |
|---|---|
| Compositions | https://learn.microsoft.com/en-us/windows/win32/tsf/compositions |
| Edit Sessions | https://learn.microsoft.com/en-us/windows/win32/tsf/edit-sessions |
| Text Stores | https://learn.microsoft.com/en-us/windows/win32/tsf/text-stores |
| Compartments | https://learn.microsoft.com/en-us/windows/win32/tsf/compartments |
| Predefined Properties (`GUID_PROP_*` formats and semantics) | https://learn.microsoft.com/en-us/windows/win32/tsf/predefined-properties |
| Actual TSF GUID values in Microsoft Win32 metadata | https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/manual/TextServices.Manual.cs |
| `TF_STATUS` (`TF_SS_TRANSITORY`) | https://learn.microsoft.com/en-us/previous-versions/windows/desktop/legacy/ms629192(v=vs.85) |
| Mozilla 1208043 (observed MS Korean IME insert-first order) | https://bugzilla.mozilla.org/show_bug.cgi?id=1208043 |
| AkelEdit source (BSD; confirms direct IMM32 composition handling) | https://svn.code.sf.net/p/akelpad/codesvn/trunk/akelpad_4/AkelEdit/AkelEdit.c |

### COM (implementing in C)

| Document | Link |
|---|---|
| What Is a COM Interface? | https://learn.microsoft.com/en-us/windows/win32/learnwin32/what-is-a-com-interface- |
| `IClassFactory` | https://learn.microsoft.com/en-us/windows/win32/api/unknwn/nn-unknwn-iclassfactory |
| `DllRegisterServer` | https://learn.microsoft.com/en-us/windows/win32/api/olectl/nf-olectl-dllregisterserver |
| `VariantClear` | https://learn.microsoft.com/en-us/windows/win32/api/oleauto/nf-oleauto-variantclear |

### IMM32 and app compatibility (§8.3, §13)

| Document | Link |
|---|---|
| Input Method Manager (IMM32) | https://learn.microsoft.com/en-us/windows/win32/intl/input-method-manager |
| `WM_IME_STARTCOMPOSITION` | https://learn.microsoft.com/en-us/windows/win32/intl/wm-ime-startcomposition |
| `EM_EXGETSEL` (reading selection, §13.4) | https://learn.microsoft.com/en-us/windows/win32/controls/em-exgetsel |
| `SendInput` (system-input route; avoid for the tested EDIT resend) | https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput |
| `PostMessage` (tested EDIT boundary-key route) | https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postmessagew |

### Hangul

| Document | Link |
|---|---|
| Hangul Syllables U+AC00–D7A3 (chart) | https://www.unicode.org/charts/PDF/UAC00.pdf |
| Hangul Jamo U+1100–11FF (chart) | https://www.unicode.org/charts/PDF/U1100.pdf |
| Unicode Standard ch.3 — Hangul composition | https://www.unicode.org/versions/latest/ch03.pdf |

The formula is in §6: `0xAC00 + (cho×21 + jung)×28 + jong`.

### Tools and samples

| Item | Link |
|---|---|
| MinGW-w64 (this project's compiler) | https://www.mingw-w64.org/ |
| Microsoft SampleIME (official TSF IME implementation sample) | https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/IME/cpp/SampleIME |

### This repository

| Item | Location |
|---|---|
| Minimal working example (§0.5) | `examples/minimal-tip/` |
| Full implementation | `src/` — see the source map in Appendix A |
| Change history | `CHANGELOG.md` |

---

*This manual's commit-only product decision and host-routing rationale (§8, §13) were
earned through 20+ rounds of on-device verification. Reuse the experimental method, not
an unqualified compatibility claim: validate composition lifetime and finalized-text
delivery in your own application matrix.*
