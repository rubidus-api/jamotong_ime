# Jamotong IME - Copyright and Third-Party Notices

Jamotong's own source code is released under the [MIT License](LICENSE).
This file lists the third-party material Jamotong uses, where it came from, and the
license under which it is used. It ships in every distribution zip next to `LICENSE`.

No GPL, LGPL, or CC BY-SA code or data is used.

---

## 1. Fonts

### 1.1 Windows system fonts (referenced by name only)

The language-bar and tray icons draw the current layout's short name at run time with GDI
`CreateFontW`, naming a font that is already installed with Windows (Dotum first, then the
system's own substitute). No font file is bundled or redistributed; the font is used only
to render on screen, as any application uses an operating-system font.

### 1.2 Spleen 5x8 bitmap glyphs

The icon lettering (the mode icon drawn by `src/langbar.c` and the profile icon
`src/jamotong.ico`) uses pixel data for 38 glyphs (A-Z, 0-9, `?`, `-`) derived from the Spleen
5x8 bitmap font. Only the glyph bitmaps are included, converted into `src/icon_font.h`;
the font file itself is not bundled.

- Font: Spleen 5x8, version 2.2.0 - https://github.com/fcambus/spleen
- License: BSD 2-Clause, reproduced in full:

```text
Copyright (c) 2018-2026, Frederic Cambus
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

---

## 2. Keyboard layouts

The built-in layouts (Korean 2-set and 3-set, Dvorak, QWERTY) are key-to-character
assignments, which are facts about published keyboard standards. Their primary source is
the published layout itself; the tables in `src/layout.c` were encoded independently from
it. The `.jmt` example files are original templates and carry no third-party data.

---

## 3. Hanja data

### 3.1 `hanja.txt` - readings to hanja

- Derived from the `kHangul` field of the **Unicode Unihan Database** 17.0.0.
  Copyright (c) 1991-2026 Unicode, Inc. Used under the **Unicode License v3**; the full
  text ships as `UNICODE-LICENSE.txt` (original: https://www.unicode.org/license.txt).
- Personal-name hanja missing a reading in Unihan were added from the Supreme Court of
  Korea's personal-name hanja list (public data), taken from the character set published
  as **rutopio/Korean-Name-Hanja-Charset** (MIT License). Only the character list is used.

### 3.2 Word entries and `hanja_hunum.txt` (meaning-and-sound table)

- The meaning-and-sound (hunum) table and the preferred spellings of word entries are
  Jamotong's own curation, written for this project rather than copied from any dictionary.
  Entries added for rarer characters were written from the meaning of the Unihan
  `kDefinition` field (Unicode License v3) without copying its wording.
- Additional Korean word to hanja spelling mappings come from **jemdiggity/hanja-wordlist**
  (MIT License). Only the word-to-spelling mapping is used, not its definitions.

---

## 4. Keyboard layouts (`docs/layouts/`)

A keyboard arrangement - which key carries which jamo - is a fact and a method, not an expression,
so it is not the subject of copyright. What could be is someone else's layout *file*, drawing or
prose. Jamotong therefore writes every layout it ships **from the published arrangement**, in its
own file format, and never converts another input method's configuration file into a shipped layout.

- **Dubeolsik standard** - the national standard arrangement (KS X 5002).
- **Sebeolsik Final (3-91)** - the arrangement Gong Byung-woo and the Hangul Culture Institute
  published for free use. The shipped file is checked against Jamotong's own built-in table on all
  58 keys.
- **Sebeolsik Final, shift-free (sunarae)** - derived by Jamotong from Sebeolsik Final with one
  stated rule: each shifted final consonant moves to the final-consonant slot of the same unshifted
  key.

Layouts designed by others whose authors have stated that they claim no rights (for example
Sebeolsik 3-2011 and 3-2012) may be added later, but only written afresh from their published
arrangement tables.

---

## 5. Jamotong's own code

Everything not listed above is Jamotong's own work under the MIT License (see `LICENSE`).
