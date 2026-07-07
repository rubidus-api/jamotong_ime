# Building a Windows Korean IME with Nothing but WinAPI and C — A Field Manual

[한국어](winapi-c-ime-manual.ko.md) | **English**

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
  press Hanja** — replacing a selection is the insertion path.)

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
| (dead) IMM32 IME attempt | `src/imm/` |


*This manual's conclusion (commit-only) and its rationale (§8) were earned through 20+
rounds of on-device verification. If you are building another IME, read the table in
§2.2 and §8 first — they will save you weeks.*
