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
  layouts via `.jmt` files (static map / hangul automata / chord). Configurable layout
  cycling hotkeys (default: Hangul key, Right Alt, Shift+Space).
- **Hanja conversion**: compose a syllable and press the Hanja key — or **select text
  first, then press Hanja** (works in legacy apps too). The candidate window shows
  meaning/reading (hunum). Reading data: Unicode Unihan-derived (8,900+ characters,
  educational-basic-hanja first ordering) + 331 curated words + 1,784 hunum entries.
- **Special characters**: consonant + Hanja key (Mieum = symbols, Siot = Greek,
  Jieut = Roman numerals, etc. — the familiar Korean IME convention).
- **Unicode codepoint input**: `Ctrl+Alt+U` (configurable, multiple shortcuts) → type hex (live preview) → Enter.
- **Settings**: `Ctrl+Alt+K` or run `jamotong.exe` — layouts, hotkeys, options.
  Stored at `%APPDATA%\Jamotong\config.ini`, with import/export.
- **Tray monitor** (`jamotong.exe`): shows the current layout in the system tray
  (blue = Jamotong active, gray = inactive); left-click = settings, right-click = menu.
- Both 64-bit and 32-bit applications are supported (separate DLLs).
  Windows 11 input-indicator branding icon included.

## Install

**Download the latest zip from [Releases](https://github.com/rubidus-api/jamotong_ime/releases)**,
extract it, and run `install.bat` **as administrator**. Sign out and back in, then press
Win+Space and select "Jamotong IME". See `README-install.txt` for details.

## Build

Cross-compiled with MinGW-w64 (on Linux):

```sh
make            # dist/jamotong.dll (x64)
make win32      # dist/jamotong32.dll (x86)
make configapp  # dist/jamotong.exe (tray monitor / settings app)
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

## License

Code: [MIT License](LICENSE). Hanja reading data is derived from the Unicode Unihan
Database (Unicode License v3; notice bundled in the distribution). No GPL/LGPL/CC BY-SA
material is used.
