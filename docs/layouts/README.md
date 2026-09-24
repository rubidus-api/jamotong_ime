# Layout sources

The Korean layouts Jamotong ships live in `redist/` and are installed with the program:

| File | Layout | Provenance |
|---|---|---|
| `redist/layout-ko-2bul.jmt` | Dubeolsik standard | The national standard arrangement (KS X 5002). |
| `redist/layout-ko-3bul-final.jmt` | Sebeolsik Final (3-91) | Published for free use by Gong Byung-woo and the Hangul Culture Institute. A test checks it against Jamotong's built-in table on all 58 keys. |
| `redist/layout-ko-3bul-390.jmt` | Sebeolsik 390 (3-90) | Same source. Six consonant clusters have no key of their own: they are typed as two final consonants in a row. |
| `redist/layout-ko-3bul-sunarae.jmt` | Sebeolsik Final, shift-free | Derived by Jamotong: the shift face is not used at all, and clusters are typed one jamo after another. |
| `redist/layout-ko-3bul-2011.jmt` | Sebeolsik 3-2011 | Designed by Pat (pat.im), who states that he claims no rights over the arrangement. |
| `redist/layout-ko-3bul-2012.jmt` | Sebeolsik 3-2012 | Same designer, who states the arrangement may be quoted, adapted and redistributed freely. |

Each arrangement was read from its **published layout table** and typed into this format by hand;
values were never converted out of another input method's configuration file. Two independent
readings had to agree before a layout was added, and the method was calibrated on Sebeolsik Final,
where the reading matched the built-in table on all 58 keys.

## The format

These files are written in the **layout language v4** (RFC-0018): `rem` comments, a closing dot,
word operators, and guards. Jamotong reads them directly - `jamotong --check <file>` validates one
and `--build` compiles it, exactly as for the older format.

A layout says what each key carries. Where the key goes - initial, vowel or final - is the
automaton's job for two-set layouts (`map jamo`), and the file's job for three-set layouts
(`map cho` / `map mid` / `map jong`).

## What is not here

Arrangements whose designers have not said they are free to use are left out until that is clear.
