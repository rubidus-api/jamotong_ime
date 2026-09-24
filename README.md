# Jamotong (자모통)

[한국어](README.ko.md) | **English**

**A Korean (Hangul) IME for Windows in pure C23 + WinAPI** — a TSF (Text Services
Framework) text service with no frameworks and no external libraries.

## Download

| | Latest release (direct download) |
|---|---|
| **Jamotong installer** | **[jamotong-0.50.1.zip](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.50.0/jamotong-0.50.1.zip)** — extract anywhere, run `install.bat` as administrator |
| Input-list repair tool | [jamotong-ime-list-repair-0.18.0.zip](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.18.0/jamotong-ime-list-repair-0.18.0.zip) — when Win+Space shows IMEs you never installed (README inside) |
| Japanese dictionary pack (demo) | [jamotong-japanese-demo-0.49.0.zip](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.49.0/jamotong-japanese-demo-0.49.0.zip) — experimental Japanese input, 50,000 entries (~0.5 MB) |
| Japanese dictionary pack (full) | [jamotong-japanese-full-0.49.0.zip](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.49.0/jamotong-japanese-full-0.49.0.zip) — the same, 500,000 entries (~5 MB) |
| Chinese dictionary pack (demo) | [jamotong-chinese-demo-0.49.0.zip](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.49.0/jamotong-chinese-demo-0.49.0.zip) — experimental pinyin input, 50,000 entries (~0.5 MB) |
| Chinese dictionary pack (full) | [jamotong-chinese-full-0.49.0.zip](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.49.0/jamotong-chinese-full-0.49.0.zip) — the same, 470,000 entries (~4 MB) |

All versions and release notes: [Releases](https://github.com/rubidus-api/jamotong_ime/releases)

### Where it installs

`install.bat` puts the program in **`C:\Program Files\Jamotong`** — a fixed, machine-wide
folder that every app, including Store (UWP) apps, can read. Your settings stay per user in
`%APPDATA%\Jamotong`. The zip you extracted is only the source: delete it after installing.

- **Upgrade**: extract the new zip and run its `install.bat` again (as administrator). If
  anything fails — a file in use, a registration error — the previous version is put back.
- **Earlier installs** (an extracted folder that was registered in place, or the former
  per-user copy in `%LocalAppData%\Programs\Jamotong`) are moved over automatically: the
  registration switches to `Program Files` and the old per-user copy is removed.
  After such a move the tray icon may show plain "한글" until Explorer restarts — answer **Y**
  to the restart question at the end, or sign out and in.
- The release binaries are **not code-signed yet**, so SmartScreen may warn before the first
  run.

## Why this IME

- No dependencies. C and the Win32 API only — no framework, no runtime, no external
  libraries. The product is two statically linked DLLs (64/32-bit) plus data files.
  Runs on Windows 10 and later.
- It picks the input path per host at runtime. Standard TSF documents get inline
  underlined composition; legacy (CUAS) documents get commit-only insertion with a
  floating preview. The decision key is the document-status flag the host itself
  declares, not the application name. Verified on device against Notepad, AkelPad,
  PuTTY, and KakaoTalk.
- A pass-through (direct input) mode. When enabled, no key is intercepted at all, so
  you can type Korean with the remote machine's IME across a remote-desktop session.
  Toggled from the tray menu or a shortcut.
- Layouts are data. Besides the built-in Dubeolsik and Sebeolsik, plain-text `.jmt`
  files define static remaps, Hangul automata layouts, and chorded keyboards; a single
  settings export carries them to another machine.
- Practical hanja data, fully offline: 9,525 unique characters (100% of standard
  personal-name hanja), ~2,200 compound words, 6,832 meaning-reading entries.
- Install, uninstall, and upgrade without signing out.
- MIT licensed, with the entire implementation documented in a public field manual
  (`winapi-c-ime-manual.md`).

## Features

- **Commit-only input engine**: only completed syllables are inserted into the document.
  Works identically in **every classic app**, including legacy (CUAS / IMM32-bridged)
  apps — no app detection, no per-app workarounds. Rationale: `winapi-c-ime-manual.md` §8.
- **Composition preview overlay**: the syllable being composed is shown in a floating
  chip at the caret (RFC-0002). Font face and size are user-configurable.
- **Keyboard layouts**: built-in Dubeolsik (2-beolsik) and Sebeolsik (final), plus user
  layouts via `.jmt` files (static remap / hangul automata / chord keyboard with layers,
  tap-hold and mouse actions — see the [.jmt reference](#custom-keyboard-layouts-jmt)).
- **Hanja conversion**: compose a syllable and press the Hanja key — or **select text
  first, then press Hanja** (works in legacy apps too). The candidate window shows the
  meaning+reading (hunum), or the reading alone when a character has no hunum entry (never a
  bare `U+XXXX`). Reading data: ~9,525 unique characters (Unicode Unihan plus
  the Supreme Court personal-name hanja set — 100% coverage of standard name hanja,
  educational-basic-hanja first ordering) + ~2,200 words + 1,784 hunum entries.
- **Special characters**: consonant + Hanja key (Mieum = symbols, Siot = Greek,
  Jieut = Roman numerals, etc. — the familiar Korean IME convention).
- **Unicode codepoint input**: `Ctrl+Alt+U` → type hex (live glyph preview and character
  name — hunum/reading for hanja, otherwise the Unicode block) → Enter.
- **Configurable shortcuts**: every trigger (layout switch, Hanja, Unicode input,
  settings, pass-through mode) accepts **multiple bindings** (up to 8 per function).
- **Pass-through (direct input) mode**: a toggle (tray icon right-click menu) that makes
  Jamotong stop intercepting keys entirely — for remote-desktop clients and similar.
  While on, the tray icon shows `--` and even the layout-switch key passes through, so
  you type Korean with the remote machine's IME.
- **Manager app** (`jamotong.exe`): a normal desktop app (appears in the taskbar and Task
  Manager) that opens/edits/validates `.jmt` layout files, tests input without TSF, and
  opens the settings window. Not a tray/background process.
- Both 64-bit and 32-bit applications are supported (separate DLLs).
  Windows 11 input-indicator branding icon included.

## Install

1. Download the latest zip from
   [Releases](https://github.com/rubidus-api/jamotong_ime/releases)
   (or build from source: `make stage` produces an installable `dist/` folder) and extract it.
2. Right-click `install.bat` → **"Run as administrator"**. It copies the program to
   `C:\Program Files\Jamotong`, registers the 64-bit and 32-bit text services, and checks that
   both point there.
3. Press `Win+Space` and select **"Jamotong IME"**. Apps that were already running pick
   up the IME after you restart them; sign out and back in only if it does not appear
   in the list.

### Defaults after install

Enabled layouts (cycle with the layout-switch key):

| Layout | Enabled by default |
|---|---|
| English QWERTY | ✔ |
| Korean Dubeolsik (2-beolsik) | ✔ |
| English Dvorak | ✖ (enable in Settings → Layouts) |
| Korean Sebeolsik final | ✖ (enable in Settings → Layouts) |

Default keys — every function is configurable and accepts multiple bindings
(Settings → Shortcuts):

| Function | Default binding(s) |
|---|---|
| Switch layout (Korean/English) | Hangul key, Right Alt, Shift+Space |
| Hanja / special characters | Hanja key |
| Unicode codepoint input | Ctrl+Alt+U |
| Open settings window | Ctrl+Alt+K |
| Pass-through (direct input) mode toggle | (none by default — toggle via the tray icon's right-click menu, or assign one) |

## Everyday use

- **Typing Hangul**: switch to the Dubeolsik layout and type — the syllable being
  composed appears in a floating preview chip at the caret and is inserted when it
  completes. Backspace deletes jamo-by-jamo while composing (configurable).
- **Hanja**: while composing a syllable, press the Hanja key → pick from the candidate
  window (`↑`/`↓` highlight, `←`/`→`/`PgUp`/`PgDn`/`Space` page, digits or `Enter`
  select, `Esc` cancel, mouse click works). Or **select existing text** (a syllable or
  word) and press Hanja to convert it in place.
- **Special characters**: type a single consonant (e.g. `ㅁ`, `ㅅ`, `ㅈ`) and press the
  Hanja key for the conventional symbol tables.
- **Unicode input**: press `Ctrl+Alt+U`, type a 2–6 digit hex codepoint (live glyph
  preview and character name), press `Enter`.
- **Popups** (candidate list, Unicode input, composition chip) stay inside the screen's work
  area — near the bottom or right edge they move left or open above the line. In apps that
  handle display scaling they follow the monitor's scale (sizes in Settings are at 100%), and
  with a **high-contrast** theme they use the theme's colors.

> **Inside UWP apps (taskbar search, the Settings app, Store apps) some of this works
> differently.** Windows does not let an input method show its own windows in those apps, so a
> candidate list or popup can never appear there. In such apps:
> - **Hanja**: a small **UI helper** that ships with Jamotong draws the candidate list for you —
>   pick with the number keys, arrows or Enter, as usual. The helper starts by itself once you use
>   Jamotong in a desktop app. Turn it off (`UseUiHelper=0`) and you get the fallback instead:
>   **press the hanja key again** to step through candidates (`UwpHanjaCycle=0` disables that too).
> - **Unicode input**: with the helper running it works as usual — `Ctrl+Alt+U`, type the hex,
>   press `Enter`. With the helper off, **type the hex first, then press `Ctrl+Alt+U`**
>   (type `AC00`, press `Ctrl+Alt+U`, and you get `가`).
> - The composing syllable is shown inline by the app itself, so no preview chip appears.
>
> Ordinary desktop apps behave exactly as before.
- **Settings window**: `Ctrl+Alt+K`, or run `jamotong.exe` (Layout ▸ Settings).
  Tabs: *Layouts* (enable/disable, reorder, add `.jmt`), *Shortcuts* (pick a function,
  then add/edit/delete its keys), *IME Options* (Hanja behavior, full-width, preview
  font/size), *General* (DPI, import/export, reset).

### Remote desktop — Jamotong on both PCs

Compose on **one side only**. Two input methods composing the same keystrokes is not
supported (it is undefined, not merely untested), and there is no protocol that would let the
two cooperate.

| You want Hangul composed by | Local PC | Remote PC |
|---|---|---|
| the **remote** PC's IME (usual for remote work) | Jamotong in **pass-through mode** (icon shows `--`) — every key, including the layout-switch key, goes through untouched | Jamotong (or any IME) in Hangul mode |
| the **local** Jamotong | Jamotong in Hangul mode | set the remote input method to **English**, so it inserts the text it receives as-is |

**Signs that both sides are composing:** jamo split into separate letters (`ㅎㅏㄴ` instead of
`한`), a syllable typed twice, the layout-switch key flipping both PCs at once, or a candidate
window opening on both screens. Turn on pass-through on the local PC (tray icon, right click)
or switch the remote PC to English.

Whether the local IME sees keys at all depends on the remote-desktop client and its
keyboard-capture setting (full screen, "apply Windows key combinations"); that part has not
been verified for every client. The one-side rule holds either way.

## Settings file

All settings are stored in a plain-text INI file:

```
%APPDATA%\Jamotong\config.ini
```

(usually `C:\Users\<you>\AppData\Roaming\Jamotong\config.ini`). It is written when you
press **Apply & Save** in the settings window and loaded whenever the IME starts in any
app. Use *General → Export/Import* to move settings between machines — **Export also bundles
your user `.jmt` layouts** into the file, so a single exported `.ini` carries both settings
and custom keyboards; Import restores the layouts on the other machine. Delete the file to
return to factory defaults. Uninstalling does not remove it.

## Custom keyboard layouts (.jmt)

A `.jmt` file is a plain UTF-8 text file describing a keyboard layout. This section is the
introduction; [`jmt-format.md`](jmt-format.md) is the complete reference (every directive, the
dictionary format, the build commands and every message with its code). There are four kinds,
selected by the `Type =` line:

| `Type` | Purpose |
|---|---|
| `static` | 1:1 character remap (Dvorak, Colemak, …) |
| `hangul` | Hangul automata layout (Sebeolsik-family, custom combination rules) |
| `chord`  | Chorded keyboard (ARTSEY-style): key combos → text/keys/mouse, with layers and tap-hold |
| `input`  | Format 3 common surface; `Engine = sequence` turns a run of keys into other letters |

**A layout is compiled before it is used.** The IME reads only built layouts (`.jmb`); the
`.jmt` file is the source you edit. Compiling checks the whole file and, for a sequence layout,
its dictionary too — so a layout that is offered in the list is one that loads cleanly.

```sh
jamotong --build my-layout.jmt          # writes my-layout.jmb beside it
jamotong --build-dir "%APPDATA%\Jamotong\layouts"
jamotong --check my-layout.jmt          # checks the source and says whether the build is current
```

You rarely need to run these by hand:

- `install.bat` builds the layouts that ship with Jamotong and the ones already in the
  machine-wide and your own layout folders.
- The manager app (`jamotong.exe`, the tray icon) builds anything new or edited when it starts,
  and after Settings → Layouts → **Apply**.
- Settings → Layouts → **Add** builds the file you pick and reports any error in it.

**Loading a layout** — either:

- copy the `.jmt` file into `%APPDATA%\Jamotong\layouts` and start the manager app once, so it
  gets built; it is added to the layout list **disabled** at the next IME start (turn it on in
  Settings → Layouts), or
- Settings → Layouts → **Add** and pick the file (added enabled; the manager builds it for you).
  From a Windows Store app's settings window this cannot start the manager — build the file
  yourself with `jamotong --build` and copy it into the layout folder instead.

A `.jmt` placed next to `jamotong.dll` (in `C:\Program Files\Jamotong`) is **not** built by the
manager — that folder needs administrator rights. Re-run `install.bat` as administrator, or run
`jamotong --build-dir "C:\Program Files\Jamotong"` from an elevated prompt.

At most 8 layouts can be active in the list. The bundled `example.jmt`,
`example-dvorak.jmt` and `example-artsey.jmt` are commented syntax samples of each type.

### Common header

```ini
# comment — everything after '#' at line start; blank lines are ignored
Type   = hangul        # static | hangul | chord  (omitted = hangul)
Name   = my_layout     # shown in the layout list / language bar (up to 63 chars)
Abbrev = 마            # 1–4 characters drawn in the 2x2 tray icon (optional)
```

Optional metadata (format version 2; every key is optional and a v1 file without them loads
exactly as before):

```ini
FormatVersion    = 2                 # omitted = 1
Id               = kim.sebeol391     # stable identity (reverse-domain style recommended)
Version          = 1.2.0             # the layout's own version
Author           = Name <mail@example.com>
License          = CC0-1.0           # SPDX identifier recommended for shared layouts
Homepage         = https://example.com/my-layout
Description      = Sebeolsik 391 with a changed number row
Locale           = ko-KR
RequiresJamotong = 0.24.0            # refuse to load on older Jamotong, with a clear message
```

**Diagnostics.** A layout with an error is never loaded half-way. Every problem is reported
(up to 16) as `file:line:column: error|warning: message [code]` with a `help:` hint, e.g.
`my.jmt:2:9: error: Key: jamo index out of range (C 0..18 / M 0..20 / T 1..27) [E-JMT-RANGE]`.
Warnings do not block loading: an unknown header key (`Athor = …` → *did you mean 'Author'?*),
or an `Abbrev` longer than 4 characters. A **newer `FormatVersion`** than this Jamotong reads is
an error (`E-JMT-FORMAT-NEWER`): loading only the parts an old version understands would
silently give a different layout. An unrecognised line is a
warning in a version-1 file and an **error** from `FormatVersion = 2`, so typos in new files
cannot silently drop keys.

Keys are always identified by **the character the physical key produces on a US QWERTY
base**, including Shift: `k` is the K key, `K` is Shift+K, `;` `!` etc. work too.

### Deriving a layout — `Extends` / `Include`

Start from another layout and write only what changes:

```ini
FormatVersion = 2
Type    = hangul
Extends = @ko_3bul          # a built-in (@ko_2bul, @ko_3bul, @en_dvorak, @en_qwerty) or ./base.jmt
Name    = my 3-beol
Key 1   = M13               # redefine a key (the later line wins)
Key 2   = -                 # '-' removes a key inherited from the base
Combine M 8 0 = -           # '-' removes an inherited combination rule
Include = common-rules.jmt  # paste another file's lines here (several Include lines allowed)
```

One `Extends` line per file, at most 4 levels deep; a loop between files and a `Type`
different from the base are errors. Name/Abbrev are inherited unless you set them; the
base's `Id`, `Version`, `Author`, … are not. In a chord layout, a `Chord`/`Hold` that the base
already defines (same layer, same keys in any order, same tap/hold) **replaces** the inherited
one; the same chord written twice in *one* file keeps the first and warns `W-JMT-DUP-CHORD`.
Dubeolsik (`@ko_2bul`) can be a base too: `Extends = @ko_2bul` keeps its 2-set behaviour
(`Composition = dubeol`, below) and you change only the keys you list.

### Shift level, physical keys and blocks (format version 2)

```ini
Key q shift = C1         # the Shift face of q (= 'Key Q'); symbols too: 'Key 1 shift' is '!'
Key q base  = C0         # the unshifted face (the default)
Key @Q      = C0         # a key named by its US QWERTY position
Key @SC10   = C0         # ... or by its scan code (hex)
Key @VK_OEM_1 = T4       # ... or by its virtual-key name
Map @SC11 shift = W      # works in static layouts too

Begin Combine C          # a block: each inner line gets the prefix 'Combine C'
  0 0 = 1
  3 3 = 4
End
```

A physical key names one key per line and resolves to the character that key produces on a
US QWERTY layout — the IME still reads keys through that mapping, so on a non-US Windows
keyboard layout the result follows Windows' key codes (not yet verified on such layouts).
`altgr` is rejected: the IME does not read the AltGr face. Blocks do not nest; a missing
`End` is an error.

### Authoring in the manager (`jamotong.exe`)

- **File ▸ New copy of a built-in layout** — a complete editable copy of Sebeolsik final, Dvorak or QWERTY.
- **File ▸ New derived layout** — an `Extends = @…` template: write only what changes.
- **Tools ▸ Validate layout** — full diagnostics (all errors and warnings, with line:column and help).
  Relative `Extends`/`Include` paths are resolved from the file's own folder.
- **Tools ▸ Try this file** — load the file you are editing and type in the box at the bottom,
  before installing it (static and hangul layouts).
- **File ▸ Export expanded** — the same as `--expand`; **File ▸ Install to my layouts** — validate
  and copy to `%APPDATA%\Jamotong\layouts` (asks before replacing).

### Command-line tools

```text
jamotong.exe --check  my.jmt [--json]          # validate only; exit 0 = loads, 1 = errors
jamotong.exe --export @ko_3bul -o ko_3bul.jmt  # write a built-in layout as a complete .jmt
jamotong.exe --expand my.jmt -o flat.jmt       # resolve Extends/Include into one file
```

`--expand` writes a canonical file (comments and line order are not kept; expanding it again
gives the same file). `--json` prints machine-readable diagnostics for editors.

### Type = static (1:1 remap)

One directive, in single and **array** form:

```ini
Map <key> = <output>      # that key now produces <output>
Map <keys...> = <outputs...>  # array: same length both sides, paired by position
```

Unmapped keys keep their original character. `Identity = passthrough` (with no `Map` lines)
makes a layout that lets every key through untouched, exactly like the built-in QWERTY — that is
what `--export @en_qwerty` writes. A file that `Extends = @en_qwerty` and adds `Map` lines becomes an
ordinary remap. Uppercase/symbol variants are separate
mappings. Key strings cannot contain a space — map the space key with the single form.
Example (Dvorak top row in one line):

```ini
Type = static
Name = my_dvorak
Abbrev = Dv

Map qwertyuiop = ',.pyfgcrl   # array form: q→' w→, e→. ...
Map [ = /                     # single form still works
```

### Type = hangul (automata layout)

Assign jamo to keys and declare combination rules; the IME's automata does the rest
(composition preview, commit, backspace-by-jamo, Hanja conversion all work).

```ini
Key <key> = <C|M|T><index>          # one jamo per key: C=choseong M=jungseong T=jongseong
Key <keys...> = <spec> <spec> ...   # array: one spec per key, paired by position
Combine <C|M|T> <a> <b> = <result>  # jamo <a> then <b> combine into <result>
Moachigi = 0|1                      # 1 = simultaneous (order-free) combination, see below
Composition = sebeol|dubeol         # how finals are typed (omitted = sebeol), see below
```

**`Composition`** — `sebeol` (default): final consonants have their own keys (`T` specs).
`dubeol`: like the built-in 2-set, a consonant key (`C`) becomes the final of the syllable
when it can, and moves to the next syllable when a vowel follows (`r k s k` → 가나). A dubeol
file has no `T` keys; compound finals come from `Combine T <final> <consonant as final> = <result>`
(`Combine T 1 19 = 3`: ㄱ then ㅅ → ㄳ). Which consonant becomes which final, and how a
compound final splits, follow standard modern Hangul. `dubeol` cannot be combined with
`Moachigi = 1`. `jamotong --export @ko_2bul` writes the full built-in 2-set as a file.

Index tables (the number after C/M/T):

```
C (choseong):  0ㄱ 1ㄲ 2ㄴ 3ㄷ 4ㄸ 5ㄹ 6ㅁ 7ㅂ 8ㅃ 9ㅅ 10ㅆ 11ㅇ 12ㅈ 13ㅉ 14ㅊ 15ㅋ 16ㅌ 17ㅍ 18ㅎ
M (jungseong): 0ㅏ 1ㅐ 2ㅑ 3ㅒ 4ㅓ 5ㅔ 6ㅕ 7ㅖ 8ㅗ 9ㅘ 10ㅙ 11ㅚ 12ㅛ 13ㅜ 14ㅝ 15ㅞ 16ㅟ 17ㅠ 18ㅡ 19ㅢ 20ㅣ
T (jongseong): 1ㄱ 2ㄲ 3ㄳ 4ㄴ 5ㄵ 6ㄶ 7ㄷ 8ㄹ 9ㄺ 10ㄻ 11ㄼ 12ㄽ 13ㄾ 14ㄿ 15ㅀ 16ㅁ 17ㅂ 18ㅄ 19ㅅ 20ㅆ 21ㅇ 22ㅈ 23ㅊ 24ㅋ 25ㅌ 26ㅍ 27ㅎ
```

Example (excerpt — a Sebeolsik-style layout where choseong/jungseong/jongseong live on
different keys):

```ini
Type = hangul
Name = ex_hangul
Abbrev = 예벌
Moachigi = 1

Key khj = C0 C2 C11   # array: k=ㄱ h=ㄴ j=ㅇ (choseong)
Key fd = M0 M20       # f=ㅏ d=ㅣ
Key s = T4            # ㄴ as jongseong (single form)
Key x = T1            # ㄱ as jongseong

Combine C 11 0 = 1     # choseong ㅇ+ㄱ → ㄲ (doubled consonant)
Combine C 18 12 = 14   # choseong ㅎ+ㅈ → ㅊ (aspirated)
Combine M 8 0 = 9      # ㅗ+ㅏ → ㅘ
Combine T 1 19 = 3     # jongseong ㄱ+ㅅ → ㄳ
```

- **`Moachigi = 0`** (sequential): jamo are entered one keystroke at a time in order —
  the usual typing style.
- **`Moachigi = 1`** (simultaneous / "moa-chigi"): several keys pressed together form
  one syllable, and `Combine` rules match **regardless of order** (`a b` also matches
  `b a`). Use this for simultaneous-stroke Sebeolsik variants.

Up to 256 `Combine` rules per layout.

### Type = chord (chorded keyboard)

A small set of "chord keys" is pressed **together and released** to perform an action —
the model used by one-hand keyboards such as ARTSEY. Actions are delivered as real
key/mouse events, so they work in any app and even outside Hangul mode.

```ini
Type = chord
Name = ex_chord
Abbrev = ART

# 1) Declare the chord keys first: each gets a bit number 0–31.
#    Array form assigns consecutive bits from the given start:
Key jkl; = 0    # j=0 k=1 l=2 ;=3
Key f = 4       # single form still works

# 2) Chords: press the listed keys together, release, action fires.
Chord j   = a          # single key = the letter a
Chord jk  = e          # j+k together = e
Chord jkl = the        # three keys = the whole word "the"
```

#### Actions (right-hand side)

| Syntax | Meaning |
|---|---|
| *plain text* | Types the text (**at most 23 characters**; longer is an error, not cut). Escapes: `\n` Enter, `\t` Tab, `\s` space, `\\` backslash, `\#` a literal `#`. A lone `\b` is Backspace. A `#` after a space starts a comment, so write `\#` for a `#` after a space (`C#` and a text that *starts* with `#` are fine as they are). |
| `key <name>` | Presses one special key. See the key-name list below. |
| `mod <name>` | **One-shot modifier**: the *next* chord/character gets this modifier. Names: `shift ctrl alt gui` (left side) and `rshift rctrl ralt rgui`. |
| `layer <name>` | **One-shot layer**: only the next chord is looked up in that layer. |
| `tlayer <name>` | **Layer toggle**: switch to the layer; the same chord (or any `tlayer` of it) switches back to `base`. |
| `slayer <name>` | **Layer switch**: go to the layer and stay. |
| `mouse move <dx> <dy>` | Move the pointer by (dx, dy) pixels. |
| `mouse click\|down\|up <left\|right\|middle>` | Mouse button: click = press+release, down/up = halves for dragging. |
| `mouse wheel <up\|down\|N>` | Scroll (N = raw wheel delta, negative = down). |

After the words an action needs, only a `# comment` may follow — `key a junk`, `mouse move 5 5x` or
`key f1junk` are errors (they used to be accepted by ignoring the rest). `mouse move` takes -10000…10000.

Key names accepted by `key <name>`:

```
Navigation : left right up down home end pgup pgdn ins del
Editing    : back enter tab space esc
Numpad     : kp0–kp9 kpadd kpsub kpmul kpdiv kpdot kpenter
Modifiers  : lshift rshift lctrl rctrl lalt ralt lwin rwin   (as plain keys)
Locks/misc : caps numlock scroll pause apps prtsc sleep
Media      : volup voldown mute mplay mnext mprev mstop
Browser    : browserback browserfwd browserrefresh browserhome mail calc mediasel
Function   : f1 … f24  (including the invisible f13–f24)
Single char: any letter/digit, e.g.  key x
```

#### Layers

```ini
Layer base       # implicit default layer; 'Layer' changes where following chords go
Chord a = layer num      # one-shot: only the NEXT chord uses the num layer
Chord fa = tlayer mouse  # toggle the mouse layer on/off

Layer num
Chord j = 1
Chord k = 2

Layer mouse
Chord j  = mouse move -20 0
Chord jk = mouse click left
Chord fa = tlayer mouse   # press again to return to base
```

Up to 16 layers, 2048 chords. A `Chord` line belongs to the most recent `Layer`
directive (or `base`). Layers may be referenced before they are defined.

#### Tap vs. Hold (delayed input)

The same chord can do two things — a quick **tap** (`Chord`) and a **hold** (`Hold`):

```ini
Chord jkl = the        # tap  (press and release quickly)
Hold  jkl = key enter  # hold (keep pressed ≥ 0.2 s, then release) → Enter
```

`Hold` behaves in one of two ways depending on its action:

- **Sustained hold** — action is `layer <name>` or `mod <name>`:
  triggered *by interruption*: while you keep the hold chord pressed and press another
  key, the layer/modifier applies **for as long as you keep holding**, then reverts on
  release. This is the momentary-layer / hold-modifier idiom:

  ```ini
  Hold jk = layer num    # hold j+k, tap other keys → they type from the num layer
  Hold kl = mod lctrl    # hold k+l, tap other keys → they arrive as Ctrl+<key>
  ```

- **Discrete (delayed) hold** — any other action (`text`, `key`, `mouse`):
  press the chord, keep it pressed at least **0.2 seconds** without pressing anything
  else, then release — the `Hold` action fires instead of the `Chord` (tap) action.

If a chord has only a `Hold` entry (no tap), the hold action fires on release
regardless of timing.

#### Complete example

`example-artsey.jmt` in the distribution shows all of the above in one working file:
letters, word chords, Backspace/Space/Enter, one-shot Shift, one-shot and momentary
number layer, a toggled mouse layer (pointer movement, clicks, wheel), tap/hold pairs
and media keys. Copy it, keep the `Key` bit declarations, and fill in your own chord
table (e.g. the published ARTSEY map).

#### Format version 3 (chord layouts)

`FormatVersion = 3` gives chord layouts an unambiguous action syntax and decides overlapping
tap/hold chords deterministically. Version 1/2 files keep working exactly as before. A version 3
file must say which Jamotong it needs (`RequiresJamotong = 0.33.0`); older versions refuse it.

```ini
FormatVersion    = 3
Type             = chord
RequiresJamotong = 0.33.0
Key jkl; = 0
ComboTermMs = 50            # wait this long (ms) after the first key for a bigger chord (1-1000)
HoldTermMs  = 200           # tap/hold threshold (1-5000)
HoldPolicy  = interrupt     # interrupt: another key confirms the hold; timeout: only after HoldTermMs

Chord jkl = text "the"            # exact text: "..." with \" \\ \n \t \u{1F600}; '#' inside quotes is text
Chord l   = key B mods(ctrl,shift) # a key with modifiers
Chord ;   = oneshot mod(shift)    # next key only (a following text is NOT shifted)
Chord jl  = oneshot layer(num)
Hold  jk  = momentary layer(num)  # while held (Hold only)
Hold  kl  = momentary mod(lctrl)
Chord kl  = toggle layer(num)
Chord j;  = switch layer(base)

Hold  k   = pointer move(0,1) profile(normal)   # while held: move down (slow|normal|fast)
Chord jk  = pointer click(left)                 # click / down / up / drag-toggle
Chord k;  = pointer drag-toggle(left)           # press now, release on the next chord
Hold  jkl = pointer wheel(0,-1) profile(scroll) # while held: scroll down (wheel(1,0) = right)
Chord kl; = cancel actions                      # stop moving, drop a held drag, cancel a macro

Macro label                       # a finite, cancelable sequence
  text "item: "
  key LEFT
  wait 40                         # ms (1-2000); the IME never blocks while waiting
  with mods(ctrl,shift)
    key RIGHT
  endwith
EndMacro
Chord jl; = macro label
Layer num                         # chords of the num layer
Chord j   = text "1"
```

- **Bigger chords win**: with `Hold jk` and `Chord jkl`, pressing j k l within `ComboTermMs`
  types `the`; the `jk` hold only starts when no bigger chord can still be formed.
- **Macros**: `Macro <name> … EndMacro` holds up to 128 steps (`text`, `key`, `pointer`, `wait`,
  `with mods(...)` / `endwith`) and runs at most 3 seconds. One runs at a time; pressing any other
  key, `cancel actions`, a focus change or a layout switch cancels it and releases the modifiers it
  pressed. There is no repetition, no condition and nothing that reads the clipboard or files.
- **Pointer**: a `pointer move`/`wheel` on `Hold` runs while you hold the keys — it speeds up to
  the profile's limit, opposite directions cancel, diagonals are normalized. On `Chord` the same
  action happens once (`pointer move(10,0)` = 10 px right). A drag started with `drag-toggle` (here `k;`) is
  released by the same chord, by `cancel actions`, or automatically when focus or layout changes.
- **Holding still counts**: keep the keys of a `Hold` chord pressed and the layer/modifier turns
  on after `HoldTermMs` even if you press nothing else.
- **Rolling**: the first release closes a chord. If a new key goes down before the rest are
  released, the closed chord fires first and the new key starts the next chord.
- In version 3 an unquoted text, a word after an action, or the same chord twice in one file
  is an error. Mouse actions use the same words as version 2.

#### Sequence input (format version 3)

A **sequence layout** turns a *run* of Latin keys into other letters — romaji to kana, for
example. The table is not written in the layout file: it lives in a **dictionary** that you
compile once. Jamotong reads only the compiled file, and a layout whose dictionary is missing
or damaged is not offered at all.

Write the dictionary source `romaji-kana.jdt` (UTF-8, one entry per line, a tab between the two
columns):

```text
JamotongData 1
Type = sequence
Name = romaji kana
License = CC0-1.0
a	\u{3042}
i	\u{3044}
ka	\u{304B}
ki	\u{304D}
ko	\u{3053}
n	\u{3093}
na	\u{306A}
ni	\u{306B}
chi	\u{3061}
ha	\u{306F}
```

Build it (the letters may also be written directly instead of `\u{...}`):

```sh
jamotong --build-dict romaji-kana.jdt -o romaji-kana.jdb
```

Then the layout file only says which dictionary it uses:

```ini
FormatVersion    = 3
Type             = input
Engine           = sequence
RequiresJamotong = 0.39.1
Name             = romaji kana
Abbrev           = KANA
Dictionary       = romaji-kana.jdb
OnUnmatched      = flush     # flush (default): type the pending letters. cancel: drop them
```

- **Longest match wins.** With `n`, `na` and `ni` in the dictionary, `n` waits: `ni` becomes に,
  while `nk` types ん and starts a new `k`. Typing `konnichiha` gives こんにちは.
- **Pending letters are shown, not inserted.** They appear in the preview chip next to the caret
  (in the manager's test box, as selected text) and only reach the document once they are decided.
- **Backspace** takes back one pending letter; letters already in the document are left alone.
  **Esc** drops the pending input. Switching layouts with the layout key types what was pending; a
  focus change (another window, the language bar) drops it — it was never in the document.
- **Space, Enter, Tab and the arrow keys belong to the application.** If something was pending, it
  is typed into the document first.
- The dictionary is looked up beside the layout file, then in `%APPDATA%\Jamotong\dicts`, then in
  `%PROGRAMDATA%\Jamotong\dicts`. The name is a plain file name ending in `.jdb`.
**Readings and candidates (optional).** With a candidate dictionary the letters the engine
produces are not committed at once: they collect in a reading the IME owns (shown next to the
caret), and the convert key turns that reading into a candidate list.

```ini
Dictionary  = romaji-kana.jdb     # keys -> letters
Candidates  = kana-words.jdb      # a reading -> several candidates
ConvertKey  = space               # space | tab | hanja | convert | f9
```

The candidate dictionary source uses `Type = candidates` and repeats the same reading once per
candidate, in the order you want them offered:

```text
JamotongData 1
Type = candidates
かな	仮名
かな	金娜
```

Backspace takes back one letter of the reading, Esc drops it, and switching layouts or leaving the
window commits the reading as it is — the IME never picks a candidate for you. A choice that
arrives after the reading changed is dropped. Without `Candidates` nothing changes: the letters go
straight into the document as before.

**A chord front end (optional).** An input layout may also declare chords. Then the chords decide
first and only what they produce with `symbol` goes into the engine — the same physical key is
never consumed twice:

```ini
Key jkl; = 0
Chord j  = symbol "k"      # logical input for the engine, not a key event
Chord jk = symbol "a"      # a bigger chord wins
Chord l  = text "hello"    # text and key actions skip the engine and go out as real input
Hold j   = momentary layer(num)
Hold ;   = oneshot mod(shift)   # hold to arm Shift for the next chord, then let go
```

A symbol the engine cannot use (no entry starts with it) is typed as it is, so nothing is lost.
Chord layouts (`Type = chord`) have no engine, so `symbol` there is an error. Everything else in
a chord layout — layers, tap/hold, pointer actions, macros — works the same in an input layout.

**Bringing in dictionary data you already have.** Word lists worth typing with are large, and
the ones worth using are other people's work. Jamotong ships none of them; it converts what you
choose:

```sh
jamotong --import-dict japanese.txt -o kana-words.jdt --limit 50000 \
         --name "Japanese words" --license "see DICTIONARY-LICENSE.txt"
jamotong --build-dict  kana-words.jdt -o kana-words.jdb
```

`--import-dict` reads a plain two-column TSV (`reading<TAB>surface`) and the five-column form
used by several open Japanese dictionaries (`reading lid rid cost surface`). Rows are sorted by
reading, and by cost within one reading, so the cheapest — the most common — candidate is offered
first. `--limit N` keeps the N cheapest rows, which is how you cut a 90 MB dictionary down to
something that opens instantly. A reading may be kana, Hangul or any other text, up to 96 bytes
of UTF-8 (about thirty kana); a surface is up to 64 characters. Comments, blank lines, rows that are
too long and rows whose text is not valid UTF-8 are skipped and counted — the importer never
writes a row the compiler would refuse. The same reading and surface twice (common in word data
that differs only in part of speech) is kept once, so the candidate list is not filled with
repeats, and `--limit` is applied after that. `--name` and `--license` are written into the
dictionary itself, so a dictionary that is passed on still says where it came from. The entries keep the licence of the file they came from,
so check it before you pass the result on.

Measured on one 7 MB shard of an open Japanese dictionary (128,908 rows): 123,381 entries kept,
5,527 readings too long, 0.08 s to convert, 0.08 s to build, 5.3 MB of `.jdb`, 1.6 ms to open.

**Ready-made Japanese (experimental).** Two dictionary packs are published beside the installer,
so the installer itself stays small and the word data keeps its own licence file next to it:
a demo pack (50,000 entries, about 0.5 MB) and a full pack (500,000 entries, about 5 MB, the most
one dictionary can hold). Each holds a kana-to-kanji dictionary, a romaji-to-kana table, the
layout that ties them together and the licence of the word data. Unpack it, copy the two `.jdb`
files into `%APPDATA%\Jamotong\dicts` and the `.jmt` into `%APPDATA%\Jamotong\layouts`, then
start the manager. Type `nihon`, press space, pick 日本. The word data comes from the open-source
Mozc dictionary; no native speaker has reviewed this profile yet, so it is offered as an
experiment rather than as finished Japanese support.

**Ready-made Chinese (experimental).** Two more packs bring pinyin input: type pinyin without
tones, press space, pick the hanzi. `nihao` gives 你好, `zhongguo` gives 中国, `xuexi` gives 学习;
write `ü` as `v` (`lv` → 率/绿/旅), though the `u` spelling after j, q, x and y works too. The
readings come from mozillazg's pinyin-data and phrase-pinyin-data and the candidate order from
jieba's word frequencies, all MIT-licensed. Install it the same way as the Japanese pack. No
native speaker has reviewed this profile yet, so it is an experiment rather than finished Chinese
support.

- Limits: the typed side of a sequence dictionary is printable ASCII up to 32 characters, a
  candidate reading is up to 96 bytes of UTF-8, one entry emits up to 64 characters, and a
  dictionary holds up to 500,000 entries. A dictionary that uses a reading longer than 32 bytes is
  written as format version 2, which Jamotong 0.44 and older refuse to load rather than misread.
- **Loading a layout checks its dictionary in full** — the checksum, the key order and the key
  characters — so a damaged dictionary means the layout is not offered at all, wherever it came
  from. The check reads the whole file once per load (about 7 ms for 100,000 entries).
  `jamotong --check layout.jmt` runs exactly the same check and names the dictionary it used.


## Uninstall

1. Run `uninstall.bat` as administrator — from the zip, or the copy in
   `C:\Program Files\Jamotong`. It unregisters both text services (whatever folder they
   point at, so older installs are covered too), stops jamotong.exe and deletes the files.
2. A DLL still loaded in running apps cannot be deleted; it is moved aside and removed at the
   next sign-in. A reboot is not required.
3. Your settings remain at `%APPDATA%\Jamotong`; delete that folder too if you do not
   plan to reinstall.

## Build

Cross-compiled with MinGW-w64 (on Linux):

```sh
make            # dist/jamotong.dll (x64)
make win32      # dist/jamotong32.dll (x86)
make configapp  # dist/jamotong.exe (manager: .jmt editor / settings / input test)
make stage      # build everything + copy redist/ into dist/
                #  -> dist/ becomes an installable folder (run install.bat as admin)
```

`redist/` contains the redistributable data required at runtime: the hanja reading
table (`hanja.txt`), the meaning/reading table (`hanja_hunum.txt`), a copy of the
Unicode License, install/uninstall scripts, and sample `.jmt` layouts.

## Documentation

- **`examples/minimal-tip/`** — a minimal working TSF IME (~200 lines). Built for people
  writing their first IME: get past "COM server → registration → key sink → insertion" before
  anything else. Manual §0.5 walks through it.
- **`winapi-c-ime-manual.md`** — a complete manual on building a Windows IME with
  nothing but WinAPI and C, starting from COM. Includes the trial-and-error record
  and its conclusions. ([한국어](winapi-c-ime-manual.ko.md))
- `CHANGELOG.md` — version history
- Detailed design notes, RFCs, and data-provenance documents are maintained in an
  internal repository. The distribution zip bundles all required license notices.

**Extensibility policy**: user keyboards are defined by the code-free `.jmt` data format
(static / hangul / chord) — no native code runs from a layout. The DLL-plugin interface in
`src/jamotong_plugin.h` is **experimental and disabled** (never auto-loaded): a TIP loads
into every host process, so injecting third-party code there is unsafe. If revived it would
require explicit install, signature verification and an out-of-process model.

## License

Code: [MIT License](LICENSE). Hanja reading data is derived from the Unicode Unihan
Database (Unicode License v3; notice bundled in the distribution), the Supreme Court
personal-name hanja public data (rutopio/Korean-Name-Hanja-Charset, MIT), and word
mappings from jemdiggity/hanja-wordlist (MIT; Korean↔hanja mappings only, no
definitions). Icon lettering uses glyphs derived from the
[Spleen 5x8](https://github.com/fcambus/spleen) bitmap font
(Copyright (c) 2018-2026, Frederic Cambus; **BSD 2-Clause License**); the notice ships
in `src/icon_font.h` and in [COPYRIGHT.md](COPYRIGHT.md) (also shipped in the zip). No GPL/LGPL/CC BY-SA
material is used.
