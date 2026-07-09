# Jamotong (자모통)

[한국어](README.ko.md) | **English**

**A Korean (Hangul) IME for Windows in pure C23 + WinAPI** — a TSF (Text Services
Framework) text service with no frameworks and no external libraries.

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
  settings) accepts **multiple bindings** (up to 8 per function).
- **Manager app** (`jamotong.exe`): a normal desktop app (appears in the taskbar and Task
  Manager) that opens/edits/validates `.jmt` layout files, tests input without TSF, and
  opens the settings window. Not a tray/background process.
- Both 64-bit and 32-bit applications are supported (separate DLLs).
  Windows 11 input-indicator branding icon included.

## Install

1. Download the latest zip from
   [Releases](https://github.com/rubidus-api/jamotong_ime/releases)
   (or build from source: `make stage` produces an installable `dist/` folder).
2. Extract it and **copy the folder to a permanent location of your choice** — the IME
   runs directly from this folder, so do not delete it after installing. A typical
   choice is the standard program location:

   ```
   C:\Program Files\Jamotong
   ```

3. Right-click `install.bat` → **"Run as administrator"**.
4. **Sign out and back in** (a full reboot is not required), then press `Win+Space`
   and select **"Jamotong IME"**.

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
- **Settings window**: `Ctrl+Alt+K`, or run `jamotong.exe` (Layout ▸ Settings).
  Tabs: *Layouts* (enable/disable, reorder, add `.jmt`), *Shortcuts* (pick a function,
  then add/edit/delete its keys), *IME Options* (Hanja behavior, full-width, preview
  font/size), *General* (DPI, import/export, reset).

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

A `.jmt` file is a plain UTF-8 text file describing a keyboard layout. There are three
kinds, selected by the `Type =` line:

| `Type` | Purpose |
|---|---|
| `static` | 1:1 character remap (Dvorak, Colemak, …) |
| `hangul` | Hangul automata layout (Sebeolsik-family, custom combination rules) |
| `chord`  | Chorded keyboard (ARTSEY-style): key combos → text/keys/mouse, with layers and tap-hold |

**Loading a layout** — either:

- copy the `.jmt` file next to `jamotong.dll` (it is auto-detected at IME start,
  added to the layout list **disabled**; turn it on in Settings → Layouts), or
- Settings → Layouts → **Add** and pick the file (added enabled).

At most 8 layouts can be active in the list. The bundled `example.jmt`,
`example-dvorak.jmt` and `example-artsey.jmt` are commented syntax samples of each type.

### Common header

```ini
# comment — everything after '#' at line start; blank lines are ignored
Type   = hangul        # static | hangul | chord  (omitted = hangul)
Name   = my_layout     # shown in the layout list / language bar (up to 63 chars)
Abbrev = 마            # 1–4 characters drawn in the 2x2 tray icon (optional)
```

Keys are always identified by **the character the physical key produces on a US QWERTY
base**, including Shift: `k` is the K key, `K` is Shift+K, `;` `!` etc. work too.

### Type = static (1:1 remap)

One directive, in single and **array** form:

```ini
Map <key> = <output>      # that key now produces <output>
Map <keys...> = <outputs...>  # array: same length both sides, paired by position
```

Unmapped keys keep their original character. Uppercase/symbol variants are separate
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
```

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
| *plain text* | Types the text (max ~23 chars). Escapes: `\n` Enter, `\t` Tab, `\s` space, `\\` backslash. A lone `\b` is Backspace. |
| `key <name>` | Presses one special key. See the key-name list below. |
| `mod <name>` | **One-shot modifier**: the *next* chord/character gets this modifier. Names: `shift ctrl alt gui` (left side) and `rshift rctrl ralt rgui`. |
| `layer <name>` | **One-shot layer**: only the next chord is looked up in that layer. |
| `tlayer <name>` | **Layer toggle**: switch to the layer; the same chord (or any `tlayer` of it) switches back to `base`. |
| `slayer <name>` | **Layer switch**: go to the layer and stay. |
| `mouse move <dx> <dy>` | Move the pointer by (dx, dy) pixels. |
| `mouse click\|down\|up <left\|right\|middle>` | Mouse button: click = press+release, down/up = halves for dragging. |
| `mouse wheel <up\|down\|N>` | Scroll (N = raw wheel delta, negative = down). |

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

## Uninstall

1. Right-click `uninstall.bat` → **"Run as administrator"**.
   It unregisters the IME, stops jamotong.exe and deletes every binary that is not in
   use.
2. If some files were reported **locked**: sign out and back in, then run
   `uninstall.bat` once more (or just delete the folder). A full reboot is *not*
   required — an IME DLL stays memory-mapped in every running app that used text input
   (Explorer, ctfmon, …), and signing out ends those processes.
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
definitions). No GPL/LGPL/CC BY-SA material is used.
