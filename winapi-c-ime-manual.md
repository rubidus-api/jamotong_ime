# Building a Windows Korean IME with Nothing but WinAPI and C — A Field Manual

[한국어](winapi-c-ime-manual.ko.md) | **English**

*Last updated: 2026-07-08 (v0.11.0 — added §12 "Field lessons after commit-only", expanded §10 gotchas)*

This document explains how to build a Korean input method (IME) for Windows from
scratch **in pure C (C23) and the Win32 API only** — no C++, no ATL/MFC, no frameworks —
including the trial-and-error record from the `jamotong` project and the final answers
we arrived at.

Audience: developers who know WinAPI and C but are new to COM/TSF.
Goal: to let you build another IME from the ground up using this document alone.

> **One-line conclusion (spoiler)**: Windows has two input systems (TSF and IMM32) with a
> shaky bridge between them (CUAS). **The only text operation that works in every app
> without exception is "insert at the caret."** So we gave up inline composition preview
> (the underlined text) and settled on a **"commit-only"** engine that inserts only
> completed syllables — the most robust universal solution. Why that is so is the heart
> of this document (§8).

---

## Table of contents
0. [Prerequisites](#0-prerequisites)
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

| App class | Examples | TSF composition (underline preview) | Range editing (replace inserted text) | Plain insertion |
|---|---|:---:|:---:|:---:|
| **Native TSF** | new Notepad, WordPad, browsers | ✅ | ✅ | ✅ |
| **CUAS EDIT controls** | AkelPad, old Win32 EDIT, KakaoTalk | ❌ killed right after the session | ❌ accumulates | ✅ |
| **Terminals (own renderer)** | PuTTY | ❌ | ❌ | ✅ (roughly) |

- Only **native TSF** apps support composition and replacement.
- **CUAS/terminals** support **"insert at the caret" only.** You cannot delete or replace
  what you already inserted.
- **The common denominator of all three = insertion, alone.** → This is where §8's
  "commit-only" comes from.

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

### 8.1 In CUAS apps, TSF composition dies right after the session
- Our composition setup was textbook-correct: display attribute `TF_ATTR_INPUT`, category
  registered, insert-then-compose, with/without `SetSelection`, sync/async,
  `ITfThreadMgrEventSink`/`ITfTextEditSink` attached — **we tried everything; none of it
  mattered.**
- Confirmed by logging: `StartComposition` succeeds (hr=0) and the composition text goes
  in, but **immediately after the edit session ends** CUAS fires
  `OnCompositionTerminated`, finalizing and killing the composition. We never identified
  the reason (undocumented CUAS internals; MS IME manages it, but its trick is not
  observable).
- **Pitfall**: passing a **NULL `ITfCompositionSink` to `StartComposition` fails with
  `E_INVALIDARG`** (the sink is mandatory, contrary to what MSDN suggests). When we
  accidentally caused that failure, plain insertion-without-composition worked cleanly
  in CUAS — and that accident was the discovery of "commit-only."

### 8.2 CUAS apps can't do "range editing" (the real reason preview is impossible)
- We tried composition-less "in-place replacement" (delete the inserted `ㄱ`, write `가`)
  → **works only in native apps (Notepad); in CUAS EDIT controls (AkelPad) the
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
> CUAS EDIT controls (AkelPad's AkelEdit) accept it with `hr=0` and **silently do nothing**.
> The final engine therefore picks the injection method **by app class** — TSF insert /
> `EM_REPLACESEL` / `SendInput`. Read §13 before you trust a single path.

### 8.5 Chronicle of trial and error (summary)
1. Inline TSF composition → terminated on every key in CUAS apps (PuTTY, AkelPad).
   Judged "CUAS limitation."
2. `SendInput` unicode-append fallback → worked in PuTTY, but EDIT controls overwrote
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
    and PuTTY set it) → if both fail, just skip the preview.
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
- **Problem**: you implement `ITfLangBarItemButton`, AddItem succeeds — and nothing shows
  in the tray.
- **Cause**: **on Win8+ the input indicator ignores any language-bar item whose guidItem
  is not `GUID_LBI_INPUTMODE`** (stated in the TF_LANGBARITEMINFO docs). Custom-GUID items
  only appear in the legacy desktop language bar.
  - `GUID_LBI_INPUTMODE = {2C77A81E-41CC-4178-A3A7-5F8A987568E6}` — not in MinGW headers;
    define it yourself.
- **The truth about right-click**: with `TF_LBI_STYLE_BTN_BUTTON`, **right-clicks also
  arrive as `OnClick(TF_LBI_CLK_RIGHT)`** — the `InitMenu`/`ITfMenu` COM path is
  BTN_MENU-only, and BTN_MENU opens the menu on left-click too (killing "left-click =
  action"). For left-click action + right-click menu:
  ```
  OnClick(RIGHT, point):
      CreatePopupMenu + InsertMenuItem            // build the menu yourself
      clamp point.x to the monitor work area
      cmd = TrackPopupMenu(TPM_NONOTIFY|TPM_RETURNCMD|TPM_LEFTBUTTON,
                           point, owner=GetFocus())   // ★owner required, NONOTIFY required
      dispatch cmd
  ```
- Icon guidelines (MS requirement): **black & white only** (white glyph with dark
  outline / dark badge), 16px base with DPI scaling. Refresh via
  `ITfLangBarItemSink::OnUpdate(TF_LBI_ICON)`.

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
- The dead compositions (§8), the vanishing syllable (§12.3) and the lagging chip (§12.2)
  were all solved by **file logging to %TEMP%**. Log: vk, FSM state, commit/preedit
  chars, `INSERT` hr and range pointer, rects and their source.
- **Warning**: an IME log records **everything the user types in every app**. Never ship
  a diagnostic build; delete logs after testing; isolate logging behind `#ifdef` so
  release builds compile it to a no-op.
- **The stale-DLL trap, again**: when symptoms appear and vanish inexplicably, suspect
  deployment before code (§10's file locking — it played ghost twice in this project).

---

## 13. Text injection per app class

§8 concluded "insertion is the only universal, so commit-only." A later round of on-device
testing (v0.12) forced a sharper conclusion: **even `InsertTextAtSelection` is not
universal.** Getting text reliably into *every* app took a per-app-class strategy plus a
cluster of related fixes. This chapter is that strategy and the incidents that produced it.

### 13.1 ★★The core reversal — AkelEdit accepts a TSF insert with hr=0 and drops it

- **Problem**: in AkelPad (whose editor is the RichEdit-family control *AkelEdit*), typing
  Hangul worked for the first few characters, then some syllables simply **never appeared**;
  a repeated character was **swallowed entirely**; a selected 4-char word converted to hanja
  came out with only its **first two characters** replaced. Notepad and KakaoTalk were fine.
- **Diagnosis (from the log)**: every `INSERT` line showed `hr=0x00000000` — success — yet
  the captured caret rect **stayed frozen** (`CHIP raw=(14,825…)` across three inserts while
  the "advance" compensation ran up 29→58→87). Native apps advance the rect; AkelEdit
  reported success and moved nothing. **The insert was a no-op.**
- **Cause**: `InsertTextAtSelection` is honored by native TSF apps and by terminals (via the
  IMM bridge), but **some CUAS EDIT controls silently ignore it** while returning `S_OK`.
  There is no error to branch on.
- **Fix — inject by app class.** For EDIT-family controls, drive the control directly with
  `EM_REPLACESEL` (the standard EDIT message; an empty selection means "insert at caret").
  Keep the TSF session for everything else.

```c
// Is the focused control an EDIT-family control? Return its HWND, else NULL.
// Require the class name to contain "edit" so a self-rendering terminal (PuTTY) that
// happens to answer EM_GETSEL is not mistaken for an editor (see §13.3).
HWND FocusEditWindow(void){
    GUITHREADINFO gti = { sizeof gti };
    if (!GetGUIThreadInfo(0,&gti) || !gti.hwndFocus) return NULL;
    HWND h = gti.hwndFocus;
    wchar_t cls[64]; int n = GetClassNameW(h, cls, 64);
    if (n<=0) return NULL;
    for (int i=0;i<n;i++) cls[i]=towlower(cls[i]);
    if (!wcsstr(cls, L"edit")) return NULL;          // Edit / RICHEDIT50W / AkelEditW
    CHARRANGE cr = {-2,-2};
    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);     // RichEdit/AkelEdit answer this
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
  | Native TSF (Notepad, modern) | not EDIT-class | `InsertTextAtSelection` (TSF session) |
  | CUAS EDIT (AkelPad, plain EDIT, KakaoTalk) | class name has `edit` | **`EM_REPLACESEL`** |
  | Self-rendering terminal (PuTTY) | not EDIT-class | `InsertTextAtSelection` (works via IMM bridge) |

- **Lesson**: "it returned `S_OK`" is **not** "it happened." When output silently fails in
  one app family, stop trusting the API's return and **verify by observing document/caret
  movement** — then drive that family with its own native mechanism (`EM_REPLACESEL`).

### 13.2 Composing + an app shortcut (Ctrl/Alt/Win) — flush *in the test phase*

- **Problem**: pressing Ctrl+S mid-composition saved the document **without the last
  syllable** — the commit-only engine had it in the FSM, not in the document.
- **First (wrong) fix**: eat the combo in `OnTestKeyDown`, then in `OnKeyDown` flush the
  syllable and **re-send the key** with `SendInput`. On device, hammering Ctrl+C/Ctrl+V in
  Notepad produced **ㅊ and ㅍ** — the injected `C`/`V` landed *behind* already-queued user
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
  the committed character. Terminals (non-EDIT) keep `SendInput` (verified in PuTTY).

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
the caret. Both need the selection API — and **AkelEdit does not answer the classic
`EM_GETSEL`; only the RichEdit `EM_EXGETSEL`.** So every selection op tries the RichEdit
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
- **Lesson**: RichEdit-family controls (incl. AkelEdit, modern Notepad, chat inputs) may
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

---

## Appendix: jamotong source map

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
| Codepoint input popup | `src/code_input.c` |
| Tray monitor / settings app | `src/tray_app.c` |
| (dead) IMM32 IME attempt | `src/imm/` |


*This manual's conclusion (commit-only) and its rationale (§8) were earned through 20+
rounds of on-device verification. If you are building another IME, read the table in
§2.2 and §8 first — they will save you weeks.*
