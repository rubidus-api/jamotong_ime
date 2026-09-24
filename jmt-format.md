# Jamotong file formats — reference

Everything Jamotong loads comes from files you can read and edit. This page is the complete
reference: the source formats, what each directive means, the build step, and every message the
tools can print. The README has the friendly introduction and worked examples.

## The four files

| File | What it is | Who writes it | Who reads it |
|---|---|---|---|
| `*.jmt` | **layout source** — UTF-8 text you edit | you | the compiler (`jamotong --build`) |
| `*.jdt` | **dictionary source** — UTF-8 text, one entry per line | you | the compiler (`jamotong --build-dict`) |
| `*.jmb` | **built layout** — binary the IME reads | the compiler | the IME |
| `*.jdb` | **built dictionary** — binary the IME maps | the compiler | the IME |

The IME reads only the built files. Compiling checks the whole source, so a layout that appears in
the list is one that loaded cleanly — including its dictionary. Building happens for you:
`install.bat` builds what ships and what is already in the layout folders, and the manager app
(`jamotong.exe`) builds new or edited sources when it starts and after Settings → Apply.

```sh
jamotong --check      layout.jmt        # check the source; says whether the build is current
jamotong --build      layout.jmt        # writes layout.jmb beside it
jamotong --build-dir  <folder>          # builds what is missing or older than its source
jamotong --build-dict words.jdt -o words.jdb
jamotong --import-dict other.txt -o words.jdt [--limit N] [--name ..] [--license ..]
jamotong --import-klc us-dvorak.klc -o dvorak.jmt        # a Windows layout source -> a static .jmt
jamotong --import-ngs sebeol.key -o sebeol.jmt            # a Nalgaeset layout file -> a .jmt
jamotong --expand     layout.jmt -o flat.jmt    # resolve Extends/Include into one file
jamotong --export     @ko_3bul -o ko.jmt        # write a built-in layout as a source file
```

Where files are looked up, in order: beside the layout file, `%APPDATA%\Jamotong\layouts`
(dictionaries: `...\dicts`), `%PROGRAMDATA%\Jamotong\...`, then the install folder. A dictionary is
named by a plain file name ending in `.jdb` — no folders in the name.

## Layout source (`.jmt`) - **the v4 grammar**

Hangul layouts and static layouts are written in this grammar. Its surface comes from lowent, the
systems language written alongside this project.

```lowlayout
rem a comment runs to the end of the line. There is no symbol comment marker.
note DOC
  A block comment. It ends on a line that holds its tag alone.
DOC

layout name "Sebeolsik Final" .
layout format 4 .
engine hangul .                        rem hangul | none

map cho do  "k" "\u3131" .  "h" "\u3134" .  end
map mid do  "f" "\u314F" .  "r" "\u3150" .  end
map jong do "x" "\u3131" .  "s" "\u3134" .  end

combine jong "\u3139" "\u3131" be "\u313A" .   rem two jamo typed in a row become one
combine cho  "\u3131" "\u3131" be "\u3132" .   rem the same initial twice is a tense consonant
```

### Lexical rules

- **Comments**: `rem` to the end of the line, `note <tag> ... <tag>` for several lines. There is no
  `#` or `//`.
- **Closers**: a form closes with ` .`. **A newline does not close anything**, so a long table may be
  broken across lines freely. The `do` that opens a block also closes its head form, and a block ends
  with `end` (no dot after it).
- **The only punctuation is the dot and parentheses.** Symbols like `=` or `&&` are not in this grammar.
- **Literals**: `42` `1_000` `0x2A` `0b1010` (a leading zero is not octal) · `'a'` (a code value) ·
  `"text"` `u"..."` `U"..."`. The escape set is the closed fourteen (`\n` `\t` `\xNN` `\uXXXX`
  `\UXXXXXXXX`, ...).
- **Names** are ASCII letters, digits and underscore. **An unknown directive is an error** - a typo
  never quietly changes a layout.

### Writing keys and jamo

- **A key is a string**: `"k"` is that key, `"Q"` is shift+q (the shift face is the key string
  itself), `"!"` is shift+1. A physical key is `(vk enter)` or `(scan 0x10)`. **There are no hidden
  defaults** - a shift face has to be written even when it carries the same jamo.
- **A jamo is written as the jamo letter**: `cho "\u3131"`, `mid "\u314F"`, `jong "\u3133"`. A number
  (`cho 0`) is accepted too.

| Written as | Meaning |
|---|---|
| `map jamo do "r" "\u3131" . end` | **Two-set** - this key carries the jamo. Whether it lands in the initial, final or cluster slot is **the automaton's decision** |
| `map cho` · `map mid` · `map jong` | **Three-set** - the file fixes the slot |
| `map char do "q" "a" . end` | A character rather than a jamo (static layout) |
| `combine jong "\u3134" "\u3148" be "\u3135" .` | Two jamo become one (clusters, compound vowels, tense consonants) |

### Conditional key values - `when`

A key may give a different jamo depending on what the syllable already holds. A guard attaches to
`key` and `map`, and **the first line that matches wins** (write the narrow one first). A line
without a guard is the default.

```lowlayout
guard jongslot be cho and jung and not jong .

map jong when jongslot do  "f" "\u313B" .  end    rem in the final slot it is a cluster
map mid do                 "f" "\u314F" .  end    rem otherwise it is a vowel
```

What can be asked: `cho` `jung` `jong` `empty`, and comparisons by number (`jong eq 8`).
The operators are words - `not` `and` `or` `eq` `ne` `lt` `le` `gt` `ge` - with parentheses for
grouping; `and` binds tighter than `or`. A guard is **compiled into a small program when the file is
read** and travels into the built layout (`.jmb`), so nothing extra happens while typing.

Limits: an expression holds at most 64 tokens and 8 levels of parentheses; a layout holds at most 128
guarded keys.

### Chord layouts - keys pressed together do something

```lowlayout
layout name "eight keys" .
layout format 4 .
engine none .

keys "arts" "eyio" .            rem the chord keys and their bit order (before any chord)
chordterm 50 .                  rem how long to wait for a bigger chord (ms)
holdterm  200 .                 rem tap versus hold (ms)
holdpolicy interrupt .          rem interrupt | timeout

chord "ar" be text "b" .        rem tapped and released
hold  "e"  be momentary (layer num) .   rem that layer only while it is held

layer num do
  chord "a" be text "1" .
end

macro sign do
  text "hello" .
  key enter .
end
chord "arts" be macro sign .
```

The actions are: `text "..."` · `key enter` · `key f4 (mods ctrl alt)` ·
`mod shift` / `layer num` (one-shot on a chord, held on a hold) · `oneshot (mod shift)` (also
spelled `sticky`) · `momentary (layer num)` · `toggle (layer x)` · `switch (layer x)` ·
`pointer (move 12 0)`, `pointer (click left)`, `pointer (wheel 0 1)` with an optional
`(profile fast)` · `mouse move 12 0` · `macro <name>` · `cancel`.

A minus sign **attached** to digits makes a negative number (`pointer (move -12 0)`); standing alone
it is a symbol this language does not have.

### What still uses the older grammar

**Sequential input layouts (`Type = input`, the dictionary and sequence engines) are written in the
1-3 grammar below.** Jamotong reads both: a file with a `layout` form is read as v4, otherwise as the
older grammar. `jamotong --export` writes hangul and static layouts in v4 (chord layouts still export
in the older grammar).

---

## Layout source, versions 1-3



```ini
# a comment; blank lines are ignored
FormatVersion    = 3          # 1, 2 or 3 (omitted = 1)
Type             = input      # static | hangul | chord | input   (omitted = hangul)
Engine           = sequence   # Type = input only
RequiresJamotong = 0.40.0     # required in format 3
Name             = my layout  # shown in the layout list
Abbrev           = MINE       # 1-4 characters for the tray icon
```

Header keys: `FormatVersion`, `Type`, `Engine`, `Id`, `Name`, `Abbrev`, `Version`, `Author`,
`License`, `Homepage`, `Description`, `Locale`, `RequiresJamotong`. Unknown header keys are a
warning; an unknown *directive* is an error in format 2 and later.

`Extends = other.jmt` takes another layout of the same kind as the base (one line, at most four
levels deep, no loops) and `Include = part.jmt` pastes a fragment. Lines that come later win.

A long table can drop its repeated prefix with a block:

```text
Begin Combine M
  8 0 = 9
  13 20 = 14
End
```

Every line inside is read as if the words after `Begin` were written in front of it, so the block
above is exactly two `Combine M ...` lines. Blocks do not nest, and there are no loops or
variables — one line is still one directive.
`@ko_3bul`, `@en_dvorak` and `@en_qwerty` name built-in layouts.

### `Type = static` — one character for one key

```ini
Map qwer = asdf          # each key on the left types the character at the same position
Map @SC10 shift = W      # a physical key: US position (@Q), scan code (@SC10) or VK name
Identity = passthrough   # the layout types exactly what the keyboard produces
```

### `Type = hangul` — Hangul automata

```ini
Composition = sebeol     # sebeol (default) or dubeol
Moachigi    = 1          # optional: press the jamo of a syllable together
Key k = C1               # C = initial, M = medial, T = final; the number is the jamo index
Combine C 1 2 = 3        # two jamo combine into one (per type)
```

`Composition = dubeol` moves a consonant to the next syllable the way two-set keyboards do; it has
no direct final keys and no Moachigi.

### `Type = chord` — chords make actions

```ini
Key jkl; = 0             # declare the chord keys: j gets bit 0, k bit 1, ...
ComboTermMs = 50         # format 3: how long to wait for a bigger chord (1-1000)
HoldTermMs  = 200        # format 3: tap/hold threshold (1-5000)
HoldPolicy  = interrupt  # interrupt (another key confirms a hold) or timeout
Layer num                # chords after this line belong to layer "num"
Chord jk = text "the"    # a chord (all keys released)
Hold  jk = momentary layer(num)   # while held
Hold  ;  = oneshot mod(shift)    # hold to arm Shift for the next chord, then let go
Macro name ... EndMacro  # a finite sequence: text, key, pointer, wait, with mods(...) / endwith
```

### `Type = input` — an engine, optionally with chords in front

```ini
Engine      = sequence            # the only engine today
Dictionary  = romaji-kana.jdb     # the built dictionary this layout uses
OnUnmatched = flush               # flush (default) types the pending letters, cancel drops them
Candidates  = kana-words.jdb      # optional: a reading -> several candidates
ConvertKey  = space               # required with Candidates: space | tab | hanja | convert | f9
Key jkl; = 0                      # optional chord front end
Chord jk = symbol "k"             # its result goes into the engine, not to the application
```

With `Candidates` the letters collect in a reading the IME owns instead of going straight into the
document, and the convert key offers the candidates for that reading. Choosing one replaces the
reading; cancelling keeps it. Backspace takes back one letter of the reading, Esc drops it, and a
boundary commits it as it is. A choice that arrives after the reading changed is dropped.

The engine waits for the longest match, so with `n`, `na` and `ni` in the dictionary a lone `n`
waits. Pending letters are shown next to the caret, not inserted. Backspace takes back one pending
letter, Esc drops them, and Space/Enter/Tab/arrows belong to the application (the pending letters
are typed first). A chord front end decides before the engine does, so one key is never consumed
twice, and a `symbol` never goes back into the chord recognizer.

### Actions (format 3)

| Action | Meaning |
|---|---|
| `text "..."` | exact string, straight to the document |
| `symbol "..."` | logical input for the engine (input layouts only) |
| `key NAME [mods(ctrl,shift,alt,gui,lctrl,...)]` | a real key event |
| `oneshot mod(x)` / `oneshot layer(x)` | applies to the next chord only; on `Hold` it is armed when the hold fires and stays armed after you let go |
| `momentary mod(x)` / `momentary layer(x)` | only while the keys are held (`Hold` only) |
| `toggle layer(x)` / `switch layer(x)` | change the current layer |
| `pointer move(dx,dy) [profile(slow\|normal\|fast)]` | move the mouse (continuous on `Hold`) |
| `pointer wheel(dx,dy) [profile(scroll)]` | scroll |
| `pointer click\|down\|up\|drag-toggle(left\|right\|middle)` | mouse buttons |
| `cancel actions` | stop continuous motion, release a held drag, cancel a macro or one-shot |
| `macro <name>` | run a macro block |

Strings are `"..."` with the escapes `\"`, `\\`, `\n`, `\t` and `\u{hex}`. Outside quotes, `#`
starts a comment.

## Dictionary source (`.jdt`)

```text
JamotongData 1
Type = sequence
Name = romaji kana
Version = 1.0.0
License = CC0-1.0
Source = where these entries came from
# a comment
ka	か
kya	きゃ
```

`Type` is `sequence` (keys to letters) or `candidates` (a reading to several candidates — repeat
the same reading once per candidate, in the order you want them offered). The first line is fixed.
Each data row is the key, a **tab**, and what it produces; `\u{hex}` works on both sides, and a
candidate dictionary's reading may be any script. Building sorts the entries, rejects duplicates, and writes a binary the
IME maps read-only; opening it checks the header and the index, and a layout that uses it checks
the whole file (checksum, order, key characters) before the layout can be used.

## Limits

| Thing | Limit |
|---|---|
| line in a `.jmt` | 255 characters |
| lines in a `.jmt` (after Extends/Include) | 20,000, at most 12 files |
| `Extends` depth | 4 |
| chords per layout / layers / macros / macro steps | 2048 / 16 / 8 / 128 |
| chord text (format 2) | 23 characters |
| dictionary: typed side / output / entries | 32 / 64 characters, 500,000 entries |
| layouts in the list | 8 |

## Messages

Every diagnostic prints as `file:line:column: severity: message [CODE]` with a `help:` line when
there is something to suggest. Warnings do not stop a load; errors do.

| Code | Meaning | What to do |
|---|---|---|
| `E-DICT-DUP` | this key is defined twice | keep one of the two rows |
| `E-DICT-EMPTY` | the source has no entries | write one 'keys TAB output' line per entry |
| `E-DICT-KEY` | the typed side takes printable ASCII without spaces | this is what is pressed on the keyboard |
| `E-DICT-HEAD` | Name (127), License (255) or Version (63) is longer than the dictionary can hold | name the licence here and keep its full text in a file beside the dictionary |
| `E-DICT-KIND` | unknown dictionary Type | Type = sequence |
| `E-DICT-LIMIT` | too many entries (max 500000) | - |
| `E-DICT-LINE` | a line is longer than 1023 bytes | one entry per line |
| `E-DICT-MAGIC` | this is not a Jamotong dictionary source | the first line must be 'JamotongData 1' |
| `E-DICT-MEMORY` | out of memory | - |
| `E-DICT-OPEN` | cannot open the dictionary source | - |
| `E-DICT-ROW` | a data row must be <keys> TAB <output> | e.g. 'ka' then a tab then the letters it types |
| `E-DICT-VALUE` | the output side cannot be used (bad escape, lone surrogate, NUL or too long) | write the letters directly or as \u{hex} |
| `E-DICT-VERSION` | this source uses a newer dictionary format | update Jamotong to build it |
| `E-DICT-WRITE` | cannot write the dictionary file | check that the folder exists and is writable |
| `E-JMB-KIND` | this layout kind cannot be built | - |
| `E-JMB-MEMORY` | out of memory | - |
| `E-JMB-WRITE` | cannot write the built layout | check that the folder exists and is writable |
| `E-JMT-ACTION` | unknown macro step | steps: text "...", key NAME [mods(...)], pointer ..., wait <ms>, with mods(...) / endwith |
| `E-JMT-ASCII` | Key: key must be an ASCII character | - |
| `E-JMT-BLOCK` | Begin inside another Begin block | close the first block with End |
| `E-JMT-BUILTIN` | this built-in layout cannot be extended | ko_2bul keeps its rules in the engine, so it cannot be written as a file |
| `E-JMT-COUNT` | Key: number of specs must equal number of keys | one spec per key, e.g. 'Key khj = C0 C2 C11' |
| `E-JMT-CYCLE` | Extends forms a loop | remove the loop between these files |
| `E-JMT-DEPTH` | Extends chain is deeper than 4 | flatten a level with 'jamotong.exe --expand' |
| `E-JMT-DICT` | the dictionary must be a plain file name ending in .jdb | no folders in the name - the file lives beside the layout or in the dictionary folder |
| `E-JMT-DICT-BAD` | this layout has no usable dictionary | - |
| `E-JMT-DICT-KIND` | this dictionary is not a sequence dictionary | build it with 'Type = sequence' |
| `E-JMT-DICT-MISSING` | the dictionary named here was not found | put it beside the layout or in the dictionary folder |
| `E-JMT-DUBEOL` | Composition = dubeol: a key cannot type a final consonant (T) directly | use C keys - dubeol turns them into finals from context |
| `E-JMT-DUP-CHORD` | same chord defined twice in this file | remove one of the two lines (in format version 3 this is an error) |
| `E-JMT-ENCODING` | line is not valid UTF-8 (or contains U+FFFD) | save the file as UTF-8 |
| `E-JMT-ENGINE` | 'Type = input' needs an engine this Jamotong has | add 'Engine = sequence' |
| `E-JMT-EXTENDS-INCLUDE` | an included fragment cannot use Extends | put Extends in the main layout file |
| `E-JMT-EXTENDS-TWICE` | only one Extends line is allowed | use Include for extra fragments |
| `E-JMT-EXTENDS-TYPE` | the base layout is a different Type | extend a layout of the same kind |
| `E-JMT-FORMAT-NEWER` | the file uses a newer FormatVersion than this Jamotong reads | update Jamotong |
| `E-JMT-IDENTITY` | Map cannot be used with 'Identity = passthrough' in the same file | remove 'Identity = passthrough' to remap keys |
| `E-JMT-INCLUDE` | cannot include this file here | - |
| `E-JMT-KEY-SYNTAX` | Key: expected a bit number after '=' | e.g. 'Key j = 0' |
| `E-JMT-LEVEL` | a value is outside the level this directive allows | see the limits table |
| `E-JMT-LIMIT` | too many macros (max 8) | - |
| `E-JMT-LINE-LONG` | line is longer than 255 characters | split it into several lines (a key list can be written over several Key lines) |
| `E-JMT-MACRO` | no macro with this name | define it first with 'Macro <name> ... EndMacro' |
| `E-JMT-MAP-LEN` | Map: left and right sides must have the same length | write one output character for each key, e.g. 'Map qwe = abc' |
| `E-JMT-OPEN` | cannot open file | - |
| `E-JMT-BLOCK` | a Begin/End block is malformed (no directive, never closed, nested, or End on its own) | one block at a time, closed with 'End' |
| `E-JMT-PHYSKEY` | a physical key name (@Q, @SC10, @VK_OEM_1) is not valid | use a US position, a scan code or a virtual-key name |
| `E-JMT-RANGE` | Key: bit out of range (0..31) or non-ASCII key | bits 0..31; a key list takes consecutive bits from the start bit |
| `E-JMT-READ` | reading stopped here (read error or invalid UTF-8) | save the file as UTF-8; the lines after this point were not read |
| `E-JMT-REQUIRES` | this layout asks for a newer Jamotong | update Jamotong |
| `E-JMT-REQUIRES-MISSING` | a FormatVersion 3 layout must say which Jamotong it needs | add 'RequiresJamotong = 0.33.0' |
| `E-JMT-SEQ-INLINE` | a layout file cannot hold the table itself | put the entries in a dictionary source (.jdt), build it with 'jamotong --build-dict' and write 'Dictionary = name.jdb' |
| `E-JMT-STRING` | bad string: unterminated, unknown escape, NUL, surrogate or too long | write text "..." with escapes \\" \\\\ \\n \\t \\u{hex} |
| `E-JMT-SYMBOL` | 'symbol' needs an input engine | use it in a layout with 'Type = input' and an 'Engine =' line; a chord layout has no engine |
| `E-JMT-TEXT-LONG` | chord text is longer than 23 characters | shorten the text (at most 23 characters after \\n, \\t and \\s) |
| `E-JMT-TOO-LONG` | file has more than 20000 lines | - |
| `E-JMT-CHORD-KEY` | a chord uses a key the v4 chord layout never declared | declare them first: keys "arts" "eyio" . |
| `E-JMT-KIND` | a v4 value is not one of the kinds this directive takes | holdpolicy is interrupt or timeout |
| `E-JMT-SHAPE` | a v4 form has the wrong shape | write: chord "<keys>" be <action> . |
| `E-JMT-TYPE` | Key: type must be C, M or T | C = choseong, M = jungseong, T = jongseong |
| `E-JMT-TYPE-UNKNOWN` | unknown Type | Type must be static, hangul, chord or (format 3) input |
| `E-JMT-UNDECLARED` | chord references a key not declared with 'Key' | declare every chord key first, e.g. 'Key j = 0' |
| `E-JMT-UNKNOWN-DIRECTIVE` | unknown directive for this layout kind | check the spelling against the directive list |
| `E-JMT-VALUE` | Identity must be passthrough or map | passthrough = keys go through unchanged (like the built-in QWERTY) |
| `W-JMT-ABBREV-LONG` | Abbrev is longer than 4 characters - the 2x2 icon shows only the first 4 | use 1 to 4 characters |
| `W-JMT-DUP-CHORD` | same chord defined twice in this file - the first definition is used | remove one of the two lines |
| `W-JMT-EMPTY-LAYER` | a layer has no chords | remove it or give it chords |
| `W-JMT-IGNORED-LINE` | unrecognised line ignored (format 1 only) | in format 2 and later this is an error |
| `W-JMT-UNKNOWN-KEY` | unknown header key - ignored | check the spelling; the message suggests the closest key |

## See also

- README — "Custom keyboard layouts (.jmt)" for the introduction and worked examples.
- `example.jmt`, `example-dvorak.jmt`, `example-artsey.jmt` ship with Jamotong as commented samples.
