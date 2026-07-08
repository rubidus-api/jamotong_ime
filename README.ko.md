# Jamotong (자모통)

**한국어** | [English](README.md)

**Windows용 순수 C23 + WinAPI 한글 IME** — 프레임워크·외부 라이브러리 없이
TSF(Text Services Framework) 텍스트 서비스로 구현한 한글 입력기.

## 특징

- **커밋 전용 입력 엔진**: 확정된 음절만 문서에 삽입. CUAS(레거시 IMM32 브리지) 앱을 포함한
  **모든 classic 앱에서 동일하게 동작** (감지·앱별 우회 없음). 근거: `winapi-c-ime-manual.md` §8.
- **조합 미리보기 오버레이**: 조합 중 음절을 캐럿 위치의 플로팅 칩으로 표시 (RFC-0002).
  글꼴·크기 사용자 설정 가능.
- **자판**: 2벌식·세벌식(최종) 내장, `.jmt` 파일로 사용자 자판 추가 — 정적 리맵/한글 조합
  자판/코드(조합) 자판(레이어·탭-홀드·마우스 동작 지원, [.jmt 문법](#사용자-자판-jmt) 참조).
- **한자 변환**: 조합 중 음절 + 한자키 → 후보창(훈음 표시) — 또는 **텍스트를 블록 선택하고
  한자키**(레거시 앱에서도 동작). 독음 데이터 = Unicode Unihan(8,900+자, 교육용 기초한자
  우선 정렬) + 단어 331개 + 훈음 1,784자.
- **특수문자**: 자음 + 한자키 (ㅁ=기호, ㅅ=그리스, ㅈ=로마숫자 등 관례).
- **유니코드 직접 입력**: `Ctrl+Alt+U` → 16진 코드포인트 입력(실시간 미리보기) → Enter.
- **단축키 전면 사용자화**: 모든 트리거(자판 전환·한자·유니코드 입력·설정 열기)에
  **복수 단축키**(기능당 최대 8개) 지정 가능.
- **트레이 모니터**(`jamotong.exe`): 현재 자판을 트레이에 표시(파랑=자모통 활성,
  회색=비활성); 좌클릭=설정, 우클릭=메뉴.
- 32/64비트 앱 모두 지원(각각의 DLL), Win11 입력 표시기 브랜딩 아이콘.

## 설치

1. [Releases](https://github.com/rubidus-api/jamotong_ime/releases)에서 최신 zip을
   내려받는다(소스 빌드는 `make stage` → 설치 가능한 `dist/` 폴더 생성).
2. 압축을 풀고 **폴더째 원하는 영구 위치로 복사**한다 — IME가 이 폴더에서 직접 실행되므로
   설치 후 지우면 안 된다. 정식 프로그램 위치의 예:

   ```
   C:\Program Files\Jamotong
   ```

3. `install.bat`를 우클릭 → **"관리자 권한으로 실행"**.
4. **로그아웃 후 다시 로그인**(재부팅까지는 필요 없음) → `Win+Space` →
   **"Jamotong IME"** 선택.

### 설치 직후 기본값

기본 자판(자판 전환 키로 순환):

| 자판 | 기본 상태 |
|---|---|
| 영문 QWERTY | ✔ 켜짐 |
| 한글 두벌식 | ✔ 켜짐 |
| 영문 드보락 | ✖ (설정 → Layouts에서 켜기) |
| 한글 세벌식 최종 | ✖ (설정 → Layouts에서 켜기) |

기본 키 — 모든 기능은 변경 가능하고 복수 지정(기능당 최대 8개)이 된다
(설정 → Shortcuts):

| 기능 | 기본 키 |
|---|---|
| 자판 전환 (한/영) | 한/영 키, 오른쪽 Alt, Shift+Space |
| 한자/특수문자 변환 | 한자 키 |
| 유니코드 코드 입력 | Ctrl+Alt+U |
| 설정 창 열기 | Ctrl+Alt+K |

## 기본 사용법

- **한글 입력**: 두벌식 자판으로 전환 후 입력 — 조합 중 음절이 캐럿 옆 플로팅 칩에
  보이고, 완성되면 문서에 삽입된다. 조합 중 백스페이스는 자소 단위 삭제(옵션).
- **한자**: 음절 조합 중 한자키 → 후보창에서 선택(`↑`/`↓` 이동, `←`/`→`/`PgUp`/`PgDn`/
  `Space` 페이지, 숫자·`Enter` 선택, `Esc` 취소, 마우스 클릭 가능). 이미 입력된
  **음절/단어를 블록 선택하고 한자키**를 눌러도 된다.
- **특수문자**: 자음 하나(ㅁ, ㅅ, ㅈ 등)를 입력하고 한자키.
- **유니코드 입력**: `Ctrl+Alt+U` → 16진 2~6자리 입력(글리프 미리보기) → `Enter`.
- **설정 창**: `Ctrl+Alt+K`, 트레이 아이콘, 또는 `jamotong.exe` 실행.
  탭: *Layouts*(자판 켜기/끄기·순서·`.jmt` 추가), *Shortcuts*(기능 선택 후 단축키
  추가/편집/삭제), *IME Options*(전각·자소삭제·미리보기 글꼴/크기), *General*(DPI·
  Import/Export·초기화).

## 설정 파일 위치

모든 설정은 평문 INI 파일 하나에 저장된다:

```
%APPDATA%\Jamotong\config.ini
```

(보통 `C:\Users\<사용자>\AppData\Roaming\Jamotong\config.ini`.) 설정 창에서
**Apply & Save**를 누를 때 저장되고, 어떤 앱에서든 IME가 시작될 때 로드된다.
*General → Export/Import*로 다른 PC에 설정을 옮길 수 있고, 파일을 지우면 공장
기본값으로 돌아간다. 언인스톨해도 이 파일은 남는다.

## 사용자 자판 (.jmt)

`.jmt`는 자판을 기술하는 평문 UTF-8 텍스트 파일이다. `Type =` 줄로 세 종류를 고른다:

| `Type` | 용도 |
|---|---|
| `static` | 1:1 문자 리맵 (드보락, 콜맥 등) |
| `hangul` | 한글 조합 자판 (세벌식 계열, 결합 규칙 자유) |
| `chord`  | 코드(조합) 자판 (ARTSEY류): 글쇠 조합 → 텍스트/특수키/마우스, 레이어·탭-홀드 |

**자판 로드 방법** — 둘 중 하나:

- `.jmt` 파일을 `jamotong.dll` 옆에 복사 (IME 시작 시 자동 감지, 목록에 **꺼진 상태로**
  추가됨 → 설정 → Layouts에서 켜기), 또는
- 설정 → Layouts → **Add**로 파일 선택 (켜진 상태로 추가).

자판 목록은 최대 8개. 배포판의 `example.jmt`·`example-dvorak.jmt`·`example-artsey.jmt`가
각 종류의 주석 달린 문법 예제다.

### 공통 머리부

```ini
# 줄 첫머리 '#' = 주석, 빈 줄 무시
Type   = hangul        # static | hangul | chord  (생략 시 hangul)
Name   = my_layout     # 자판 목록/언어바에 표시되는 이름 (최대 63자)
Abbrev = 마            # 트레이 2x2 아이콘에 그릴 1~4글자 (선택)
```

키는 항상 **US QWERTY 기준으로 그 물리 키가 내는 문자**로 지정한다. Shift 포함:
`k`=K키, `K`=Shift+K, `;` `!` 같은 기호도 그대로 쓴다.

### Type = static (1:1 리맵)

지시문 하나 — 단건과 **배열** 두 형태:

```ini
Map <키> = <출력>          # 그 키가 <출력> 문자를 내게 된다
Map <키…> = <출력…>        # 배열 지정: 좌우 같은 길이, 위치 대응
```

지정하지 않은 키는 원래 문자를 유지한다. 대문자/기호 자리는 각각 따로 지정한다.
키 나열엔 공백을 쓸 수 없으므로 스페이스 키는 단건으로 지정한다.
예 (드보락 윗줄을 한 줄로):

```ini
Type = static
Name = my_dvorak
Abbrev = Dv

Map qwertyuiop = ',.pyfgcrl   # 배열 지정: q→' w→, e→. ...
Map [ = /                     # 단건 지정도 그대로 가능
```

### Type = hangul (조합 자판)

키에 자모를 배정하고 결합 규칙을 선언하면 나머지(조합 미리보기·확정·자소 단위
백스페이스·한자 변환)는 IME 오토마타가 처리한다.

```ini
Key <키> = <C|M|T><인덱스>          # 키 하나에 자모 하나: C=초성 M=중성 T=종성
Key <키…> = <스펙> <스펙> …         # 배열 지정: 키 수만큼 스펙 나열, 위치 대응
Combine <C|M|T> <a> <b> = <결과>    # 자모 a 다음 b가 오면 <결과>로 결합
Moachigi = 0|1                      # 1 = 모아치기(순서 무관 결합), 아래 설명
```

인덱스 표 (C/M/T 뒤의 숫자):

```
초성 C: 0ㄱ 1ㄲ 2ㄴ 3ㄷ 4ㄸ 5ㄹ 6ㅁ 7ㅂ 8ㅃ 9ㅅ 10ㅆ 11ㅇ 12ㅈ 13ㅉ 14ㅊ 15ㅋ 16ㅌ 17ㅍ 18ㅎ
중성 M: 0ㅏ 1ㅐ 2ㅑ 3ㅒ 4ㅓ 5ㅔ 6ㅕ 7ㅖ 8ㅗ 9ㅘ 10ㅙ 11ㅚ 12ㅛ 13ㅜ 14ㅝ 15ㅞ 16ㅟ 17ㅠ 18ㅡ 19ㅢ 20ㅣ
종성 T: 1ㄱ 2ㄲ 3ㄳ 4ㄴ 5ㄵ 6ㄶ 7ㄷ 8ㄹ 9ㄺ 10ㄻ 11ㄼ 12ㄽ 13ㄾ 14ㄿ 15ㅀ 16ㅁ 17ㅂ 18ㅄ 19ㅅ 20ㅆ 21ㅇ 22ㅈ 23ㅊ 24ㅋ 25ㅌ 26ㅍ 27ㅎ
```

예 (발췌 — 초/중/종성이 서로 다른 키에 놓이는 세벌식형):

```ini
Type = hangul
Name = ex_hangul
Abbrev = 예벌
Moachigi = 1

Key khj = C0 C2 C11   # 배열 지정: k=ㄱ h=ㄴ j=ㅇ (초성)
Key fd = M0 M20       # f=ㅏ d=ㅣ
Key s = T4            # ㄴ 종성 (단건 지정)
Key x = T1            # ㄱ 종성

Combine C 11 0 = 1     # 초성 ㅇ+ㄱ → ㄲ (된소리)
Combine C 18 12 = 14   # 초성 ㅎ+ㅈ → ㅊ (거센소리)
Combine M 8 0 = 9      # ㅗ+ㅏ → ㅘ
Combine T 1 19 = 3     # 종성 ㄱ+ㅅ → ㄳ
```

- **`Moachigi = 0`** (이어치기): 자모를 한 타씩 순서대로 입력하는 일반 방식.
- **`Moachigi = 1`** (모아치기/동시치기): 여러 키를 함께 눌러 한 음절을 만들고,
  `Combine` 규칙이 **순서와 무관하게** 매칭된다(`a b`가 `b a`에도 적용).
  동시타건 세벌식 변형에 쓴다.

`Combine` 규칙은 자판당 최대 256개.

### Type = chord (코드 자판)

몇 개의 "코드 글쇠"를 **함께 눌렀다 모두 떼면** 그 조합의 동작이 실행된다 —
ARTSEY 같은 한 손 자판의 방식. 동작은 실제 키/마우스 이벤트로 전달되므로
어떤 앱에서든, 한글 모드 밖에서도 동작한다.

```ini
Type = chord
Name = ex_chord
Abbrev = ART

# 1) 코드 글쇠 선언이 먼저: 각 글쇠에 비트 번호 0~31을 배정.
#    배열 지정은 시작 비트부터 연속 배정:
Key jkl; = 0    # j=0 k=1 l=2 ;=3
Key f = 4       # 단건 지정도 그대로 가능

# 2) 조합: 나열한 글쇠를 함께 눌렀다 떼면 동작 실행.
Chord j   = a          # 한 글쇠 = 문자 a
Chord jk  = e          # j+k 동시 = e
Chord jkl = the        # 세 글쇠 = 단어 "the" 통째로
```

#### 동작(우변) 종류

| 문법 | 의미 |
|---|---|
| *일반 텍스트* | 텍스트 입력 (최대 ~23자). 이스케이프: `\n`=Enter, `\t`=Tab, `\s`=스페이스, `\\`=역슬래시. 단독 `\b`=백스페이스. |
| `key <이름>` | 특수키 하나 입력. 아래 키 이름 목록 참조. |
| `mod <이름>` | **원샷 모디파이어**: *다음* 조합/문자에 이 모디파이어가 붙는다. 이름: `shift ctrl alt gui`(왼쪽), `rshift rctrl ralt rgui`. |
| `layer <이름>` | **원샷 레이어**: 다음 조합 한 번만 그 레이어에서 찾는다. |
| `tlayer <이름>` | **레이어 토글**: 그 레이어로 전환, 다시 실행하면 `base`로 복귀. |
| `slayer <이름>` | **레이어 전환**: 그 레이어로 가서 유지. |
| `mouse move <dx> <dy>` | 포인터를 (dx, dy)픽셀 상대 이동. |
| `mouse click\|down\|up <left\|right\|middle>` | 마우스 버튼: click=누름+뗌, down/up=드래그용 반쪽. |
| `mouse wheel <up\|down\|N>` | 휠 스크롤 (N=원시 델타, 음수=아래). |

`key <이름>`이 받는 키 이름:

```
이동    : left right up down home end pgup pgdn ins del
편집    : back enter tab space esc
키패드  : kp0~kp9 kpadd kpsub kpmul kpdiv kpdot kpenter
모디파이어(일반 키로): lshift rshift lctrl rctrl lalt ralt lwin rwin
락/기타 : caps numlock scroll pause apps prtsc sleep
미디어  : volup voldown mute mplay mnext mprev mstop
브라우저: browserback browserfwd browserrefresh browserhome mail calc mediasel
기능키  : f1 … f24 (보이지 않는 f13~f24 포함)
단일 문자: 아무 글자/숫자, 예  key x
```

#### 레이어

```ini
Layer base       # 암묵 기본 레이어; 'Layer' 뒤의 Chord들은 그 레이어 소속
Chord a = layer num      # 원샷: 다음 조합 한 번만 num 레이어
Chord fa = tlayer mouse  # 마우스 레이어 켜기/끄기

Layer num
Chord j = 1
Chord k = 2

Layer mouse
Chord j  = mouse move -20 0
Chord jk = mouse click left
Chord fa = tlayer mouse   # 다시 누르면 base로
```

레이어 최대 16개, 조합 최대 2048개. `Chord` 줄은 가장 최근 `Layer` 지시문(없으면
`base`) 소속이다. 아직 정의되지 않은 레이어를 미리 참조해도 된다(전방 참조 허용).

#### 탭 vs 홀드 (딜레이드 입력)

같은 조합에 두 동작을 줄 수 있다 — 짧게 누르는 **탭**(`Chord`)과 길게 누르는
**홀드**(`Hold`):

```ini
Chord jkl = the        # 탭  (짧게 눌렀다 뗌)
Hold  jkl = key enter  # 홀드 (0.2초 이상 누르고 있다가 뗌) → Enter
```

`Hold`는 동작 종류에 따라 두 방식으로 동작한다:

- **지속형 홀드** — 동작이 `layer <이름>` 또는 `mod <이름>`일 때:
  *방해(interrupt) 기반*으로 발동한다. 홀드 조합을 누른 채 다른 키를 누르면
  그 순간 발동하고, **누르고 있는 동안** 임시 레이어/모디파이어가 유지되다가
  떼면 원래대로 돌아온다. 모멘터리 레이어/홀드 모디파이어 관용구:

  ```ini
  Hold jk = layer num    # j+k를 누른 채 다른 키 → num 레이어에서 입력
  Hold kl = mod lctrl    # k+l을 누른 채 다른 키 → Ctrl+<키>로 전달
  ```

- **단발형(딜레이드) 홀드** — 그 외 동작(`텍스트`/`key`/`mouse`)일 때:
  조합을 누르고 다른 키 없이 **0.2초 이상** 유지한 뒤 떼면 탭(`Chord`) 대신
  `Hold` 동작이 실행된다.

조합에 `Hold`만 있고 탭이 없으면 시간과 무관하게 뗄 때 홀드 동작이 실행된다.

#### 전체 예제

배포판의 `example-artsey.jmt` 한 파일에 위 내용이 모두 동작하는 형태로 들어 있다:
문자·단어 조합, 백스페이스/스페이스/엔터, 원샷 Shift, 원샷·모멘터리 숫자 레이어,
토글 마우스 레이어(이동·클릭·휠), 탭/홀드 쌍, 미디어 키. 이 파일을 복사해 `Key`
비트 선언은 두고 조합표만 원하는 자판(예: 공개된 ARTSEY 표)으로 채우면 된다.

## 삭제 (언인스톨)

1. `uninstall.bat`를 우클릭 → **"관리자 권한으로 실행"**.
   IME 등록을 해제하고, 트레이 앱을 종료한 뒤, 잠겨 있지 않은 바이너리를 즉시
   삭제한다.
2. 일부 파일이 **locked**로 표시되면: 로그아웃 후 다시 로그인해서
   `uninstall.bat`를 한 번 더 실행(또는 폴더째 삭제)하면 끝난다. **재부팅은 필요
   없다** — IME DLL은 텍스트 입력을 쓴 모든 실행 중 앱(탐색기, ctfmon 등)에 매핑돼
   있어서 그 세션 안에서는 지울 수 없고, 로그아웃하면 그 프로세스들이 모두 종료된다.
3. 설정은 `%APPDATA%\Jamotong`에 남는다. 재설치 계획이 없으면 그 폴더도 지운다.

## 빌드

MinGW-w64 크로스 컴파일 (Linux에서):

```sh
make            # dist/jamotong.dll (x64)
make win32      # dist/jamotong32.dll (x86)
make configapp  # dist/jamotong.exe (트레이 모니터링/설정 앱)
make stage      # 위 전부 빌드 + redist/(한자 데이터·설치 스크립트) 를 dist/에 복사
                #  → dist/ 가 곧 설치 폴더 (install.bat 를 관리자 권한으로 실행)
```

`redist/`에는 실행에 필요한 재배포 데이터가 있다: 한자 독음 테이블(`hanja.txt`),
훈음 표(`hanja_hunum.txt`), Unicode License 사본, 설치/삭제 스크립트, 예제 자판(`.jmt`).

## 문서

- **`winapi-c-ime-manual.ko.md`** — WinAPI+C만으로 IME를 만드는 종합 매뉴얼 ([English](winapi-c-ime-manual.md))
  (COM 기초부터, 시행착오와 결론 포함. 다른 IME를 만들려면 여기부터)
- `CHANGELOG.md` — 버전별 변경 이력
- 상세 설계 노트·RFC·데이터 출처 명세는 내부 저장소에서 관리 (배포 zip에는
  Unicode License 사본 등 필요한 고지가 동봉됨)

## 라이선스

코드: [MIT License](LICENSE). 한자 독음 데이터는 Unicode Unihan DB 파생(Unicode License v3,
배포 zip에 고지 동봉). GPL/LGPL/CC BY-SA 자료는 사용하지 않음.
