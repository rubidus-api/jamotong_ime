[한국어](README.ko.md) | **English** — **Jamotong v0.73.1** — [MSI](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.73.1/jamotong-0.73.1.msi) · [ZIP](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.73.1/jamotong-0.73.1.zip) · [Cleanup tool](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.73.1/jamotong-cleanup-0.73.1.exe)

# Jamotong (자모통)

**A Korean (Hangul) IME for Windows in pure C23 + WinAPI** — a TSF (Text Services
Framework) text service with no frameworks and no external libraries.

## Download

| | Latest release (direct download) |
|---|---|
| **Jamotong installer package (recommended)** | **[jamotong-0.73.1.msi](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.73.1/jamotong-0.73.1.msi)** — double-click to install (English or Korean, you choose the folder). **One file**: tick Chinese Simplified, Chinese Traditional or Japanese on the features page to install them too (about 21 MB with all three); remove it from Installed apps |
| Jamotong zip (no installer) | [jamotong-0.73.1.zip](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.73.1/jamotong-0.73.1.zip) — the same files, to place and register by hand (see [Install](#install)) |
| Cleanup tool | [jamotong-cleanup-0.73.1.exe](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.73.1/jamotong-cleanup-0.73.1.exe) — removes **every version** of Jamotong from this PC: the installer, the old language packs, a registration without an installer (zip), the old IMM32 input method and what they left; your settings only if you tick the box. Run it, approve the administrator prompt, Remove all; if it asks for a restart, restart and run it again |
| Input-list repair tool | [jamotong-ime-list-repair-0.18.0.zip](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.18.0/jamotong-ime-list-repair-0.18.0.zip) — when Win+Space shows IMEs you never installed (README inside) |

All versions and release notes: [Releases](https://github.com/rubidus-api/jamotong_ime/releases)

### Installing

- **The MSI (recommended)**: double-click `jamotong-0.73.1.msi`. The first page asks for the language
  of the installer (English, or Korean — picked for you when Windows is set to Korean), then shows the
  license and the install folder. It asks for administrator rights once — registering an input method
  writes to a machine-wide place, which is Windows' rule, not ours. Upgrade by running the newer MSI;
  remove it from **Settings ▸ Apps ▸ Installed apps ▸ Jamotong ▸ Modify ▸ Remove** (Uninstall is turned off there so
  that Jamotong's own dialogs run). It never forces a restart: apps that were already
  running keep the previous copy until you sign in again. A silent install works too:
  `msiexec /i jamotong-0.73.1.msi /qn` (add `INSTALLDIR="D:\Jamotong\"` to choose the folder).
- **Languages (0.70.0)**: Chinese Simplified, Chinese Traditional and Japanese are features of the same
  MSI, off by default — tick them on the features page, or add `ADDLOCAL=ZhSimplified,ZhTraditional,Japanese`
  to a silent install (`ADDLOCAL=ALL` for everything). Add or remove them later with **Installed apps ▸
  Jamotong ▸ Modify ▸ Change**. The separate Chinese pack MSIs of 0.62–0.69 are removed by the upgrade, and a
  pack you had installed stays installed as its feature. If you used the old Japanese zip pack (0.49), delete its
  `japanese.jmt` (and `japanese.*.jmb`) from `%APPDATA%\Jamotong\layouts`: a layout of yours with the same name
  takes the place of the installed one, so the old Japanese would stay.
- **The zip**: the same files without an installer, for people who place and register them by hand
  (see [Install](#install)). `install.bat` and `uninstall.bat` are gone as of 0.61.0.

**Upgrading from 0.60.0: save your work first.** The 0.60.0 package was built without the setting that
stops Windows from closing apps that have the IME loaded, and Windows removes the old version by its own
rules — so this one upgrade **closes the apps that are using Jamotong** (they are not reopened). Before
anything is closed the installer lists those apps and waits: save your work, close them and choose
**Retry**, or continue and let them be closed. A silent install (`/qn`) cannot ask and closes them. Later
upgrades do not close anything. Any `uninstall.bat` an older zip install left behind is removed by the MSI.

### Where it installs

The default is **`C:\Program Files\Jamotong`**, and you may choose another folder on the install-folder
page. The folder has one requirement: **only administrators may be able to change it or any folder above
it.** An input method is loaded into every app, including ones running as administrator, so a folder an
ordinary user can rename or write to would let that user swap the DLL. When the IME registers itself it
checks the folder and every folder above it; if a non-administrator could change one of them (a folder
under your user profile, a link, a network or removable drive), the installation stops and is rolled
back, and `%ProgramData%\Jamotong\install.log` says which folder and why. A new folder on a local drive,
such as `D:\Jamotong`, is fine. After the check the installer locks the install folder: administrators
may change it, everyone else — including Store (UWP) apps — may only read it. Pick an empty folder; its
contents get the same lock.

Your settings stay per user in `%APPDATA%\Jamotong`. An upgrade reuses the folder you chose before.

- The release binaries and the MSI are **not code-signed yet**, so SmartScreen may warn before the first
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
- **Pass-through (direct input) mode**: a toggle (`Ctrl+Alt+P`, or the tray icon's right-click menu) that makes
  Jamotong stop intercepting keys entirely — for remote-desktop clients and similar.
  While on, the tray icon shows a grey dash and even the layout-switch key passes through, so
  you type Korean with the remote machine's IME.
- **Manager app** (`jamotong.exe`): a normal desktop app (appears in the taskbar and Task
  Manager) that opens/edits/validates `.jmt` layout files, tests input without TSF, and
  opens the settings window. Not a tray/background process.
- Both 64-bit and 32-bit applications are supported (separate DLLs).
  Windows 11 input-indicator branding icon included.

## Install

1. Download `jamotong-0.73.1.msi` from
   [Releases](https://github.com/rubidus-api/jamotong_ime/releases) and double-click it. It installs the
   program and registers the 64-bit and 32-bit text services.

   Without the installer (the zip, or `make stage`, which gathers the files in `dist/`): copy the files to
   a folder only administrators can change — `C:\Program Files\Jamotong` is the usual one — and run
   `jamotong.exe --register` from an administrator prompt in that folder. It refuses a folder that
   ordinary users can change (the log in `%ProgramData%\Jamotong\install.log` says why). To remove,
   run `jamotong.exe --unregister` the same way, then delete the folder.
2. Press `Win+Space` and select **"Jamotong IME"**. Apps that were already running pick
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
| Pass-through (direct input) mode toggle | Ctrl+Alt+P (the tray icon's right-click menu toggles it too) |

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

> **Inside UWP apps (taskbar search, Store apps)** Jamotong shows its own candidate list and
> Unicode-input popup there too, as in desktop apps (0.61.1; verified in the taskbar search box).
> If an app keeps them invisible, set `UwpOwnWindow=0` in `config.ini` to go back to the earlier way:
> - **Hanja**: a small **UI helper** that ships with Jamotong draws the candidate list — pick with the
>   number keys, arrows or Enter. The helper starts by itself once you use Jamotong in a desktop app.
>   With the helper off too (`UseUiHelper=0`), **press the hanja key again** to step through
>   candidates (`UwpHanjaCycle=0` disables that as well).
> - **Unicode input**: with the helper it works as usual; with the helper off, **type the hex first,
>   then press `Ctrl+Alt+U`** (type `AC00`, press `Ctrl+Alt+U`, and you get `가`).
> - The composing syllable is shown inline by the app itself, so no preview chip appears.
>
> The same fallback (helper, then cycling) is used automatically if the candidate window cannot be
> created at all. Ordinary desktop apps behave exactly as before.
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
jamotong --build my-layout.jmt          # writes my-layout.v9.jmb beside it
jamotong --build-dir "%APPDATA%\Jamotong\layouts"
jamotong --check my-layout.jmt          # checks the source and says whether the build is current
```

You rarely need to run these by hand:

- Installing (`jamotong.exe --register`, which the MSI runs) builds the layouts that ship with
  Jamotong and the ones already in the machine-wide and your own layout folders.
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

A `.jmt` placed next to `jamotong.dll` (the install folder, `C:\Program Files\Jamotong` by default) is
**not** built by the manager — that folder needs administrator rights. Run
`jamotong --build-dir "<install folder>"` from an elevated prompt.

The layout list has no fixed size — add as many as you like and switch through the enabled ones. The bundled `example.jmt`,
`example-dvorak.jmt` and `example-artsey.jmt` are commented syntax samples of each type.

`layout-ko-onehand.jmt` is a **one-hand Hangul layout**: eight left-hand keys, where keys pressed
together in the top row (`q w e r`) give a consonant and in the bottom row (`a s d f`) a vowel; the
jamo then compose the two-set way (finals, double finals, two vowels make ㅘ-type vowels). For a tense
consonant press all four top keys together, then the plain one (`q+w+e+r`, then ㄱ = ㄲ); a consonant
typed twice stays two consonants (`먹고`). Three thumb keys reach everything else: hold `v` and press
a finger to switch to English, symbols, numbers, brackets, navigation, function keys or plain jamo
(`v`+`r` is back to Hangul); hold `b` for a one-shot Ctrl/Shift/Alt/Win on the next key (so `b`+`f`,
then English `c`, is Ctrl+C); hold space for Backspace, Enter, Delete, Tab and Esc. The file's header
has the full table. Add it like any other layout. How the chords are written is in
[jmt-format.md](jmt-format.md).

### Common header

```ini
# comment — everything after '#' at line start; blank lines are ignored
Type   = hangul        # static | hangul | chord  (omitted = hangul)
Name   = my_layout     # shown in the layout list / language bar (up to 63 chars)
Abbrev = 마            # 1–4 characters: the first two give the language shown on the tray icon (ko, en; ZH → cn, JA → jp), the whole picks its badge (optional)
```

Optional metadata (v2; every key is optional and a v1 file without them loads
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

### Shift level, physical keys and blocks (v2)

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

### Static layouts (the v4 grammar)

One key gives one character and no automaton runs. The whole layout is a `map char` block: the left
string is what the key gives on a US keyboard, the right string is what this layout gives instead.
Keys you do not name keep their own character, so **QWERTY is a layout with no mappings at all**:

```lowlayout
layout name "qwerty" .
layout format 4 .
engine none .                  rem no automaton - one key, one character

rem no `map char` block: every key passes through untouched.
rem This is exactly what `jamotong --export @en_qwerty` writes.
```

Writing a key to itself is allowed but pointless, so a real static layout only lists what it
changes. Here is a **complete** one - Dvorak, every key including the shifted face
(`jamotong --export @en_dvorak` writes this file):

```lowlayout
layout name "dvorak" .
layout format 4 .
engine none .

map char do
  "-" "[" .  "=" "]" .  "_" "{" .  "+" "}" .
  "q" "'" .  "w" "," .  "e" "." .  "r" "p" .  "t" "y" .  "y" "f" .
  "u" "g" .  "i" "c" .  "o" "r" .  "p" "l" .  "[" "/" .  "]" "=" .
  "a" "a" .  "s" "o" .  "d" "e" .  "f" "u" .  "g" "i" .  "h" "d" .
  "j" "h" .  "k" "t" .  "l" "n" .  ";" "s" .  "'" "-" .
  "z" ";" .  "x" "q" .  "c" "j" .  "v" "k" .  "b" "x" .  "n" "b" .
  "m" "m" .  "," "w" .  "." "v" .  "/" "z" .
  "Q" "\"" .  "W" "<" .  "E" ">" .  "R" "P" .  "T" "Y" .  "Y" "F" .
  "U" "G" .  "I" "C" .  "O" "R" .  "P" "L" .  "{" "?" .  "}" "+" .
  "S" "O" .  "D" "E" .  "F" "U" .  "G" "I" .  "H" "D" .  "J" "H" .
  "K" "T" .  "L" "N" .  ":" "S" .  "\"" "_" .
  "Z" ":" .  "X" "Q" .  "C" "J" .  "V" "K" .  "B" "X" .  "N" "B" .
  "<" "W" .  ">" "V" .  "?" "Z" .
end
```

A key string is one key, written as text, so punctuation keys need no escaping beyond `\"` and
`\\`. The space key is written `" "`.

### Hangul layouts (the v4 grammar)

Assign a jamo to each key and state the combining rules; the automaton does the rest (inline
preview, commit, jamo-wise backspace, hanja conversion). Layout files are written in the **v4
grammar**: `rem` comments, forms closed by ` .`, and words instead of symbols.

**What you write depends on whether the layout is two-set or three-set.**

```lowlayout
rem Two-set: the key names the jamo; the automaton decides the slot
layout name "my dubeolsik" .
layout format 4 .
engine hangul .

map jamo do
  "r" "ㄱ" .  "s" "ㄴ" .  "e" "ㄷ" .  "k" "ㅏ" .  "j" "ㅓ" .
end
map jamo do  "R" "ㄲ" .  "E" "ㄸ" .  end        rem the shift face is the key string itself

combine jong "ㄱ" "ㅅ" be "ㄳ" .                 rem a consonant cluster
combine mid  "ㅗ" "ㅏ" be "ㅘ" .                 rem a compound vowel
```

```lowlayout
rem Three-set: the file fixes the slot
map cho  do  "k" "ㄱ" .  "h" "ㄴ" .  "j" "ㅇ" .  end
map mid  do  "f" "ㅏ" .  "d" "ㅣ" .  end
map jong do  "s" "ㄴ" .  "x" "ㄱ" .  end
moachigi .                                      rem turn on order-free simultaneous input
```

**Conditional keys** - one key can give a different jamo depending on what the syllable holds. The
first line that matches wins.

```lowlayout
guard jongslot be cho and jung and not jong .
map jong when jongslot do  "f" "ㄻ" .  end      rem in the final slot it is a cluster
map mid do                 "f" "ㅏ" .  end      rem otherwise a vowel
```

A guard can ask about `cho`, `jung`, `jong`, `empty` and compare numbers (`jong eq 8`); the operators
are words (`not and or eq ne lt le gt ge`). Jamo are written as jamo letters, and numbers are
accepted too - choseong 0ㄱ..18ㅎ, jungseong 0ㅏ..20ㅣ, jongseong 1ㄱ..27ㅎ.

`jamotong --export @ko_2bul -o mine.jmt` writes a built-in layout out as a v4 file to start from, and
six layouts (`layout-*.jmt`) are already in the install folder.

**A complete layout.** Here is the standard two-set (두벌식) keyboard in full - every key and every
combining rule. This is the file `jamotong --export @ko_2bul` writes, and `layout-ko-2bul.jmt` in the
install folder:

```lowlayout
layout name "ko_2bul" .
layout format 4 .
engine hangul .

rem two-set: the key says the jamo, the automaton picks the slot
map jamo do
  "A" "ㅁ" .  "B" "ㅠ" .  "C" "ㅊ" .  "D" "ㅇ" .  "E" "ㄸ" .  "F" "ㄹ" .
  "G" "ㅎ" .  "H" "ㅗ" .  "I" "ㅑ" .  "J" "ㅓ" .  "K" "ㅏ" .  "L" "ㅣ" .
  "M" "ㅡ" .  "N" "ㅜ" .  "O" "ㅒ" .  "P" "ㅖ" .  "Q" "ㅃ" .  "R" "ㄲ" .
  "S" "ㄴ" .  "T" "ㅆ" .  "U" "ㅕ" .  "V" "ㅍ" .  "W" "ㅉ" .  "X" "ㅌ" .
  "Y" "ㅛ" .  "Z" "ㅋ" .  "a" "ㅁ" .  "b" "ㅠ" .  "c" "ㅊ" .  "d" "ㅇ" .
  "e" "ㄷ" .  "f" "ㄹ" .  "g" "ㅎ" .  "h" "ㅗ" .  "i" "ㅑ" .  "j" "ㅓ" .
  "k" "ㅏ" .  "l" "ㅣ" .  "m" "ㅡ" .  "n" "ㅜ" .  "o" "ㅐ" .  "p" "ㅔ" .
  "q" "ㅂ" .  "r" "ㄱ" .  "s" "ㄴ" .  "t" "ㅅ" .  "u" "ㅕ" .  "v" "ㅍ" .
  "w" "ㅈ" .  "x" "ㅌ" .  "y" "ㅛ" .  "z" "ㅋ" .
end

combine mid "ㅗ" "ㅏ" be "ㅘ" .
combine mid "ㅗ" "ㅐ" be "ㅙ" .
combine mid "ㅗ" "ㅣ" be "ㅚ" .
combine mid "ㅜ" "ㅓ" be "ㅝ" .
combine mid "ㅜ" "ㅔ" be "ㅞ" .
combine mid "ㅜ" "ㅣ" be "ㅟ" .
combine mid "ㅡ" "ㅣ" be "ㅢ" .
combine jong "ㄱ" "ㅅ" be "ㄳ" .
combine jong "ㄴ" "ㅈ" be "ㄵ" .
combine jong "ㄴ" "ㅎ" be "ㄶ" .
combine jong "ㄹ" "ㄱ" be "ㄺ" .
combine jong "ㄹ" "ㅁ" be "ㄻ" .
combine jong "ㄹ" "ㅂ" be "ㄼ" .
combine jong "ㄹ" "ㅅ" be "ㄽ" .
combine jong "ㄹ" "ㅌ" be "ㄾ" .
combine jong "ㄹ" "ㅍ" be "ㄿ" .
combine jong "ㄹ" "ㅎ" be "ㅀ" .
combine jong "ㅂ" "ㅅ" be "ㅄ" .
```

Jamo are written as the jamo letters themselves. The shift faces are ordinary keys here - `"Q"` is
the key that gives `Q` on a US keyboard - so nothing has to say "shift".

- **Without `moachigi`** (the default, sequential): jamo are entered one keystroke at a time in
  order — the usual typing style.
- **With `moachigi .`** (simultaneous / "moa-chigi"): several keys pressed together form one
  syllable, and `combine` rules match **regardless of order** (`"ㄱ" "ㅅ"` also matches
  `"ㅅ" "ㄱ"`). Use this for simultaneous-stroke Sebeolsik variants.

Up to 256 `combine` rules per layout.

### Chord layouts (the v4 grammar)

A small set of "chord keys" is pressed **together and released** to perform an action —
the model used by one-hand keyboards such as ARTSEY. Actions are delivered as real
key/mouse events, so they work in any app and even outside Hangul mode. Chord layouts are
written in the same **v4 grammar** as hangul layouts: `rem` comments, forms closed by ` .`,
`do … end` blocks, and words instead of symbols.

```lowlayout
layout name "ex_chord" .
layout format 4 .
engine none .

keys "arts" "eyio" .        rem the keys this layout uses, in bit order (0..31)
chordterm 50 .              rem how long to wait for a bigger chord (ms, 1-1000)
holdterm 200 .              rem tap/hold threshold (ms, 1-5000)
holdpolicy interrupt .      rem interrupt: another key confirms the hold; timeout: only after holdterm

chord "a"   be text "a" .           rem one key
chord "ar"  be text "b" .           rem a+r together
chord "art" be text "the" .         rem three keys = a whole word
```

`keys` comes before the chords and gives every key its bit, in the order written. A chord is
simply the string of the keys it uses. Up to 16 layers and 2048 chords; the same chord written
twice in one file is an error.

#### Actions (after `be`)

| Form | Meaning |
|---|---|
| `text "the"` | Types the text (**at most 23 characters**). Escapes are the closed v4 set: `\n` `\t` `\\` `\"` `\xNN` `\uXXXX` `\UXXXXXXXX` … |
| `key enter` | One special key. Names below. |
| `key f4 (mods ctrl alt)` | The key with modifiers: `shift ctrl alt gui` and the `r…` right-hand names. |
| `mod shift` / `layer num` | The short form: on a `chord` it is one-shot, on a `hold` it lasts while held. |
| `oneshot (mod shift)` | **One-shot modifier** — the next chord only (`sticky` means the same). |
| `oneshot (layer num)` | **One-shot layer** — the next chord is looked up there. |
| `momentary (layer num)` | The layer (or `mod`) applies **while the chord is held**. |
| `toggle (layer mouse)` | Switch the layer on; the same chord switches it back to `base`. |
| `switch (layer base)` | Go to the layer and stay. |
| `pointer (move 12 0)` | Move the pointer by (dx, dy) pixels; add `(profile fast)` — `slow` `normal` `fast` `scroll`. |
| `pointer (click left)` | Mouse button: `click` / `down` / `up` / `dragtoggle`, with `left` `right` `middle`. |
| `pointer (wheel 0 -1)` | Scroll (`wheel 1 0` scrolls right). |
| `macro label` | Run a macro defined by `macro label do … end`. |
| `cancel` | Stop moving, drop a held drag, cancel a running macro. |

A minus sign attached to digits is part of the number (`move -12 0`); `pointer move` takes
-10000…10000. A word cannot hold a minus sign in this language, so the third-generation
`drag-toggle` is written `dragtoggle`. Key names accepted by `key`:

```
Navigation : left right up down home end pgup pgdn ins del
Editing    : back enter tab space esc
Numpad     : kp0-kp9 kpadd kpsub kpmul kpdiv kpdot kpenter
Modifiers  : lshift rshift lctrl rctrl lalt ralt lwin rwin   (as plain keys)
Locks/misc : caps numlock scroll pause apps prtsc sleep
Media      : volup voldown mute mplay mnext mprev mstop
Browser    : browserback browserfwd browserrefresh browserhome mail calc mediasel
Function   : f1 … f24  (including the invisible f13-f24)
Single char: any letter/digit, e.g.  key x
```

#### Layers and macros

```lowlayout
chord "ay" be oneshot (layer num) .   rem the next chord only
chord "as" be toggle (layer mouse) .  rem on/off

layer num do
  chord "a" be text "1" .
  chord "r" be text "2" .
end

layer mouse do
  chord "a"  be pointer (move -20 0) .
  chord "ar" be pointer (click left) .
  chord "as" be toggle (layer mouse) .   rem press again to return to base
end

macro label do                 rem a finite, cancelable sequence (up to 128 steps, 3 s)
  text "item: " .
  key left .
  wait 40 .                    rem ms (1-2000); the IME never blocks while waiting
  with (mods ctrl shift) .
  key right .
  endwith .
end
chord "ao" be macro label .
```

A `chord` inside `layer … do … end` belongs to that layer; outside any block it belongs to
`base`. Layers may be used before they are defined. A macro is defined before the chord that
points at it. One macro runs at a time; any other key, `cancel`, a focus change or a layout
switch stops it and releases the modifiers it pressed. There is no repetition, no condition, and
nothing that reads the clipboard or files.

#### Tap vs. hold

The same chord can do two things — a quick **tap** (`chord`) and a **hold** (`hold`):

```lowlayout
chord "e" be text " " .
hold  "e" be momentary (layer num) .   rem while held: the num layer
chord "y" be key back .
hold  "y" be mod lctrl .               rem while held: other keys arrive as Ctrl+<key>
```

- A `hold` whose action is `mod`/`layer` is **sustained**: it turns on when another key is
  pressed while you keep holding, and reverts on release. Keeping the keys pressed without
  touching anything else also turns it on after `holdterm`.
- Any other hold action (`text`, `key`, `pointer`) fires **after `holdterm`**, instead of the
  tap action, when you release. If a chord has only a `hold` entry, it fires on release
  regardless of timing.
- **Bigger chords win**: with `hold "ar"` and `chord "art"`, pressing a r t within `chordterm`
  types the three-key action; the `ar` hold only starts when no bigger chord can still form.
- **Rolling**: the first release closes a chord. A new key pressed before the rest are released
  fires the closed chord first and starts the next one.
- **Pointer**: `pointer (move …)` / `(wheel …)` on a `hold` runs while you hold — it accelerates
  to the profile's limit, opposite directions cancel and diagonals are normalized. On a `chord`
  it happens once. A drag started with `dragtoggle` is released by the same chord, by `cancel`,
  or when focus or layout changes.

#### Complete example

`example-artsey.jmt` in the distribution shows all of the above in one working file: letters,
word chords, Backspace/Space/Enter, one-shot Shift, one-shot and momentary number layers, a
toggled mouse layer (pointer movement, clicks, wheel), tap/hold pairs and media keys. Copy it,
keep the `keys` declaration, and fill in your own chord table (e.g. the published ARTSEY map).
Its `note DOC … DOC` header summarises the grammar in the file itself.

#### Sequence input (v3)

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

**Korean layouts that come with it.** Six layouts are installed alongside the program: Dubeolsik
standard, Sebeolsik Final (3-91), Sebeolsik 390, Sebeolsik Final shift-free, Sebeolsik 3-2011 and
Sebeolsik 3-2012. Each was typed from its published arrangement table by Jamotong (see
`COPYRIGHT.md`). To adapt one, copy `layout-*.jmt` from the install folder into
`%APPDATA%\Jamotong\layouts` and edit it there; the manager app rebuilds it.

**Bringing in a Windows keyboard layout.** If a layout you want already exists as a Microsoft
Keyboard Layout Creator source (`.klc`), convert it:

```sh
jamotong --import-klc us-dvorak.klc -o dvorak.jmt
jamotong --check dvorak.jmt
```

Each key's normal and Shift faces are carried over into a static layout. Dead keys, ligatures and
the AltGr face are not — they are counted and named in the report so you know what to add by hand.

**Bringing in a Nalgaeset layout.** Most Korean three-set (세벌식) layouts are shared as Nalgaeset
input method files (`.key` for the key table, `.ist` for a whole input scheme). The **key table**
is carried over into a hangul layout:

```sh
jamotong --import-ngs "sebeol 3-2012.key" -o 3-2012.jmt
jamotong --check 3-2012.jmt
```

The result is written in the **v4 grammar**. Jamo keys are carried over, and a key that gives a
different jamo **depending on the slot** (shift-free and alternating layouts) becomes a `when
jongslot` guard: a final consonant in the final slot, a vowel in the vowel slot, an initial
otherwise. Nalgaeset's condition bytes themselves are **not read** - they are its own automaton's
state numbers - so what is used is only the shape, "which slots can this key fill". Type with an
imported layout once to confirm it. The cluster and compound-vowel tables are added from the standard
modern Hangul rules, because the Nalgaeset file does not carry them. What is not carried over is
counted: formulas with no jamo (symbols and functions), character keys in a hangul layout, jamo codes
we do not know, and every automaton and option.

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

**Japanese (romaji).** The **Japanese** feature of the installer (0.70.0) adds **Japanese (romaji)**,
switched on. Type romaji (`nihongo`, `kka` for っか, `-` for ー); the reading
shows as kana. **Space** converts: the first candidate is the whole sentence (`denshadekaishaniiku` →
電車で会社に行く), then the words for the reading, hiragana and katakana. 1-9 pick, **Enter** types the kana as
they are, Esc clears. **Segments (0.73.0)**: when the sentence has two or more segments, Space shows it
converted with the segment being edited in brackets (`[私は]日本語を話します`) and the candidates for that
segment. **←/→** move to another segment, **Shift+←/→** make it shorter or longer (the rest is cut again),
**↑/↓** or Space move through the candidates, 1-9 pick one and go to the next segment, **Enter** types the
sentence, Backspace or Esc go back to the kana; typing on types the sentence first. Nothing is learned from
what you pick. **F6** hiragana, **F7** katakana, **F8** half-width katakana, **F9** full-width and
**F10** half-width romaji. Punctuation comes as 、。「」・〜！？ (Layout Options can turn it off). **Custom
phrases**: Layout Options ▸ Edit custom phrases opens `japanese-phrases.txt`, one per line, the reading in
kana then the text (`よろ よろしくお願いします`); they come first. The dictionary is the open-source Mozc
dictionary (about 500,000 entries) with its part-of-speech connection costs (0.71.0): the sentence is chosen
as Mozc chooses it, word costs plus the cost of each word following the one before (私は日本語を話します,
友達と映画を見ました) - 42 of our 50 test sentences come out right at once (15 in 0.70.0). Words that need
wider context (降る/フル, 重い/思い) may need picking from the candidates; there is no learning. Its licences are installed in `licenses\japanese`.

**Chinese (pinyin).** Two features of the installer (0.70.0; separate MSIs from 0.62 to 0.69): **Chinese
Simplified** and **Chinese Traditional**. Each adds its layout - **Chinese Simplified (pinyin)**
or **Chinese Traditional (pinyin)** - switched **on** in your layout list (if you switch it off, it stays
off). Type pinyin without tones; as in other Chinese input methods the **candidates show while you type**.
Space takes the highlighted one, 1-9 pick, `-` `=` turn pages, `[` `]` take only the first or last
character of the word (以词定字), Enter types the letters as they are and Esc clears. The first candidate
is the **whole sentence** (`woaini` → 我爱你, `jintiantianqihenhao` → 今天天气很好); then the reading's own
words and **suggestions** - longer words and idioms that start with what you typed (`yijian` → 一箭双雕) and
words typed by their **initials** (`zg` → 中国, `wsm` → 为什么, `y` → 有). Pick a word that covers only the
front and the rest stays, its candidates open at once. `'` separates syllables (`xi'an` → 西安), `ü` is
`v` (`lv` → 绿), and `rq`, `sj`, `xq` give the date, time and weekday. Punctuation becomes Chinese
(，。？！、“”‘’ - 「」『』 in traditional), except `.` `,` `:` right after a digit (3.14). The traditional
layout uses Taiwan forms (裡, 為, 眾) and offers every traditional form of a character (发 → 發, 髮). Settings
→ **Layout Options** switches, per layout, the sentence candidate, the suggestions, the Chinese
punctuation and fuzzy pinyin (z=zh, n=l, an=ang …), and picks the **keys**: full pinyin or a double-pinyin
scheme - 小鹤 (Xiaohe), 自然码 (Ziranma) or 微软 (Microsoft), two keys per syllable. `v` then digits (Shift+V
with double pinyin) is the **V mode**: `v123` → 一百二十三 / 壹佰贰拾叁, `v3.14` → 三点一四, `v2026.10.3` → a
date, `v1+2*3` → 7. "Edit custom phrases..." opens `chinese-phrases.txt`: one line per phrase, letters then
the text (`dz 北京市海淀区`), offered first. **Emoji and symbols** come after their word (`kaixin` → 开心 ☺ 😄,
`dianhua` → ☎ 📞; names from Unicode CLDR), and the candidates show **pinyin with tones** (你 nǐ, 中国 zhōng guó);
both can be switched off. Readings come from mozillazg's pinyin-data and phrase-pinyin-data, the order from jieba's word
frequencies (all MIT), the traditional forms from OpenCC (Apache-2.0); the licenses are installed in
`licenses\chinese-*` beside Jamotong. No native speaker has reviewed it yet.

- Limits: the typed side of a sequence dictionary is printable ASCII up to 32 characters, a
  candidate reading is up to 96 bytes of UTF-8, one entry emits up to 64 characters, and a
  dictionary holds up to 500,000 entries. A dictionary that uses a reading longer than 32 bytes is
  written as v2, which Jamotong 0.44 and older refuse to load rather than misread.
- **Loading a layout checks its dictionary in full** — the checksum, the key order and the key
  characters — so a damaged dictionary means the layout is not offered at all, wherever it came
  from. The check reads the whole file once per load (about 7 ms for 100,000 entries).
  `jamotong --check layout.jmt` runs exactly the same check and names the dictionary it used.


## Uninstall

1. **Settings ▸ Apps ▸ Installed apps ▸ Jamotong ▸ Modify ▸ Remove.** (Uninstall is greyed out on purpose: it would
   run Windows' bare window, in English with Cancel as the default.) It unregisters both text services and
   removes the files. (Installed by hand from the zip: run `jamotong.exe --unregister` from an
   administrator prompt, then delete the folder.)
2. A DLL still loaded in running apps cannot be deleted; Windows moves it aside and removes it at the
   next restart. A reboot is not required — running apps keep the old copy until they exit.
3. Your settings remain at `%APPDATA%\Jamotong`; delete that folder too if you do not
   plan to reinstall.

## Build

Cross-compiled with MinGW-w64 (on Linux):

```sh
make            # dist/jamotong.dll (x64)
make win32      # dist/jamotong32.dll (x86)
make configapp  # dist/jamotong.exe (manager: .jmt editor / settings / input test)
make stage      # build everything + copy redist/ into dist/
                #  -> dist/ holds the files to install (see Install: --register)
```

`redist/` contains the redistributable data required at runtime: the hanja reading
table (`hanja.txt`), the meaning/reading table (`hanja_hunum.txt`), a copy of the
Unicode License, and the sample and shipped `.jmt` layouts.

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
