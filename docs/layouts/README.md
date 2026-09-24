# Layout sources

Jamotong's own layout files, written by hand from **published arrangements** - never converted from
another input method's configuration files.

| File | Layout | Provenance |
|---|---|---|
| `ko-2bul.low.jmt` | Dubeolsik standard | The national standard arrangement (KS X 5002). An arrangement is a fact and a method; the wording of this file is Jamotong's own. |
| `ko-3bul-final.low.jmt` | Sebeolsik Final (3-91) | The arrangement Gong Byung-woo and the Hangul Culture Institute published for free use. A test checks this file against Jamotong's built-in table on all 58 keys. |
| `ko-3bul-sunarae.low.jmt` | Sebeolsik Final, shift-free | Derived by Jamotong from Sebeolsik Final with one stated rule: each shifted final consonant moves to the final-consonant slot of the same unshifted key. |

## Can I use them today?

- Dubeolsik standard and Sebeolsik Final are **built in** already (`ko_2bul`, `ko_3bul`). To start from
  one, export it: `jamotong --export @ko_3bul -o my.jmt`.
- These files are written in the **layout language v4** (a new grammar: `rem` comments, a closing
  dot, word operators, guards). The release that reads v4 will also ship them in the installable
  package. Until then they are the reference for the format.
- The shift-free layout needs v4 by nature: one key gives a final consonant or a vowel depending on
  what the syllable already holds, which the older format cannot say.

## What is not here

Some layouts by other designers are free to use - 3-2011 and 3-2012, for instance, whose author
states that he claims no rights. Those will be written afresh from their published arrangement
tables when they are added; values pulled out of someone else's configuration file do not belong here.
