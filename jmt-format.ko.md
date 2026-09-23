# 자모통 파일 형식 — 참조

자모통이 읽는 것은 전부 사람이 읽고 고칠 수 있는 파일이다. 이 문서는 그 전부를 적은 참조다 —
원본 형식, 지시문의 뜻, 컴파일 단계, 도구가 낼 수 있는 모든 메시지. 친절한 소개와 예제는
README 에 있다.

## 네 가지 파일

| 파일 | 무엇인가 | 누가 쓰나 | 누가 읽나 |
|---|---|---|---|
| `*.jmt` | **자판 원본** — 사람이 고치는 UTF-8 텍스트 | 사람 | 컴파일러 (`jamotong --build`) |
| `*.jdt` | **사전 원본** — UTF-8 텍스트, 한 줄에 한 항목 | 사람 | 컴파일러 (`jamotong --build-dict`) |
| `*.jmb` | **구운 자판** — 입력기가 읽는 이진 | 컴파일러 | 입력기 |
| `*.jdb` | **구운 사전** — 입력기가 매핑하는 이진 | 컴파일러 | 입력기 |

입력기는 구운 파일만 읽는다. 굽는 동안 원본 전체를 검사하므로, 목록에 뜨는 자판은 사전까지
깨끗하게 읽힌 자판이다. 굽는 일은 대신 해 준다: `install.bat` 이 배포 자판과 자판 폴더에 이미 있는
것을 굽고, 관리 앱(`jamotong.exe`)이 뜰 때와 설정 → Apply 뒤에 새로 넣었거나 고친 것을 굽는다.

```sh
jamotong --check      자판.jmt          # 원본을 검사하고, 구운 것이 최신인지 알려 준다
jamotong --build      자판.jmt          # 옆에 자판.jmb 를 만든다
jamotong --build-dir  <폴더>            # 없거나 원본보다 낡은 것을 굽는다
jamotong --build-dict 낱말.jdt -o 낱말.jdb
jamotong --import-dict 남의자료.txt -o 낱말.jdt [--limit N] [--name ..] [--license ..]
jamotong --expand     자판.jmt -o 하나.jmt      # Extends·Include 를 펴서 한 파일로
jamotong --export     @ko_3bul -o ko.jmt        # 내장 자판을 원본 파일로
```

찾는 차례: 자판 파일 옆 → `%APPDATA%\Jamotong\layouts`(사전은 `...\dicts`) →
`%PROGRAMDATA%\Jamotong\...` → 설치 폴더. 사전은 폴더 없는 `.jdb` 파일 이름으로만 가리킨다.

## 자판 원본 (`.jmt`)

```ini
# 주석; 빈 줄은 무시
FormatVersion    = 3          # 1·2·3 (없으면 1)
Type             = input      # static | hangul | chord | input   (없으면 hangul)
Engine           = sequence   # Type = input 에서만
RequiresJamotong = 0.40.0     # 3판에서는 필수
Name             = 내 자판    # 자판 목록에 보이는 이름
Abbrev           = MINE       # 트레이 아이콘에 그릴 1~4글자
```

머리부 키: `FormatVersion`·`Type`·`Engine`·`Id`·`Name`·`Abbrev`·`Version`·`Author`·`License`·
`Homepage`·`Description`·`Locale`·`RequiresJamotong`. 모르는 머리부 키는 경고, 모르는 **지시문**은
2판부터 오류다.

`Extends = 다른.jmt` 는 같은 종류의 자판을 기반으로 삼고(한 줄, 최대 4단계, 순환 금지),
`Include = 조각.jmt` 는 조각을 그 자리에 편다. 나중 줄이 이긴다. `@ko_3bul`·`@en_dvorak`·
`@en_qwerty` 는 내장 자판이다.

### `Type = static` — 글쇠 하나에 글자 하나

```ini
Map qwer = asdf          # 왼쪽 글쇠가 같은 자리의 오른쪽 글자를 낸다
Map @SC10 shift = W      # 물리 글쇠: US 자리(@Q)·스캔코드(@SC10)·가상키 이름
Identity = passthrough   # 자판이 글쇠가 내는 그대로 친다
```

### `Type = hangul` — 한글 오토마타

```ini
Composition = sebeol     # sebeol(기본) 또는 dubeol
Moachigi    = 1          # 선택: 한 음절의 자모를 함께 누른다
Key k = C1               # C=초성·M=중성·T=종성, 숫자는 자모 번호
Combine C 1 2 = 3        # 두 자모가 하나로 (종류별)
```

`Composition = dubeol` 은 두벌식처럼 자음을 다음 음절로 넘긴다. 직접 종성 글쇠와 모아치기는 쓸 수
없다.

### `Type = chord` — 조합이 동작을 낸다

```ini
Key jkl; = 0             # 조합 글쇠 선언: j=비트 0, k=1, …
ComboTermMs = 50         # 3판: 더 큰 조합을 기다리는 시간 (1~1000)
HoldTermMs  = 200        # 3판: 탭/홀드 판정 (1~5000)
HoldPolicy  = interrupt  # interrupt(다른 키가 홀드를 확정) 또는 timeout
Layer num                # 이 줄 뒤의 조합은 num 레이어
Chord jk = text "the"    # 조합 (글쇠를 모두 뗄 때)
Hold  jk = momentary layer(num)   # 누르고 있는 동안
Macro 이름 ... EndMacro  # 유한한 동작열: text·key·pointer·wait·with mods(...)/endwith
```

### `Type = input` — 엔진, 원하면 앞에 조합

```ini
Engine      = sequence            # 지금은 이 엔진 하나
Dictionary  = romaji-kana.jdb     # 이 자판이 쓰는 구운 사전
OnUnmatched = flush               # flush(기본)=보류한 글자를 친다, cancel=버린다
Candidates  = kana-words.jdb      # 선택: 읽기 → 후보 여럿
ConvertKey  = space               # Candidates 와 함께 필수: space | tab | hanja | convert | f9
Key jkl; = 0                      # 선택: 앞단 조합
Chord jk = symbol "k"             # 그 결과는 응용이 아니라 엔진으로 간다
```

`Candidates` 가 있으면 글자가 문서로 바로 가지 않고 입력기가 가진 **읽기**에 쌓이며, 변환 글쇠가 그
읽기의 후보를 내놓는다. 고르면 읽기를 후보로 바꾸고, 취소하면 읽기가 남는다. 백스페이스는 읽기 한
글자, Esc 는 읽기 전체, 경계에서는 읽은 그대로 확정한다. 읽기가 바뀐 뒤 늦게 온 선택은 버린다.

엔진은 최장 일치를 기다린다 — 사전에 `n`·`na`·`ni` 가 있으면 `n` 하나로는 확정하지 않는다. 보류한
글자는 캐럿 옆에 보이고 문서에는 넣지 않는다. 백스페이스는 보류 한 글자를 되돌리고, Esc 는
보류를 버리며, 사이띄개·엔터·탭·화살표는 응용의 것이다(보류는 먼저 확정한다). 앞단 조합이 있으면
조합이 먼저 결정하므로 한 글쇠를 둘이 겹쳐 먹지 않고, `symbol` 은 조합 인식기로 되돌아가지 않는다.

### 동작 (3판)

| 동작 | 뜻 |
|---|---|
| `text "..."` | 정확한 문자열을 문서로 |
| `symbol "..."` | 엔진으로 보내는 논리 입력 (입력 자판에서만) |
| `key 이름 [mods(ctrl,shift,alt,gui,lctrl,…)]` | 실제 키 이벤트 |
| `oneshot mod(x)` / `oneshot layer(x)` | 다음 조합에만 |
| `momentary mod(x)` / `momentary layer(x)` | 누르고 있는 동안 (`Hold` 전용) |
| `toggle layer(x)` / `switch layer(x)` | 레이어 바꾸기 |
| `pointer move(dx,dy) [profile(slow\|normal\|fast)]` | 마우스 이동 (`Hold` 면 연속) |
| `pointer wheel(dx,dy) [profile(scroll)]` | 스크롤 |
| `pointer click\|down\|up\|drag-toggle(left\|right\|middle)` | 마우스 버튼 |
| `cancel actions` | 연속 동작 정지·드래그 해제·매크로/원샷 취소 |
| `macro <이름>` | 매크로 실행 |

문자열은 `"..."` 안에 `\"`·`\\`·`\n`·`\t`·`\u{16진}` 이스케이프를 쓴다. 따옴표 밖의 `#` 부터는 주석이다.

## 사전 원본 (`.jdt`)

```text
JamotongData 1
Type = sequence
Name = romaji kana
Version = 1.0.0
License = CC0-1.0
Source = 이 자료가 어디서 왔는지
# 주석
ka	か
kya	きゃ
```

`Type` 은 `sequence`(글쇠열 → 글자) 또는 `candidates`(읽기 → 후보 여럿 — 같은 읽기를 후보 수만큼
적고, 적은 차례가 보여 줄 차례다)다. 첫 줄은 고정이다. 자료 줄은 키, **탭**, 낼 글자이고,
`\u{16진}` 은 **양쪽 모두** 쓸 수 있다. 후보 사전의 읽기는 어느 문자든 된다. 구우면 항목을
정렬하고 중복을 거르며, 입력기가 읽기 전용으로 매핑하는 이진을 만든다. 열 때 머리부와 색인을
보고, 그 사전을 쓰는 자판을 읽을 때 파일 전체(검사합·차례·키 글자)를 본다.

## 한도

| 무엇 | 한도 |
|---|---|
| `.jmt` 한 줄 | 255자 |
| `.jmt` 줄 수 (Extends·Include 편 뒤) | 2만 줄, 파일 12개까지 |
| `Extends` 깊이 | 4 |
| 자판당 조합 / 레이어 / 매크로 / 매크로 단계 | 2048 / 16 / 8 / 128 |
| 조합 문자열 (2판) | 23자 |
| 사전: 친 쪽 / 낼 글자 / 항목 | 32자 / 64자 / 50만 |
| 목록의 자판 | 8개 |

## 메시지

모든 진단은 `파일:줄:칸: 수준: 메시지 [코드]` 꼴이고, 알려 줄 것이 있으면 `help:` 줄이 붙는다.
경고는 로드를 막지 않고, 오류는 막는다.

| 코드 | 뜻 | 할 일 |
|---|---|---|
| `E-DICT-DUP` | 사전에 같은 키가 두 번 있다 | keep one of the two rows |
| `E-DICT-EMPTY` | 사전에 항목이 하나도 없다 | write one 'keys TAB output' line per entry |
| `E-DICT-KEY` | 친 쪽은 공백 없는 ASCII 여야 한다 | this is what is pressed on the keyboard |
| `E-DICT-HEAD` | Name/License 가 사전이 실을 수 있는 길이(63글자)를 넘는다 | 라이선스 이름만 적고 전문은 파일로 가리킨다 |
| `E-DICT-KIND` | 모르는 사전 Type | Type = sequence |
| `E-DICT-LIMIT` | 항목이 한도(50만)를 넘는다 | - |
| `E-DICT-LINE` | 한 줄이 1023바이트를 넘는다 | one entry per line |
| `E-DICT-MAGIC` | 사전 원본이 아니다 (첫 줄이 JamotongData 1) | the first line must be 'JamotongData 1' |
| `E-DICT-MEMORY` | 메모리가 모자란다 | - |
| `E-DICT-OPEN` | 사전 원본을 열 수 없다 | - |
| `E-DICT-ROW` | 자료 줄은 키<탭>값 이어야 한다 | e.g. 'ka' then a tab then the letters it types |
| `E-DICT-VALUE` | 낼 글자를 쓸 수 없다 (이스케이프·서로게이트·길이) | write the letters directly or as \u{hex} |
| `E-DICT-VERSION` | 더 새 사전 원본 판 | update Jamotong to build it |
| `E-DICT-WRITE` | 사전 파일을 쓸 수 없다 (폴더 확인) | check that the folder exists and is writable |
| `E-JMB-KIND` | 구울 수 없는 자판 종류 | - |
| `E-JMB-MEMORY` | 메모리가 모자란다 | - |
| `E-JMB-WRITE` | 구운 자판을 쓸 수 없다 | check that the folder exists and is writable |
| `E-JMT-ACTION` | 모르는 동작이거나 뒤에 군더더기가 있다 | steps: text "...", key NAME [mods(...)], pointer ..., wait <ms>, with mods(...) / endwith |
| `E-JMT-ASCII` | ASCII 밖의 글쇠 | - |
| `E-JMT-BLOCK` | 블록(Begin/End) 짝이 맞지 않는다 | close the first block with End |
| `E-JMT-BUILTIN` | 이 내장 자판은 물려받을 수 없다 | ko_2bul keeps its rules in the engine, so it cannot be written as a file |
| `E-JMT-COUNT` | 개수가 맞지 않는다 | one spec per key, e.g. 'Key khj = C0 C2 C11' |
| `E-JMT-CYCLE` | Extends 가 돌고 있다 | remove the loop between these files |
| `E-JMT-DEPTH` | Extends 가 4단계보다 깊다 | flatten a level with 'jamotong.exe --expand' |
| `E-JMT-DICT` | 사전 이름이 없거나 규칙에 맞지 않는다 | no folders in the name - the file lives beside the layout or in the dictionary folder |
| `E-JMT-DICT-BAD` | 사전을 쓸 수 없다 (깨짐·검사합) | - |
| `E-JMT-DICT-KIND` | 순차 사전이 아니다 | build it with 'Type = sequence' |
| `E-JMT-DICT-MISSING` | 적어 둔 사전을 찾지 못했다 | put it beside the layout or in the dictionary folder |
| `E-JMT-DUBEOL` | 두벌식에서는 쓸 수 없는 선언 | use C keys - dubeol turns them into finals from context |
| `E-JMT-DUP-CHORD` | 한 파일 안에 같은 조합이 두 번 (3판) | remove one of the two lines (in format version 3 this is an error) |
| `E-JMT-ENCODING` | UTF-8 이 아니다 | save the file as UTF-8 |
| `E-JMT-ENGINE` | Engine 이 없거나 모르는 엔진 | add 'Engine = sequence' |
| `E-JMT-EXTENDS-INCLUDE` | 조각 파일은 Extends 를 쓸 수 없다 | put Extends in the main layout file |
| `E-JMT-EXTENDS-TWICE` | Extends 는 한 줄만 | use Include for extra fragments |
| `E-JMT-EXTENDS-TYPE` | 기반 자판의 종류가 다르다 | extend a layout of the same kind |
| `E-JMT-FORMAT-NEWER` | 이 자모통이 읽지 못하는 더 새 형식 판 | update Jamotong |
| `E-JMT-IDENTITY` | Identity 값이 잘못됐다 | remove 'Identity = passthrough' to remap keys |
| `E-JMT-INCLUDE` | Include 를 풀 수 없다 | - |
| `E-JMT-KEY-SYNTAX` | Key/Chord/Hold 줄의 문법 | e.g. 'Key j = 0' |
| `E-JMT-LEVEL` | 값이 허용 단계를 벗어난다 | see the limits table |
| `E-JMT-LIMIT` | 한도를 넘는다 (매크로·단계·문자열) | - |
| `E-JMT-LINE-LONG` | 한 줄이 255자를 넘는다 | split it into several lines (a key list can be written over several Key lines) |
| `E-JMT-MACRO` | 매크로 이름이나 블록이 잘못됐다 | define it first with 'Macro <name> ... EndMacro' |
| `E-JMT-MAP-LEN` | Map 의 좌우 길이가 다르다 | write one output character for each key, e.g. 'Map qwe = abc' |
| `E-JMT-OPEN` | 파일을 열 수 없다 | - |
| `E-JMT-PHYSKEY` | 물리 글쇠 이름이 잘못됐다 | use a US position, a scan code or a virtual-key name |
| `E-JMT-RANGE` | 값이 범위를 벗어난다 | bits 0..31; a key list takes consecutive bits from the start bit |
| `E-JMT-READ` | 파일을 읽다 실패했다 | save the file as UTF-8; the lines after this point were not read |
| `E-JMT-REQUIRES` | 이 자판은 더 새 자모통을 요구한다 | update Jamotong |
| `E-JMT-REQUIRES-MISSING` | 3판 파일인데 RequiresJamotong 이 없다 | add 'RequiresJamotong = 0.33.0' |
| `E-JMT-SEQ-INLINE` | 변환표는 자판 파일 안에 둘 수 없다 | put the entries in a dictionary source (.jdt), build it with 'jamotong --build-dict' and write 'Dictionary = name.jdb' |
| `E-JMT-STRING` | 문자열 문법이 잘못됐다 | write text "..." with escapes \\" \\\\ \\n \\t \\u{hex} |
| `E-JMT-SYMBOL` | symbol 은 엔진이 있는 자판에서만 | use it in a layout with 'Type = input' and an 'Engine =' line; a chord layout has no engine |
| `E-JMT-TEXT-LONG` | 조합 문자열이 23자를 넘는다 (2판) | shorten the text (at most 23 characters after \\n, \\t and \\s) |
| `E-JMT-TOO-LONG` | 파일이 2만 줄을 넘는다 | - |
| `E-JMT-TYPE` | Type 값이 잘못됐다 | C = choseong, M = jungseong, T = jongseong |
| `E-JMT-TYPE-UNKNOWN` | 모르는 Type | Type must be static, hangul, chord or (format 3) input |
| `E-JMT-UNDECLARED` | Key 로 선언하지 않은 글쇠를 조합에 썼다 | declare every chord key first, e.g. 'Key j = 0' |
| `E-JMT-UNKNOWN-DIRECTIVE` | 이 자판 종류가 모르는 지시문 | check the spelling against the directive list |
| `E-JMT-VALUE` | 값이 잘못됐다 | passthrough = keys go through unchanged (like the built-in QWERTY) |
| `W-JMT-ABBREV-LONG` | Abbrev 가 4자보다 길다 | use 1 to 4 characters |
| `W-JMT-DUP-CHORD` | 같은 조합이 두 번 — 첫 줄이 쓰인다 (1·2판) | remove one of the two lines |
| `W-JMT-EMPTY-LAYER` | 레이어에 조합이 없다 | remove it or give it chords |
| `W-JMT-IGNORED-LINE` | 모르는 줄을 무시했다 (1판에서만) | in format 2 and later this is an error |
| `W-JMT-UNKNOWN-KEY` | 모르는 머리부 키 — 무시했다 | check the spelling; the message suggests the closest key |

## 함께 보기

- README(한국어) — "사용자 자판 (.jmt)" 절의 소개와 예제.
- 배포판의 `example.jmt`·`example-dvorak.jmt`·`example-artsey.jmt` 는 주석 달린 예제다.
