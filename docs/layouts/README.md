# Layout sources

Jamotong's own layout files, written by hand from **published arrangements** - never converted from
another input method's configuration files.

| File | Layout | Provenance |
|---|---|---|
| `ko-2bul.low.jmt` | Dubeolsik standard | The national standard arrangement (KS X 5002). An arrangement is a fact and a method; the wording of this file is Jamotong's own. |
| `ko-3bul-final.low.jmt` | Sebeolsik Final (3-91) | The arrangement Gong Byung-woo and the Hangul Culture Institute published for free use. A test checks this file against Jamotong's built-in table on all 58 keys. |
| `ko-3bul-sunarae.low.jmt` | Sebeolsik Final, shift-free | Derived by Jamotong from Sebeolsik Final with one stated rule: each shifted final consonant moves to the final-consonant slot of the same unshifted key. |
| `ko-3bul-390.low.jmt` | Sebeolsik 390 (3-90) | Also published for free use by Gong Byung-woo and the Hangul Culture Institute. Unlike Sebeolsik Final it gives no key to six consonant clusters - they are typed as two final consonants in a row, which the `combine jong` table below each layout handles. |
| `ko-3bul-2011.low.jmt` | Sebeolsik 3-2011 | Designed by Pat (pat.im), who states that he claims no rights over the arrangement. |
| `ko-3bul-2012.low.jmt` | Sebeolsik 3-2012 | Same designer, who states the arrangement may be quoted, adapted and redistributed freely. |

## Can I use them today?

- Dubeolsik standard and Sebeolsik Final are **built in** already (`ko_2bul`, `ko_3bul`). To start from
  one, export it: `jamotong --export @ko_3bul -o my.jmt`.
- These files are written in the **layout language v4** (a new grammar: `rem` comments, a closing
  dot, word operators, guards). The release that reads v4 will also ship them in the installable
  package. Until then they are the reference for the format.
- The shift-free layout needs v4 by nature: one key gives a final consonant or a vowel depending on
  what the syllable already holds, which the older format cannot say.

## How these were written

Each arrangement was read from its **published layout table** and typed into this format by hand.
Two independent readings had to agree before a layout was added: the published table, and the values
seen in a widely used configuration for the same layout. The method was calibrated first on
Sebeolsik Final, where the reading matched Jamotong's built-in table on all 58 keys.

## What is not here

Layouts whose designers have not said the arrangement is free to use - several newer three-set and
sin-sebeolsik variants - are left out until that is clear.
