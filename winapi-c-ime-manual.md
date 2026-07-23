# Building a Windows Korean IME with Nothing but WinAPI and C — A Field Manual

[한국어](winapi-c-ime-manual.ko.md) | **English**

*Last updated: 2026-07-24 (AkelPad Meta R2 field result and R3 interaction suite)*

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
8. [★The big lesson: the CUAS wall and "commit-only"](#8-the-big-lesson)
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
- **Linking**: `-lole32 -luuid` for COM; most TSF symbols come from `msctf.h` + `-luuid`.
  `-municode` is not needed for a DLL.
- **Build shape**: `gcc -shared -o jamotong.dll *.c jamotong.def -lole32 -luuid -lgdi32 ...`
  - Export `DllGetClassObject`, `DllCanUnloadNow`, `DllRegisterServer`,
    `DllUnregisterServer` through a `.def` file (on 32-bit, name decoration makes the
    `.def` effectively mandatory).

---

## 0.5 Start Here — Build an IME That Shows Up and Eats One Key

TSF documentation is vast and it is hard to know where to begin. So build **the smallest thing
that works** first. The code lives in `examples/minimal-tip/` and this repository builds it on
every change, so it cannot rot.

Goal: **appear in the Windows language list, and turn the `a` key into `ㄱ` in the document.**
That is all. Two files: `minimal.c` (~200 lines) and `minimal.def` (6 lines).

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
Notepad is the most forgiving host; failing there usually means the key sink never attached.
The fastest check is `OutputDebugStringW(L"activated\n")` in `Activate` plus DebugView
(§12.6, diagnostics).

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
| Composition preview (underline) | **it dies in CUAS apps** — the core finding of this manual | §8 |
| Hangul/English toggle | state management stops it being minimal | §9.2 |
| Per-app-class strategies | Notepad working feels like "done". It is not. | §13 |

**Read §8 before anything else.** "Add a composition preview, watch it break in every CUAS
app" is the wall this project hit twenty-plus times, and the reason the minimal example
sidesteps it from the start.

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
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfKeyEventSink)) {
        *ppv = self; This->lpVtbl->AddRef(This); return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
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
| **IMM32** (Input Method Manager) | legacy (9x) | the old way. Win11 **blocks registration of third-party IMM32 IMEs outright** (§8.3) |

- **CUAS** (Cicero Unaware Application Support): for apps that still receive input via
  IMM32, the OS **translates TSF composition into IMM32 messages** (`WM_IME_*`). This
  bridge being incomplete is the root of all the pain.

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
> included LANGID still terminated per key. Section 12.7.8 records this valid R2 result
> and the R3 design that isolates order and repeated writes.

---

## 3. TIP skeleton

### 3.1 Required interfaces (minimal IME)

| Interface | Role | Required? |
|---|---|:---:|
| `ITfTextInputProcessor` | `Activate`/`Deactivate` entry point | ✅ |
| `ITfKeyEventSink` | key input | ✅ |
| (class factory `IClassFactory`) | object creation | ✅ |
| `ITfDisplayAttributeProvider` | composition underline color/style | only if composing |
| `ITfCompositionSink` | notification when composition is terminated externally | only if composing |
| `ITfFunctionProvider`+`ITfFnConfigure` | "Options" button → settings window | optional |
| `ITfThreadMgrEventSink`/`ITfTextEditSink` | document focus/edit tracking | optional |

> **A commit-only IME (§8) needs neither `ITfDisplayAttributeProvider` nor
> `ITfCompositionSink`** — it never composes. jamotong ultimately dropped both.

### 3.2 Activate / Deactivate

```c
HRESULT STDMETHODCALLTYPE TIP_Activate(ITfTextInputProcessor *This, ITfThreadMgr *ptim, TfClientId tid){
    JamotongTextService *obj = (JamotongTextService*)This;
    obj->threadMgr = ptim; ptim->lpVtbl->AddRef(ptim);   // keep + AddRef
    obj->clientId  = tid;                                 // used in every later TSF call

    // register the key event sink
    ITfKeystrokeMgr *ksm = NULL;
    ptim->lpVtbl->QueryInterface(ptim, &IID_ITfKeystrokeMgr, (void**)&ksm);
    ksm->lpVtbl->AdviseKeyEventSink(ksm, tid, (ITfKeyEventSink*)&obj->lpVtblKES, TRUE /*fForeground*/);
    ksm->lpVtbl->Release(ksm);
    return S_OK;
}
// Deactivate: UnadviseKeyEventSink → Release threadMgr → reset state
```

`clientId` (TfClientId) is **your TIP's ID card** — pass it to nearly every TSF call,
including edit-session requests.

---

## 4. Registration

An IME registers in **three layers**, all handled in `DllRegisterServer`.

### 4.1 COM server registration (registry)
```
HKCR\CLSID\{our-clsid}\InprocServer32  (default) = full DLL path,  ThreadingModel = Apartment
```
- The 64-bit DLL goes to the 64-bit hive (regsvr32 in System32); the 32-bit one to
  `HKLM\WOW6432Node\Classes` (SysWOW64\regsvr32). **Register per bitness** so each
  bitness of app can load its DLL.

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
ITfCategoryMgr *cat; CoCreateInstance(&CLSID_TF_CategoryMgr, ...);
cat->lpVtbl->RegisterCategory(cat, &CLSID_Ours, &GUID_TFCAT_TIP_KEYBOARD, &CLSID_Ours);
// only if you use composition underline:
cat->lpVtbl->RegisterCategory(cat, &CLSID_Ours, &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, &CLSID_Ours);
// Win11 Store/immersive app compatibility:
cat->lpVtbl->RegisterCategory(cat, &CLSID_Ours, &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, &CLSID_Ours);
```

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
   `*pfEaten`. **No side effects allowed.**
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

### 5.2 VK → character mapping
Convert virtual key (VK) + Shift into the QWERTY character first (`'R'`→`'r'`,
Shift+`'2'`→`'@'`), then map that character to a jamo through the layout table. You can
use raw VK constants (0xBA etc.) if you prefer not to depend on `windows.h` names.

### 5.3 Keys you must pass through
- **Ctrl/Alt/Win combinations**: they are app shortcuts (Ctrl+C). Don't eat them
  (`*pfEaten=FALSE`). (**Shift is the exception** — needed for capitals and tense consonants.)
- **Modifier/lock keys** (Shift/Ctrl/Alt/Caps/Hangul/Hanja...): must not commit the composition.

### 5.4 Re-entry guard for our own synthetic input
Boundary-key resending (§9.2) uses `SendInput`, and that key comes back into `OnKeyDown`.
Plant a **magic value** in `ki.dwExtraInfo` (e.g. `0x4A414D4F`) when calling `SendInput`,
and at the top of the handlers: if `(ULONG_PTR)GetMessageExtraInfo() == magic`, return
immediately.

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

> The FSM is **easy to unit-test as native code** (no Windows needed). jamotong verifies
> it with 47 tests.

---

## 7. Edit sessions

### 7.1 Edits happen only inside an edit session
Modifying the document (`ITfContext`) requires an **edit cookie**, and the cookie is
only handed to you via the `RequestEditSession` callback:

```c
// 1) implement ITfEditSession (§1.2) — DoEditSession(ec) is the meat
// 2) request:
ctx->lpVtbl->RequestEditSession(ctx, clientId, (ITfEditSession*)&es, TF_ES_SYNC|TF_ES_READWRITE, &hr);
// 3) TSF calls back our DoEditSession(ec) → modify the document with that ec
```
- `TF_ES_SYNC`: the callback runs synchronously, in place (recommended, simple).
  `TF_ES_ASYNCDONTCARE` exists but was useless against the CUAS composition problem.

### 7.2 ★The commit-only engine (jamotong's final form)

**Insert only finalized syllables with `InsertTextAtSelection`. Never draw the syllable
being composed.**

```c
HRESULT DoEditSession(ITfEditSession *This, TfEditCookie ec){
    int cLen = wcslen(data.committed);   // finalized syllable(s)
    if (cLen > 0) {
        ITfInsertAtSelection *ins;
        ctx->lpVtbl->QueryInterface(ctx, &IID_ITfInsertAtSelection, (void**)&ins);
        ITfRange *r = NULL;
        ins->lpVtbl->InsertTextAtSelection(ins, ec, 0, data.committed, cLen, &r);
        if (r) { MoveCaretToEnd(ctx, ec, r); r->lpVtbl->Release(r); }  // caret after insert (avoids reversal)
        ins->lpVtbl->Release(ins);
    }
    return S_OK;   // do nothing for the preedit (composing) syllable
}
```
- The composing syllable appears **when the next syllable begins or a boundary key
  (space/enter) forces a commit** — i.e., one beat late. That is commit-only's one price
  (no preview).
- **Beware the reversal bug**: if you don't move the caret to the **end** of the inserted
  text (`MoveCaretToEnd`), the next insert lands in front and `가나다` becomes `다나가`.
  `Collapse(TF_ANCHOR_END)` the range that `InsertTextAtSelection` returned, then
  `SetSelection`.

### 7.3 (Reference) How you *would* do composition preview — and why we didn't
Two methods, both native-TSF-only:
- **TSF composition**: `ITfContextComposition::StartComposition` → update via
  `ITfRange::SetText` → apply display attributes (underline). **In CUAS apps the
  composition is terminated right after the session (§8.1).**
- **In-place replacement**: insert the composing syllable as plain text, then delete and
  re-insert as it changes (`ShiftStart`+`SetText`). **CUAS apps don't support range
  editing → it accumulates (`ㄱ가간...`) (§8.2).**

→ Both break under CUAS, which is what led to **commit-only**. Details next chapter.

---

## 8. The big lesson

This chapter is the reason this document exists — what jamotong learned over **20+ rounds
of on-device testing.**

### 8.1 In some compatibility hosts, TSF composition dies right after the session
- Our composition setup was textbook-correct: display attribute `TF_ATTR_INPUT`, category
  registered, insert-then-compose, with/without `SetSelection`, sync/async,
  `ITfThreadMgrEventSink`/`ITfTextEditSink` attached — **we tried everything; none of it
  mattered.**
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
  the control. All four profiles still worked in Notepad. The next isolated question is
  whether the compatibility host expects `GUID_PROP_LANGID`, `GUID_PROP_READING`, or both
  on the live composition range; do not fold that unverified behavior into the product.
- **Pitfall**: passing a **NULL `ITfCompositionSink` to `StartComposition` fails with
  `E_INVALIDARG`** (the sink is mandatory, contrary to what MSDN suggests). When we
  accidentally caused that failure, plain insertion-without-composition worked cleanly
  in CUAS — and that accident was the discovery of "commit-only."

### 8.2 CUAS apps can't do "range editing" (the real reason preview is impossible)
- We tried composition-less "in-place replacement" (delete the inserted `ㄱ`, write `가`)
  → **works only in native TSF apps; in CUAS EDIT controls the
  replacement doesn't happen and text accumulates like `ㄱ가간가나낟...`.**
- That is, CUAS apps do not support extending a range backwards with
  `ITfRange::ShiftStart` and overwriting with `SetText`. **Only insertion at the caret works.**
- **∴ Inline composition preview in CUAS apps is structurally impossible via TSF.**

### 8.3 Bypass via an IMM32 IME? — Win11 blocks it
- "Then let's build a proper legacy (IMM32) IME" is the natural detour. We built the
  `.ime` (a DLL) and implemented `ImeInquire`, `ImeProcessKey`, `ImeToAsciiEx`. However:
  - `ImmInstallIME` is a **de-facto retired no-op on modern Windows** (returns NULL,
    GetLastError=0).
  - Registering directly via the registry makes the IME **appear in the settings list but
    grayed out (disabled)** → Win11 **refuses to activate third-party IMM32 IMEs by policy.**
  - Side discovery: `ImmInstallIME` reads the IME's **version resource (VERSIONINFO)** —
    without one you get `1813 ERROR_RESOURCE_TYPE_NOT_FOUND`. (Moot, since it's blocked anyway.)
- **∴ On Win11, a third-party IMM32 IME is a dead end.**

### 8.4 Conclusion: insertion is the only universal → commit-only
The only operation that works in all three app classes is **"insert at the caret."** Therefore:
- TSF composition ❌ (CUAS kills it) → don't use
- Range editing ❌ (CUAS doesn't support it) → don't use
- IMM32 IME ❌ (Win11 blocks it) → don't use
- **Insertion ✅ → insert only finalized syllables = commit-only** → consistent in
  **every app** with no detection, no branching, no orphan characters.

The price: no inline preview (underline) of the syllable being composed. In Korean the
composing syllable is normally just the last character, so it is tolerable in practice.
**If you truly need preview, you'd enable TSF composition for native apps only and fall
back to commit-only for CUAS/terminals** — but telling the two apart requires destructive
detection ("start a composition and watch it die"), which mangles the first keystroke
(an orphan). jamotong chose commit-only because that orphan was unacceptable.

> **Update (v0.12, see §13).** "Insertion is universal" turned out to be *almost* true.
> `InsertTextAtSelection` works everywhere for **native TSF apps and terminals**, but some
> CUAS EDIT controls (RichEdit-family editors) accept it with `hr=0` and **silently do
> nothing**. The final engine therefore picks the injection method **by app class** — TSF
> insert / `EM_REPLACESEL` / `SendInput`. Read §13 before you trust a single path.

### 8.5 Chronicle of trial and error (summary)
1. Inline TSF composition → terminated on every key in CUAS apps and terminals.
   Judged "CUAS limitation."
2. `SendInput` unicode-append fallback → worked in terminals, but EDIT controls overwrote
   synthetic input as if it were composition.
3. Proper IMM32 IME → blocked by Win11 (§8.3).
4. The NULL-sink accident revealed that composition-less insertion works in CUAS.
5. In-place replacement preview → CUAS can't range-edit (accumulation, §8.2).
6. **Commit-only finalized** → consistent in every app. Done.

---

## 9. Extras

### 9.1 Hanja conversion (under the commit-only model)
- The composing syllable is **not in the document** (commit-only), so hanja conversion is
  **insertion, not replacement**: Hanja key → look up the current FSM syllable in the
  dictionary → candidate window (our own popup) → on selection, **insert the chosen
  hanja** (`InsertTextAtSelection`) + reset the FSM. Being insertion, it works everywhere.
- Make the candidate window a popup that never steals focus, and route keys through the
  IME (while the window is up, eat all keys and forward them to its handler).
- Word-level conversion by reading already-typed text and replacing it is **range editing
  → native apps only.** (Alternative that works everywhere: **select the text first, then
  press Hanja** — replacing a selection is the insertion path. See §12.5.)

### 9.2 Non-jamo boundary keys (space/enter/arrows)
When a non-jamo key arrives mid-composition, **flush (commit) the current syllable** and
**resend that key as a real key event** (`SendInput` with the §5.4 magic marker).
Edit-session insertion doesn't reach terminals, but a real resent key is handled natively
by everything — space, enter, arrows, terminals included.

### 9.3 Opening the settings window (ITfFnConfigure)
- The Win11 modern Settings app **does not offer an "Options" button** for third-party
  TIPs (policy). `ITfFnConfigure` survives only for classic dialogs.
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
  and `ITfDisplayAttributeInfo **`. The description string belongs to the separate
  `ITfDisplayAttributeInfo::GetDescription` method. Adding a `BSTR *` parameter unbalances
  the x86 stack and violates the ABI contract on x64 as well.
- **Never pass NULL sink to `StartComposition`**: `E_INVALIDARG`. The sink is mandatory.
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
- **Registry redirection**: a 32-bit process's `HKCR\CLSID` goes to `WOW6432Node`.
  Verify where things actually land.
- **Unicode/locale**: keep sources and strings UTF-16 (`wchar_t`, `-W` APIs). Non-ASCII in
  `.rc` needs windres codepage care (safest: keep resources ASCII).
- **`RequestEditSession` is a synchronous callback**: document edits must happen inside
  that `ec` only.
- **`S_OK` does not guarantee that a composition survived**: the host may call
  `OnCompositionTerminated` during or just after the session. Correlate request/session
  HRESULTs with a transaction generation, termination epoch, and the post-session
  composition state.
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
- [ ] Edit session (`ITfEditSession`) + `InsertTextAtSelection` (finalized syllables only; move caret to end)
- [ ] Non-jamo boundary-key resend (`SendInput` + magic marker)
- [ ] Registration: COM CLSID + `RegisterProfile` + `RegisterCategory(TIP_KEYBOARD)` + install/uninstall.bat
- [ ] Build & register both 32- and 64-bit

**Nice to have**: hanja candidate window, settings (hotkey / separate exe), tray branding icon.
**Not needed if commit-only**: `ITfCompositionSink`, `ITfDisplayAttributeProvider`, composition/underline logic.

---

## 12. Field lessons after commit-only

The story did not end with commit-only (§8). While adding preview, hanja and the tray
icon, **CUAS bit us three more times**. Each lesson below is "problem → cause → final
fix", all verified with on-device logging (v0.9–v0.11).

### 12.1 Composition preview lives *outside* the document — a floating overlay
- **Problem**: the price of commit-only is that the syllable being composed is invisible.
- **Fix**: don't touch the document — **draw the composing syllable yourself in a small
  translucent chip window at the caret** (the same industry-standard fallback as the
  classic IMM32 "default composition window"). Zero document contact = zero CUAS constraints.
- **Implementation notes**:
  - Style: `WS_POPUP` + `WS_EX_LAYERED|NOACTIVATE|TOPMOST|TOOLWINDOW|TRANSPARENT` (click-through).
  - Translucency via **uniform alpha** (`SetLayeredWindowAttributes`) + normal WM_PAINT.
    Per-pixel alpha (`UpdateLayeredWindow`) makes GDI text vanish — GDI doesn't write alpha.
  - Caret-rect fallback chain: `GetActiveView`→`GetTextExt` **inside the same edit session**
    as the commit insert → on failure `GetGUIThreadInfo` (system caret; old EDIT controls
    and many terminals set it) → if both fail, just skip the preview.
  - Create lazily on the input thread; hide on focus change, destroy on Deactivate.

### 12.2 ★CUAS's GetTextExt "succeeds with a stale rect"
- **Problem**: the chip overlaps the just-committed character, or trails the caret by
  exactly one keystroke.
- **Cause**: in CUAS apps the commit insertion is **asynchronous**. Calling `GetTextExt`
  right after the insert — in the same session — **returns S_OK with the pre-insert
  coordinates** (it's not a failure, so no fallback can catch it!). Native apps are
  synchronous and return the advanced rect immediately.
- **Final fix — staleness detection (accumulating compensation)**:
  1. Remember the **raw rect** every time you draw the chip.
  2. "A commit happened this event, **and** the rect equals the previous raw rect" =
     stale → draw shifted right by one full-width advance (~line height), **accumulated**.
  3. Reset the accumulator when the rect actually moves.
  - Accumulation also covers fast typing (rect frozen across several keys); in native
    apps the rect always moves, so the compensation **never fires** (no false positives).
  - Pitfall: store the **raw** rect for comparison — storing the compensated one breaks
    the next comparison.

### 12.3 ★The vanishing-last-syllable incident — racing your own synthetic key
- **Problem**: in CUAS apps the **last syllable of a word intermittently disappears**
  (the 국 of 대한민국 one day, the 라 of 가나다라 the next). Content-independent,
  timing-dependent.
- **Diagnosis**: with logging in place, **every `InsertTextAtSelection` returned S_OK** —
  IME→CUAS always succeeded. The loss always coincided with
  "flush (insert last syllable) + `SendInput` boundary-key resend".
- **Cause**: the synthetic boundary key (hardware queue) **races CUAS's result-character
  delivery inside the app** — when the key wins, the pending result character is dropped.
- **Final fix**: **space is a character.** When space arrives mid-composition, don't
  resend it — insert **"syllable + space" in one edit session** (one delivery channel,
  nothing left to race). Enter/arrows remain resent as real keys (they're control keys:
  terminals, auto-indent, etc. need native handling).
- **Lesson**: **never mix the two delivery channels (edit-session insert + SendInput) in
  a single key event.** CUAS gives you no ordering guarantee between them.

### 12.4 The tray input-indicator mode icon (MS IME's 한/A)

This is the small icon in the tray that shows the IME's current state and updates live. It
is **not** the profile branding icon (§4.2) — it is a run-time *language-bar item* your TIP
adds while active. The mechanism looks intimidating in the docs, but it's five small pieces.

**The recipe — what actually makes the icon appear:**

1. **Implement one language-bar item** — an object exposing `ITfLangBarItemButton`
   (which is `ITfLangBarItem` + the button methods). Only a handful of methods do real work;
   the rest return `S_OK`/`E_NOTIMPL`.
2. **Add it when the TIP activates, remove it when it deactivates.** In `ActivateEx`, get
   `ITfLangBarItemMgr` from the thread manager and `AddItem`; in `Deactivate`, `RemoveItem`.
   ```c
   ITfLangBarItemMgr *mgr;
   ptim->lpVtbl->QueryInterface(ptim, &IID_ITfLangBarItemMgr, (void**)&mgr);
   mgr->lpVtbl->AddItem(mgr, (ITfLangBarItem*)myLangBarItem);   // Deactivate: RemoveItem
   ```
3. **In `GetInfo`, set exactly two things that matter** — the item's identity and style:
   ```c
   pInfo->guidItem = GUID_LBI_INPUTMODE;                          // ★must be this GUID
   pInfo->dwStyle  = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;  // ★shown in tray
   pInfo->clsidService = CLSID_Ours;  lstrcpyW(pInfo->szDescription, L"...");
   ```
   - `GUID_LBI_INPUTMODE = {2C77A81E-41CC-4178-A3A7-5F8A987568E6}` — **not in MinGW headers,
     define it yourself.** On Win8+ the tray indicator **hosts only an item with this
     guidItem**; any other GUID appears only in the legacy desktop language bar (this is the
     one thing that silently costs people a day: `AddItem` succeeds, nothing shows).
   - `TF_LBI_STYLE_SHOWNINTRAY` is what puts it in the tray indicator.
4. **In `GetIcon`, return the current `HICON`.** The shell takes ownership and destroys it,
   so hand back a fresh icon each call (e.g. draw the current layout's abbreviation). Return
   `S_FALSE` with `*phIcon = NULL` if you have nothing yet (e.g. after Deactivate).
5. **When state changes, tell the shell to re-query** via the sink you were handed in
   `AdviseSink`: `sink->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT)`. That single call is the entire
   "refresh the icon" path.

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
  selection** — that is the same insertion path apps use for type-over, so
  **it works in CUAS apps too**.
- **Result**: the "select text → press Hanja" UX gives you **word-level hanja conversion
  in every app**. (The classic read-before-caret + `ShiftStart` replacement stays
  native-only — §8.2.)
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
- **Change one variable per profile.** The AkelPad suite uses four independent
  CLSID/profiles: (0) control, (1) only `TF_AE_NONE`, (2) only insert-before-compose,
  and (3) only omission of explicit `SetSelection`. Combining them in one DLL could make
  input work without revealing why.
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
  hypothesis. LANGID presence, property order, and repeated writes remain confounded and
  are isolated by R3 (§12.7.8).
- Keep a comparison build separate from the installed IME: distinct DLL/display names,
  CLSID, and profile GUID, plus one JSONL file per process. Run one fixed scenario in
  Notepad and once in the failing application.
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
| **Our implementation defect** | ABI, HRESULT, lifetime, or state publication violates the contract | wrong vtable signature, NULL composition sink, consuming a key after edit failure | fix it, then restart the same scenario from a clean deployment |
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

| Defect or bad assumption | Symptom | Correction and verdict |
|---|---|---|
| Declaring a nonexistent extra `BSTR *` parameter in the C vtable for `ITfDisplayAttributeProvider::GetDisplayAttributeInfo` | COM registers/stack can be misaligned: ABI-level undefined behavior | changed to the documented three-parameter method; AkelPad still terminated with the fixed build |
| Passing NULL as the `StartComposition` sink | `E_INVALIDARG`; no composition is created, while direct committed insertion can look like a partial success | pass a real `ITfCompositionSink`; check both HRESULT and returned pointer |
| Treating `StartComposition == S_OK` as sufficient even if the returned composition is NULL | later range failure or divergence between local and document state | treat `S_OK + NULL` as explicit failure and roll back before publishing state |
| Treating the `RequestEditSession` result, callback `phrSession`, and inner operation result as one HRESULT | a successful request can hide a failed or unexecuted session | retain `request_hr`, `session_hr`, `inner_hr`, and `callback_ran` separately |
| Publishing the FSM and keeping `eaten=TRUE` after document-edit failure | the app never receives the key while internal state advances, leaving scattered or stale jamo | publish only after edit success; otherwise clear the composition and pass the original key. AkelPad later still failed with zero session failures |
| Reading compartment `VT_EMPTY` as “off” | Hangul/English toggle works only once in hosts such as PuTTY | treat it as “unset” and retain local fallback state; unrelated to composition termination |
| Swapped capability-GUID labels and a malformed immersive GUID | registration/activation conclusions can be confused on some environments | corrected to Microsoft constants. The desktop AkelPad profile already loaded, so this is unlikely to explain per-key termination, but it removes a confounder |
| Suspecting the x86 DLL or COM bitness | a 64-bit application might have loaded a different server | confirmed 64-bit AkelPad and verified PE32+ x86-64 DLLs and required exports; symptom unchanged |
| Overwriting a TSF DLL while it remains mapped in multiple processes | old and new symptoms appear to alternate like ghosts | use independent identities and trace stems; close editors, unregister, and log out when necessary |
| Deleting every temporary JSONL before collection | one old file held open by another process aborts the test | never delete globally; copy only matching logs modified after a run marker, skipping a locked candidate individually |
| Reading `%TARGET%` immediately after `set /p` in the same parenthesized `cmd.exe` block | the pasted AkelPad path is evaluated as the old empty value | moved input/use outside the block, then eliminated manual input by using the default install location |

These were not worthless fixes. Removing them made the later trace trustworthy. But because
the same out-of-session termination remained **after** each correction, continuing to use
them as a sufficient explanation for current AkelPad jamo separation would waste time.

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

Meta R2 corrects all four defects and uses new CLSID, profile, display-attribute, and trace
identities without changing the product or the v1 field artifact. Its build and package
checks pass, but Windows field execution is still pending; this is not a successful metadata
hypothesis result.

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
| **Confirmed** | AkelPad and Notepad text stores have different lifetime behavior | AkelPad creates a composition per key; Notepad reuses the existing composition |
| **Strong inference** | AkelPad's TSF context interacts with an IMM32 compatibility path that rewrites text/composing/attribute state into a result and ends composition | `TF_SS_TRANSITORY`, AkelEdit's direct `WM_IME_*`/`ImmGetCompositionStringW` handling, and simultaneous post-session property changes. The trace cannot identify the actor or internal CUAS decision |
| **Open hypothesis** | metadata used by MS IME—language, reading, ownership, or related range state—is missing from the live range | real LANGID/READING application and observation never occurred because both GUIDs were wrong. Official definitions describe data semantics, not a universal “required to survive” rule |
| **Rejected as sufficient** | active-end style, explicit selection, insert-first order, immediate API failure, or application bitness | isolated A/B or binary verification leaves the result unchanged |
| **Not knowable from current evidence** | “AkelPad itself terminated it,” “CUAS is conclusively the bug,” or “MS IME uses a secret API” | the trace has no caller stack or compatibility-layer internals |

The most accurate current statement is therefore: **after a successful TIP edit, a second
edit on the compatibility-host side ends the composition**. Shortening this to “CUAS is the
culprit” turns an inference into a fact.

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

The corrected `make akel-metadata-r2-suite` build implements requirements 1–4. Production
initializers and a portable executable test share the official GUID bytes, while every
schema-2 update separates:

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

#### 12.7.8 Corrected R2 field result and the R3 interaction suite

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

R3 uses new identities and schema 3 to isolate those variables:

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
A cell requires both the visible result and a passing `validation.json`.

If Reading→LANGID also fails, LANGID presence becomes the leading candidate; if only that
reversed order passes, order becomes the leading candidate. If the once profile recovers
after the first key, repeated writes are implicated; if it stays broken, the first LANGID
write may leave context-lifetime state. No single AkelPad run is enough to turn any of
these outcomes into a universal product policy.

#### 12.7.9 Time-saving procedure for the next implementation

1. **Build a control first.** Pass a minimal TIP in a native-TSF control such as Notepad.
2. **Use fixed input.** Same key sequence, fresh document, fresh process, comparable speed.
3. **Collect visible output and structural trace together.** Either one alone can mislead.
4. **Separate request, session, and inner results.** Never declare success from the
   `RequestEditSession` HRESULT alone.
5. **Record survival after success.** Put callback end, transaction close, `OnEndEdit`,
   and `OnCompositionTerminated` on one sequence axis.
6. **Change one variable per profile.** Separate DLL, CLSID, profile, and trace stem too.
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

Repeating `TF_AE_NONE`, omitting `SetSelection`, or insert-first **unchanged and in
isolation** has low value now. A rerun should introduce a genuinely distinct variable or
new observation, such as range ownership or metadata lifetime.

---

## 13. Text injection per app class

§8 concluded "insertion is the only universal, so commit-only." A later round of on-device
testing (v0.12) forced a sharper conclusion: **even `InsertTextAtSelection` is not
universal.** Getting text reliably into *every* app took a per-app-class strategy plus a
cluster of related fixes. This chapter is that strategy and the incidents that produced it.

### 13.1 ★★The core reversal — a RichEdit-family control accepts a TSF insert with hr=0 and drops it

- **Problem**: in a **RichEdit-family editor control reached via the CUAS bridge**, typing
  Hangul worked for the first few characters, then some syllables simply **never appeared**;
  a repeated character was **swallowed entirely**; a selected 4-char word converted to hanja
  came out with only its **first two characters** replaced. Fully native TSF editors were fine.
- **Diagnosis (from the log)**: every `INSERT` line showed `hr=0x00000000` — success — yet
  the captured caret rect **stayed frozen** (`CHIP raw=(14,825…)` across three inserts while
  the "advance" compensation ran up 29→58→87). Native apps advance the rect; this control
  reported success and moved nothing. **The insert was a no-op.**
- **Cause**: `InsertTextAtSelection` is honored by native TSF apps and by terminals (via the
  IMM bridge), but **some CUAS EDIT controls silently ignore it** while returning `S_OK`.
  There is no error to branch on.
- **Fix — inject by app class.** For EDIT-family controls, drive the control directly with
  `EM_REPLACESEL` (the standard EDIT message; an empty selection means "insert at caret").
  Keep the TSF session for everything else.

```c
// Is the focused control an EDIT-family control? Return its HWND, else NULL.
// Require the class name to contain "edit" so a self-rendering terminal that
// happens to answer EM_GETSEL is not mistaken for an editor (see §13.3).
HWND FocusEditWindow(void){
    GUITHREADINFO gti = { sizeof gti };
    if (!GetGUIThreadInfo(0,&gti) || !gti.hwndFocus) return NULL;
    HWND h = gti.hwndFocus;
    wchar_t cls[64]; int n = GetClassNameW(h, cls, 64);
    if (n<=0) return NULL;
    for (int i=0;i<n;i++) cls[i]=towlower(cls[i]);
    if (!wcsstr(cls, L"edit")) return NULL;          // e.g. Edit, RICHEDIT50W, RichEdit-family
    CHARRANGE cr = {-2,-2};
    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);     // RichEdit-family controls answer this
    if (cr.cpMin >= 0) return h;
    DWORD s=~0u,e=~0u; SendMessageW(h, EM_GETSEL,(WPARAM)&s,(LPARAM)&e);  // plain EDIT
    return (s!=~0u) ? h : NULL;
}

// Commit finalized text. One choke point used by every commit path.
void CommitText(Svc *svc, ITfContext *pic, const wchar_t *str){
    HWND edit = FocusEditWindow();
    if (edit){ SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)str);  // EDIT-family: reliable
               svc->lastCaretValid = FALSE; }                        // overlay uses system caret
    else      RequestEditSessionInsert(svc, pic, str);               // native/terminal: TSF
}
```

- Now the three app classes map to three injection methods:

  | App class | Detect | Inject finalized text |
  |---|---|---|
  | Native TSF (fully TSF-aware editor) | not EDIT-class | `InsertTextAtSelection` (TSF session) |
  | CUAS EDIT (plain EDIT / RichEdit-family via the bridge) | class name has `edit` | **`EM_REPLACESEL`** |
  | Self-rendering terminal (own renderer) | not EDIT-class | `InsertTextAtSelection` (works via IMM bridge) |

- **Lesson**: "it returned `S_OK`" is **not** "it happened." When output silently fails in
  one app family, stop trusting the API's return and **verify by observing document/caret
  movement** — then drive that family with its own native mechanism (`EM_REPLACESEL`).

### 13.2 Composing + an app shortcut (Ctrl/Alt/Win) — flush *in the test phase*

- **Problem**: pressing Ctrl+S mid-composition saved the document **without the last
  syllable** — the commit-only engine had it in the FSM, not in the document.
- **First (wrong) fix**: eat the combo in `OnTestKeyDown`, then in `OnKeyDown` flush the
  syllable and **re-send the key** with `SendInput`. On device, hammering Ctrl+C/Ctrl+V in
  a native editor produced **ㅊ and ㅍ** — the injected `C`/`V` landed *behind* already-queued user
  input (the Ctrl-up, the next key), so it was processed **without Ctrl** and interpreted as
  a jamo. Synthetic re-send fundamentally races subsequent user input; it is **wrong for
  modifier combos.**
- **Final fix — flush-in-test, no injection**: if a composition is active and Ctrl/Alt/Win
  is down, **commit the syllable right there in `OnTestKeyDown` (synchronous edit session)
  and do *not* eat the key.** The app then handles the shortcut natively, on its own timing,
  with the original event. No injection ⇒ no race. The only side effect is an early commit.

```c
// OnTestKeyDown
if (HasCtrlAltWin()){
    if (fsm.state != EMPTY){                 // composing → finalize now, synchronously
        FsmResult r = { Fsm_Flush(&fsm), 0 };
        OutputResult(obj, pic, r);           // commit via §13.1 CommitText
    }
    *pfEaten = FALSE;                         // pass the shortcut through untouched
    return S_OK;
}
```

### 13.3 Boundary-key resend — PostMessage to the app queue, not SendInput

- **Problem**: a boundary key that *must* be a real key (Enter, arrows — terminals need
  them, EDIT controls auto-indent on them) still lost the preceding syllable in CUAS apps,
  **even after delaying the `SendInput` resend by 30 ms**.
- **Cause**: `SendInput` posts to the **system input queue**; CUAS delivers the committed
  character into the **app's message queue**. They are different queues with **no ordering
  guarantee** — delay does not fix ordering.
- **Fix**: for an EDIT-family focus window, **post the key straight to that window's message
  queue** with `PostMessage(WM_KEYDOWN/WM_KEYUP)`. Same queue, FIFO ⇒ the key lands *after*
  the committed character. Terminals (non-EDIT) keep `SendInput` (verified on a self-rendering terminal).

```c
void ResendKey(WPARAM vk, LPARAM lp){
    HWND edit = FocusEditWindow();
    if (edit){
        LPARAM base = lp & 0x01FF0000;                        // scancode + extended bit
        PostMessageW(edit, WM_KEYDOWN, vk, base | 1);
        PostMessageW(edit, WM_KEYUP,   vk, base | 0xC0000001);
    } else SendKeyThrough(vk, lp);                            // terminal: system queue
}
```
- **Lesson**: a CUAS ordering race is fixed by using the **same queue**, not by *delaying*
  the other queue. (Space is still handled as §12.3 — "syllable+space" in one insert — which
  sidesteps the resend entirely.)

### 13.4 Reading and replacing a selection — the EM_EXGETSEL ladder

Selection-based hanja (§12.5) reads the selection, and word conversion replaces a run before
the caret. Both need the selection API — and **some RichEdit-family controls do not answer the
classic `EM_GETSEL`; only the RichEdit `EM_EXGETSEL`.** So every selection op tries the RichEdit
message first, then the plain-EDIT message:

- **Read selection**: `EM_EXGETSEL`+`EM_GETSELTEXT` (the control copies the selected text
  itself — offset-unit agnostic) → else `EM_GETSEL`+`WM_GETTEXT` (plain EDIT, char offsets).
  The first path also fixed "대한민국 read as 대한": with `WM_GETTEXT` we sliced by an offset
  whose unit the control disagreed about; `EM_GETSELTEXT` hands back the exact text.
- **Replace a word before the caret** (CUAS-safe, replaces the classic native-only
  `ShiftStart`): get the caret via `EM_EXGETSEL` (fallback `EM_GETSEL`), select the run with
  `EM_EXSETSEL`/`EM_SETSEL`, **read it back and verify it equals the expected word** (don't
  trust offset units — try char then UTF-16-unit spans), then `EM_REPLACESEL`.

```c
// caret position, RichEdit first
CHARRANGE cr = {-2,-2}; LONG caret; BOOL rich;
SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);
if (cr.cpMin>=0 && cr.cpMin==cr.cpMax){ caret=cr.cpMin; rich=TRUE; }
else { DWORD s,e; SendMessageW(h,EM_GETSEL,(WPARAM)&s,(LPARAM)&e);
       if (s!=e) return FALSE; caret=e; rich=FALSE; }
// select [caret-span, caret], verify text, then EM_REPLACESEL(h, TRUE, hanja)
```
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
- **Fix**: on the "repeat" branch, cross-check the **physical** key state and self-heal a
  stale flag.

```c
if (c->keyDown[vk]){                       // looks like a repeat…
    if (GetKeyState(vk) & 0x8000) return REPEAT;   // really held → genuine repeat
    c->keyDown[vk] = false;                // key-up was lost → clear the ghost flag
    if (c->activeKeys>0) c->activeKeys--;  // …and treat this as a fresh press
}
```
- Plus two escape hatches, both routed through one reset function (§13.6): **Esc cancels the
  composition** (MS-IME convention; sequential *and* moa-chigi), and **Backspace clears a
  moa-chigi composition** (the old gate tested `fsm.state`, which moa-chigi never sets).

### 13.6 Keep the original on cancel — commit *then* replace

- **Problem**: type 가, press the Hanja key, leave the candidate window **without choosing**
  → the 가 **disappeared**. The composing syllable was never in the document; the engine had
  planned to *insert* the hanja over nothing.
- **Fix**: on Hanja-key while composing, **commit the syllable to the document first**
  (making it `replaceLen=1`, targeting that just-committed char), then convert by *replacing*
  it. Cancelling leaves the committed original in place — the same pattern as word conversion.

### 13.7 One reset choke point + a captured target window

Two small structural fixes that prevented whole classes of the above:

- **`ResetComposition()` — a single function** that clears the FSM, the chord state, the
  chip state and hides the overlay. Focus change, Deactivate, Esc, backspace-clear and
  hanja-commit all call it. Preview *show* stays solely in `OutputResult`. When cleanup is
  scattered across many handlers, a broken composition leaves the chip pinned somewhere
  (§13.5) — centralizing it kills that class of bug.
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
class of bug (§13.5, §13.7). The contract jamotong uses:

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
| Text Services Framework (개요) | https://learn.microsoft.com/en-us/windows/win32/tsf/text-services-framework |
| TSF Architecture (구조) | https://learn.microsoft.com/en-us/windows/win32/tsf/architecture |
| Using Text Services Framework | https://learn.microsoft.com/en-us/windows/win32/tsf/using-text-services-framework |
| TSF Reference (전체 목록) | https://learn.microsoft.com/en-us/windows/win32/tsf/text-services-framework-reference |
| msctf.h (인터페이스 색인) | https://learn.microsoft.com/en-us/windows/win32/api/msctf/ |

### Interfaces this manual uses

| Interface | Used for | Link |
|---|---|---|
| `ITfTextInputProcessor` | TIP itself (Activate/Deactivate) — §3 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itftextinputprocessor |
| `ITfTextInputProcessorEx` | activation flags extension | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itftextinputprocessorex |
| `ITfThreadMgr` | thread manager — the entry point to everything | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfthreadmgr |
| `ITfKeystrokeMgr` | advise the key sink — §5 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfkeystrokemgr |
| `ITfKeyEventSink` | receiving keys — **watch vtbl order** §5.1 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfkeyeventsink |
| `ITfEditSession` | edit session — §7 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfeditsession |
| `ITfContext` | document context | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfcontext |
| `ITfInsertAtSelection` | **insertion (the heart of commit-only)** — §8.4 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfinsertatselection |
| `ITfRange` | range — what CUAS blocks editing on §8.2 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfrange |
| `ITfComposition` | composition — external termination in some compatibility hosts §8.1 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfcomposition |
| `ITfCompositionSink` | composition-end notification — **NULL makes it fail** | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfcompositionsink |
| `ITfCompositionSink::OnCompositionTerminated` | correlate external termination — §12.6 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcompositionsink-oncompositionterminated |
| `ITfContextComposition` | starting a composition | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfcontextcomposition |
| `ITfTextEditSink::OnEndEdit` | observe edit completion order — §12.6 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itftexteditsink-onendedit |
| `ITfEditRecord::GetTextAndPropertyUpdates` | observe text/property changed-range presence without contents — §12.6 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfeditrecord-gettextandpropertyupdates |
| `ITfContext::InWriteSession` | determine whether our client owns a write lock during notification — §12.7 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-inwritesession |
| `ITfContext::GetProperty` | obtain a predefined/custom property object — §12.7 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-getproperty |
| `ITfProperty::SetValue` | set LANGID/READING on a nonempty range — §12.7 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfproperty-setvalue |
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

### IMM32 and app compatibility (§8.3, §13)

| Document | Link |
|---|---|
| Input Method Manager (IMM32) | https://learn.microsoft.com/en-us/windows/win32/intl/input-method-manager |
| `WM_IME_STARTCOMPOSITION` | https://learn.microsoft.com/en-us/windows/win32/intl/wm-ime-startcomposition |
| `EM_EXGETSEL` (reading selection, §13.4) | https://learn.microsoft.com/en-us/windows/win32/controls/em-exgetsel |
| `SendInput` (§13.3 — do not use) | https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput |
| `PostMessage` (§13.3 — use this) | https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postmessagew |

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

*This manual's conclusion (commit-only) and its rationale (§8) were earned through 20+
rounds of on-device verification. If you are building another IME, read the table in
§2.2 and §8 first — they will save you weeks.*
