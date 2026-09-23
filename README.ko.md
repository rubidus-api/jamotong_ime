# Jamotong (자모통)

**한국어** | [English](README.md)

**Windows용 순수 C23 + WinAPI 한글 IME** — 프레임워크·외부 라이브러리 없이
TSF(Text Services Framework) 텍스트 서비스로 구현한 한글 입력기.

## 다운로드

| | 최신 릴리스 (클릭 = 바로 다운로드) |
|---|---|
| **자모통 설치판** | **[jamotong-0.34.0.zip](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.34.0/jamotong-0.34.0.zip)** — 아무 곳에 풀고 `install.bat` 을 관리자 권한으로 실행 |
| 입력기 목록 복구 도구 | [jamotong-ime-list-repair-0.18.0.zip](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.18.0/jamotong-ime-list-repair-0.18.0.zip) — Win+Space 에 설치 안 한 IME 가 잔뜩 보일 때 (README 동봉) |

전체 버전 목록·릴리스 노트: [Releases](https://github.com/rubidus-api/jamotong_ime/releases)

### 설치 위치

`install.bat` 은 프로그램을 **`C:\Program Files\Jamotong`** 에 넣습니다 — 모든 앱(Store/UWP 앱 포함)이
읽을 수 있는 고정된 기계 전체 폴더입니다. 설정은 사용자별로 `%APPDATA%\Jamotong` 에 남습니다. 압축을 푼
폴더는 원본일 뿐이니 설치 뒤 지워도 됩니다.

- **업그레이드**: 새 zip 을 풀고 그 `install.bat` 을 다시 관리자 권한으로 실행합니다. 중간에 실패하면(쓰는 중인
  파일, 등록 오류) 이전 판으로 되돌립니다.
- **예전 방식의 설치**(압축 폴더 자체를 등록한 경우, 또는 `%LocalAppData%\Programs\Jamotong` 의 사용자별 사본)는
  자동으로 옮겨집니다: 등록이 `Program Files` 로 바뀌고 옛 사용자별 사본은 지워집니다. 옮긴 직후에는 트레이
  아이콘이 "한글" 글자로 보일 수 있습니다 — 끝의 탐색기 재시작 질문에 **Y** 를 누르거나 다시 로그인하세요.
- 릴리스 바이너리는 **아직 코드 서명되지 않아** 처음 실행할 때 SmartScreen 경고가 뜰 수 있습니다.

## 장점

- 의존성이 없다. 프레임워크·런타임·외부 라이브러리 없이 C와 Win32 API만 사용하며,
  정적 링크된 DLL 두 개(64/32비트)와 데이터 파일이 전부다. Windows 10 이상에서 동작한다.
- 호스트별 입력 경로를 실행 시점에 스스로 고른다. 표준 TSF 문서에서는 문서 안 밑줄 조합,
  레거시(CUAS) 문서에서는 커밋 전용 + 플로팅 미리보기 — 판정 기준은 앱 이름이 아니라
  호스트가 선언하는 문서 상태 플래그다. 메모장·AkelPad·PuTTY·카카오톡 등 실기로 검증했다.
- 무간섭(직접 입력) 모드가 있다. 켜면 키를 일절 가로채지 않아, 원격 데스크톱 너머의
  원격 PC IME로 한글을 입력할 수 있다. 트레이 메뉴 또는 단축키로 토글한다.
- 자판이 데이터다. 두벌식·세벌식 내장 외에 평문 `.jmt` 파일로 정적 리맵·한글 조합
  자판·코드(조합) 자판을 정의할 수 있고, 설정 Export 한 파일로 다른 PC에 그대로 옮겨진다.
- 한자 변환 데이터가 실용 규모다. 고유 한자 9,525자(인명용 표준 100% 커버)·복합어 약
  2,200개·훈음 6,832자를 오프라인 파일로 내장한다.
- 설치·제거·업그레이드에 로그아웃이 필요 없다.
- MIT 라이선스이고, 구현 과정 전체가 공개 매뉴얼(`winapi-c-ime-manual.ko.md`)로 남아 있다.

## 특징

- **커밋 전용 입력 엔진**: 확정된 음절만 문서에 삽입. CUAS(레거시 IMM32 브리지) 앱을 포함한
  **모든 classic 앱에서 동일하게 동작** (감지·앱별 우회 없음). 근거: `winapi-c-ime-manual.md` §8.
- **조합 미리보기 오버레이**: 조합 중 음절을 캐럿 위치의 플로팅 칩으로 표시 (RFC-0002).
  글꼴·크기 사용자 설정 가능.
- **자판**: 2벌식·세벌식(최종) 내장, `.jmt` 파일로 사용자 자판 추가 — 정적 리맵/한글 조합
  자판/코드(조합) 자판(레이어·탭-홀드·마우스 동작 지원, [.jmt 문법](#사용자-자판-jmt) 참조).
- **한자 변환**: 조합 중 음절 + 한자키 → 후보창(훈음 표시; 훈음이 없는 한자는 음만 표시,
  `U+XXXX`로 떨어지지 않음) — 또는 **텍스트를 블록 선택하고 한자키**(레거시 앱에서도 동작).
  독음 데이터 = 고유 한자 약 9,525자(Unicode Unihan + 대법원 인명용 한자, 인명용 표준 한자
  100% 커버, 교육용 기초한자 우선 정렬) + 단어 약 2,200개 + 훈음 1,784자.
- **특수문자**: 자음 + 한자키 (ㅁ=기호, ㅅ=그리스, ㅈ=로마숫자 등 관례).
- **유니코드 직접 입력**: `Ctrl+Alt+U` → 16진 코드포인트 입력(실시간 미리보기 + 문자명 —
  한자면 훈음/음, 그 외는 유니코드 블록명) → Enter.
- **단축키 전면 사용자화**: 모든 트리거(자판 전환·한자·유니코드 입력·설정 열기·무간섭 모드)에
  **복수 단축키**(기능당 최대 8개) 지정 가능.
- **무간섭(직접 입력) 모드**: 원격 데스크톱 등에서 자모통이 키를 일절 가로채지 않게 하는
  토글(트레이 아이콘 우클릭 메뉴). 켜져 있는 동안 아이콘이 `--`로 바뀌고, 자판 전환키까지
  원격으로 그대로 전달되어 원격 PC의 IME로 한글을 입력할 수 있다.
- **관리 앱**(`jamotong.exe`): 트레이 상주가 아니라 작업 표시줄·작업 관리자에 나오는 일반 앱.
  `.jmt` 자판 파일 열기/편집/검증, TSF 없이 입력 테스트, 설정 창 열기를 한곳에서.
- 32/64비트 앱 모두 지원(각각의 DLL), Win11 입력 표시기 브랜딩 아이콘.

## 설치

1. [Releases](https://github.com/rubidus-api/jamotong_ime/releases)에서 최신 zip을 내려받아 푼다
   (소스 빌드는 `make stage` → 설치 가능한 `dist/` 폴더 생성).
2. `install.bat`를 우클릭 → **"관리자 권한으로 실행"**. 프로그램을 `C:\Program Files\Jamotong` 에 복사하고
   64비트·32비트 입력기를 등록한 뒤, 둘 다 그 폴더를 가리키는지 확인한다.
3. `Win+Space` → **"Jamotong IME"** 선택. 이미 떠 있던 앱은 재시작해야 IME를 받으며,
   목록에 안 보일 때만 로그아웃 후 재로그인한다.

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
| 무간섭(직접 입력) 모드 토글 | (기본 없음 — 트레이 아이콘 우클릭 메뉴로 토글, 원하면 지정) |

## 기본 사용법

- **한글 입력**: 두벌식 자판으로 전환 후 입력 — 조합 중 음절이 캐럿 옆 플로팅 칩에
  보이고, 완성되면 문서에 삽입된다. 조합 중 백스페이스는 자소 단위 삭제(옵션).
- **한자**: 음절 조합 중 한자키 → 후보창에서 선택(`↑`/`↓` 이동, `←`/`→`/`PgUp`/`PgDn`/
  `Space` 페이지, 숫자·`Enter` 선택, `Esc` 취소, 마우스 클릭 가능). 이미 입력된
  **음절/단어를 블록 선택하고 한자키**를 눌러도 된다.
- **특수문자**: 자음 하나(ㅁ, ㅅ, ㅈ 등)를 입력하고 한자키.
- **유니코드 입력**: `Ctrl+Alt+U` → 16진 2~6자리 입력(글리프 미리보기 + 문자명) → `Enter`.
- **팝업**(후보창·유니코드 입력·조합 칩)은 화면 작업영역 안에 뜹니다 — 아래나 오른쪽 끝에서는 왼쪽으로
  밀리거나 줄 위로 올라갑니다. 표시 배율을 처리하는 앱에서는 모니터 배율을 따르고(설정의 크기는 100%
  기준), **고대비** 테마에서는 테마 색을 씁니다.

> **UWP 앱(작업표시줄 검색, 설정 앱, Store 앱)에서는 조금 다르게 동작합니다.**
> Windows 가 그런 앱 안에서는 입력기가 자기 창을 띄우지 못하게 막습니다(후보창·팝업이
> 화면에 나타날 수 없습니다). 그래서 그 앱에서는:
> - **한자**: 자모통이 함께 띄우는 **UI 도우미**가 후보창을 대신 그려 줍니다 — 평소처럼 숫자키·↑↓·
>   Enter 로 고르면 됩니다. 도우미는 데스크톱 앱에서 자모통을 쓸 때 자동으로 뜹니다.
>   도우미를 끄면(`UseUiHelper=0`) 후보창 대신 **한자키를 거듭 눌러** 후보를 차례로 바꿉니다
>   (그 방식도 끄려면 `UwpHanjaCycle=0`).
> - **유니코드 입력**: 도우미가 있으면 평소와 같습니다 — `Ctrl+Alt+U` → 16진수 → `Enter`.
>   도우미를 껐다면 **16진수를 먼저 치고 `Ctrl+Alt+U`** 를 누르면 그 문자로 바뀝니다
>   (예: `AC00` 입력 → `Ctrl+Alt+U` → `가`).
> - 조합 중인 글자는 문서에 밑줄로 직접 보이므로 미리보기 칩은 뜨지 않습니다.
>
> 일반(데스크톱) 앱에서는 모두 종전대로 동작합니다.
- **설정 창**: `Ctrl+Alt+K`, 또는 `jamotong.exe` 실행(Layout ▸ Settings).
  탭: *Layouts*(자판 켜기/끄기·순서·`.jmt` 추가), *Shortcuts*(기능 선택 후 단축키
  추가/편집/삭제), *IME Options*(전각·자소삭제·미리보기 글꼴/크기), *General*(DPI·
  Import/Export·초기화).

### 원격 데스크톱 — 두 PC 모두에 자모통이 있을 때

조합은 **한쪽에서만** 하세요. 두 입력기가 같은 키 입력을 동시에 조합하는 것은 지원하지 않습니다
(시험을 안 해 본 것이 아니라 동작이 정의되지 않습니다). 둘이 협력할 방법(프로토콜)도 없습니다.

| 한글 조합을 맡길 쪽 | 로컬 PC | 원격 PC |
|---|---|---|
| **원격** PC 의 IME (원격 작업의 보통 경우) | 자모통을 **무간섭 모드**로(아이콘 `--`) — 자판 전환키를 포함한 모든 키가 그대로 넘어감 | 자모통(또는 다른 IME)을 한글 모드로 |
| **로컬** 자모통 | 자모통을 한글 모드로 | 원격 입력기를 **영어**로 — 받은 글자를 그대로 넣게 |

**양쪽이 함께 조합하고 있다는 신호:** 자모가 풀려 나옴(`한` 대신 `ㅎㅏㄴ`), 한 음절이 두 번 들어감,
자판 전환키가 두 PC 를 한꺼번에 바꿈, 후보창이 양쪽 화면에 뜸. 로컬 PC 에서 무간섭 모드를 켜거나
(트레이 아이콘 우클릭) 원격 PC 를 영어로 바꾸세요.

로컬 IME 가 키를 받기라도 하는지는 원격 데스크톱 클라이언트와 그 키보드 설정(전체 화면, "Windows 키
조합 적용")에 따라 다르며, 모든 클라이언트에서 확인하지는 않았습니다. 어느 경우든 "한쪽에서만" 규칙은
같습니다.

## 설정 파일 위치

모든 설정은 평문 INI 파일 하나에 저장된다:

```
%APPDATA%\Jamotong\config.ini
```

(보통 `C:\Users\<사용자>\AppData\Roaming\Jamotong\config.ini`.) 설정 창에서
**Apply & Save**를 누를 때 저장되고, 어떤 앱에서든 IME가 시작될 때 로드된다.
*General → Export/Import*로 다른 PC에 설정을 옮길 수 있는데, **Export는 사용자 `.jmt`
자판 본문도 함께 동봉**하므로 내보낸 `.ini` 하나로 설정과 사용자 자판이 같이 이동하고,
Import가 다른 PC에서 자판을 복원한다. 파일을 지우면 공장 기본값으로 돌아간다. 언인스톨해도
이 파일은 남는다.

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

선택 메타데이터(형식 2판 — 전부 선택이고, 이것들이 없는 1판 파일은 예전 그대로 로드된다):

```ini
FormatVersion    = 2                 # 생략 = 1
Id               = kim.sebeol391     # 자판 식별자 (역-도메인 꼴 권장)
Version          = 1.2.0             # 자판 자신의 판
Author           = 이름 <mail@example.com>
License          = CC0-1.0           # 공유하는 자판이면 SPDX 식별자 권장
Homepage         = https://example.com/my-layout
Description      = 세벌식 391 에서 숫자열만 바꿈
Locale           = ko-KR
RequiresJamotong = 0.24.0            # 이보다 낮은 자모통에서는 알기 쉬운 메시지와 함께 로드 거부
```

**진단.** 오류가 있는 자판은 절대 반쯤 로드되지 않는다. 문제는 모두(최대 16개)
`파일:줄:열: error|warning: 메시지 [코드]` 와 `help:` 도움말로 보고된다. 예:
`my.jmt:2:9: error: Key: jamo index out of range (C 0..18 / M 0..20 / T 1..27) [E-JMT-RANGE]`.
경고는 로드를 막지 않는다: 모르는 머리부 키(`Athor = …` → *did you mean 'Author'?*), 4글자를 넘는
`Abbrev`. 이 자모통이 읽는 것보다 **더 새로운 `FormatVersion`** 은 오류다(`E-JMT-FORMAT-NEWER`) — 옛 판이 아는
부분만 읽으면 조용히 다른 자판이 되기 때문이다. 알아보지 못한 줄은 1판 파일에서는 경고, **`FormatVersion = 2`
부터는 오류**다 — 새 파일에서 오타로 글쇠가 조용히 빠지는 일이 없게.

키는 항상 **US QWERTY 기준으로 그 물리 키가 내는 문자**로 지정한다. Shift 포함:
`k`=K키, `K`=Shift+K, `;` `!` 같은 기호도 그대로 쓴다.

### 자판 파생 — `Extends` / `Include`

다른 자판에서 출발해 바뀌는 것만 적는다:

```ini
FormatVersion = 2
Type    = hangul
Extends = @ko_3bul          # 내장 자판(@ko_2bul, @ko_3bul, @en_dvorak, @en_qwerty) 또는 ./base.jmt
Name    = 내 세벌식
Key 1   = M13               # 글쇠 다시 정하기 (뒤의 줄이 이긴다)
Key 2   = -                 # '-' = 기반에서 물려받은 글쇠 지우기
Combine M 8 0 = -           # '-' = 물려받은 결합 규칙 지우기
Include = common-rules.jmt  # 다른 파일의 줄을 이 자리에 붙인다 (여러 줄 가능)
```

`Extends` 는 파일마다 한 줄, 깊이는 4 단계까지다. 파일끼리 서로를 부르거나 `Type` 이 기반과 다르면
오류다. `Name`·`Abbrev` 는 따로 적지 않으면 물려받지만 기반의 `Id`·`Version`·`Author` 등은 물려받지
않는다. 조합(chord) 자판에서 기반이 이미 정의한 `Chord`/`Hold`(같은 층, 순서와 무관한 같은 키, 같은 tap/hold)를
다시 쓰면 물려받은 것을 **대체**한다. *한 파일 안에서* 같은 조합을 두 번 쓰면 첫 줄을 쓰고 `W-JMT-DUP-CHORD` 로 경고한다.
두벌식(`@ko_2bul`)도 기반이 된다: `Extends = @ko_2bul` 은 두벌식 동작(아래 `Composition = dubeol`)을
그대로 두고 적은 키만 바꾼다.

### Shift 면·물리 글쇠·블록 (형식 2판)

```ini
Key q shift = C1         # q 의 Shift 면(= 'Key Q'). 기호도: 'Key 1 shift' 는 '!'
Key q base  = C0         # Shift 없는 면 (생략 시 이것)
Key @Q      = C0         # US QWERTY 자리 이름으로 글쇠 지정
Key @SC10   = C0         # ... 또는 스캔코드(16진)
Key @VK_OEM_1 = T4       # ... 또는 가상키 이름
Map @SC11 shift = W      # static 자판에서도 된다

Begin Combine C          # 블록: 안쪽 줄마다 'Combine C' 를 붙인다
  0 0 = 1
  3 3 = 4
End
```

물리 글쇠는 한 줄에 하나이고, 그 글쇠가 US QWERTY 에서 내는 문자로 풀린다. IME 는 여전히 그 대응으로 글쇠를
읽으므로 US 가 아닌 Windows 자판 배열에서는 Windows 가 주는 키 코드를 따른다(그런 배열에서는 아직 실측하지
않았다). `altgr` 는 오류다 — IME 가 AltGr 면을 읽지 않는다. 블록은 중첩되지 않고, `End` 가 빠지면 오류다.

### 관리자 앱(`jamotong.exe`)에서 만들기

- **File ▸ New copy of a built-in layout** — 세벌식 최종·드보락·QWERTY 의 완전한 사본을 편집기로.
- **File ▸ New derived layout** — `Extends = @…` 틀: 바뀌는 것만 적는다.
- **Tools ▸ Validate layout** — 전체 진단(오류·경고 모두, 줄:열과 도움말). 상대 `Extends`/`Include`
  경로는 그 파일의 폴더 기준으로 푼다.
- **Tools ▸ Try this file** — 편집 중인 파일을 읽어 아래 시험칸에서 쳐 본다(설치 전, static·hangul 자판).
- **File ▸ Export expanded** — `--expand` 와 같다. **File ▸ Install to my layouts** — 검증 후
  `%APPDATA%\Jamotong\layouts` 에 복사(바꾸기 전에 묻는다).

### 명령줄 도구

```text
jamotong.exe --check  my.jmt [--json]          # 검사만. 종료 코드 0 = 로드됨, 1 = 오류
jamotong.exe --export @ko_3bul -o ko_3bul.jmt  # 내장 자판을 완전한 .jmt 로 저장
jamotong.exe --expand my.jmt -o flat.jmt       # Extends/Include 를 풀어 파일 하나로
```

`--expand` 는 정규형으로 쓴다(주석·줄 순서는 남지 않고, 다시 펼쳐도 같은 파일). `--json` 은 편집기가
읽을 수 있는 진단을 낸다.

### Type = static (1:1 리맵)

지시문 하나 — 단건과 **배열** 두 형태:

```ini
Map <키> = <출력>          # 그 키가 <출력> 문자를 내게 된다
Map <키…> = <출력…>        # 배열 지정: 좌우 같은 길이, 위치 대응
```

지정하지 않은 키는 원래 문자를 유지한다. `Identity = passthrough`(`Map` 줄 없이)는 모든 키를 손대지 않고 그대로
통과시키는 자판이다 — 내장 QWERTY 와 똑같고, `--export @en_qwerty` 가 이렇게 쓴다. `Extends = @en_qwerty` 에 `Map` 줄을
더하면 보통의 리맵 자판이 된다. 대문자/기호 자리는 각각 따로 지정한다.
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
Composition = sebeol|dubeol         # 받침을 치는 방식 (생략 = sebeol), 아래 설명
```

**`Composition`** — `sebeol`(기본값): 받침(종성) 키가 따로 있다(`T` 스펙). `dubeol`: 내장 두벌식처럼 자음 키(`C`)가
받침이 될 수 있으면 받침이 되고, 뒤에 모음이 오면 다음 음절로 넘어간다(`r k s k` → 가나). 두벌식 파일에는 `T` 키가
없고, 겹받침은 `Combine T <받침> <자음을 받침으로 바꾼 번호> = <결과>` 로 적는다(`Combine T 1 19 = 3`: ㄱ 다음 ㅅ → ㄳ).
어느 자음이 어느 받침이 되는지와 겹받침이 어떻게 나뉘는지는 표준 현대 한글 규칙을 따른다. `dubeol` 은
`Moachigi = 1` 과 함께 쓸 수 없다. `jamotong --export @ko_2bul` 은 내장 두벌식 전체를 파일로 쓴다.

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
| *일반 텍스트* | 텍스트 입력 (**최대 23자** — 넘으면 자르지 않고 오류). 이스케이프: `\n`=Enter, `\t`=Tab, `\s`=스페이스, `\\`=역슬래시, `\#`=`#` 글자. 단독 `\b`=백스페이스. 공백 뒤의 `#` 부터는 주석이므로, 공백 뒤에 `#` 글자가 필요하면 `\#` 로 쓴다(`C#` 이나 `#` 로 시작하는 텍스트는 그대로 된다). |
| `key <이름>` | 특수키 하나 입력. 아래 키 이름 목록 참조. |
| `mod <이름>` | **원샷 모디파이어**: *다음* 조합/문자에 이 모디파이어가 붙는다. 이름: `shift ctrl alt gui`(왼쪽), `rshift rctrl ralt rgui`. |
| `layer <이름>` | **원샷 레이어**: 다음 조합 한 번만 그 레이어에서 찾는다. |
| `tlayer <이름>` | **레이어 토글**: 그 레이어로 전환, 다시 실행하면 `base`로 복귀. |
| `slayer <이름>` | **레이어 전환**: 그 레이어로 가서 유지. |
| `mouse move <dx> <dy>` | 포인터를 (dx, dy)픽셀 상대 이동. |
| `mouse click\|down\|up <left\|right\|middle>` | 마우스 버튼: click=누름+뗌, down/up=드래그용 반쪽. |
| `mouse wheel <up\|down\|N>` | 휠 스크롤 (N=원시 델타, 음수=아래). |

동작에 필요한 낱말 뒤에는 `# 주석`만 올 수 있다 — `key a junk`, `mouse move 5 5x`, `key f1junk` 는 오류다(예전엔
나머지를 무시하고 받아들였다). `mouse move` 는 -10000…10000.

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

#### 형식 3판 (조합 자판)

`FormatVersion = 3` 은 조합 자판에 헷갈리지 않는 동작 문법을 주고, 겹치는 탭/홀드 조합을 늘 같은 규칙으로 판정한다.
1·2판 파일은 예전과 똑같이 동작한다. 3판 파일은 필요한 자모통 판을 적어야 하고(`RequiresJamotong = 0.33.0`), 옛 판은 이
파일을 거부한다.

```ini
FormatVersion    = 3
Type             = chord
RequiresJamotong = 0.33.0
Key jkl; = 0
ComboTermMs = 50            # 첫 키 뒤 이 시간(ms) 안이면 더 큰 조합을 기다린다 (1-1000)
HoldTermMs  = 200           # 탭/홀드 판정 시간 (1-5000)
HoldPolicy  = interrupt     # interrupt: 다른 키가 홀드를 확정 / timeout: HoldTermMs 가 지나야

Chord jkl = text "the"            # 정확한 문자열: "..." 안에서 \" \\ \n \t \u{1F600}; 따옴표 안의 '#' 은 글자
Chord l   = key B mods(ctrl,shift) # 수정키를 곁들인 키
Chord ;   = oneshot mod(shift)    # 다음 키에만 (이어지는 text 에는 씌우지 않는다)
Chord jl  = oneshot layer(num)
Hold  jk  = momentary layer(num)  # 누르고 있는 동안 (Hold 전용)
Hold  kl  = momentary mod(lctrl)
Chord kl  = toggle layer(num)
Chord j;  = switch layer(base)
Layer num                         # num 층의 조합
Chord j   = text "1"
```

- **더 큰 조합이 이긴다**: `Hold jk` 와 `Chord jkl` 이 함께 있을 때 j k l 을 `ComboTermMs` 안에 누르면 `the` 가 입력된다.
  `jk` 홀드는 더 큰 조합이 더는 만들어질 수 없을 때에야 시작한다.
- **가만히 있어도 켜진다**: `Hold` 조합의 키를 누른 채 두면 다른 키를 누르지 않아도 `HoldTermMs` 뒤에 레이어/수정키가 켜진다.
- **굴려 치기(rolling)**: 첫 키를 떼면 조합이 닫힌다. 나머지 키를 떼기 전에 새 키를 누르면 닫힌 조합이 먼저 나가고, 새
  키는 다음 조합을 시작한다.
- 3판에서는 따옴표 없는 문자열, 동작 뒤의 여분 낱말, 한 파일 안의 같은 조합 두 번이 오류다. 마우스 동작은 2판과 같은
  낱말을 쓴다.

## 삭제 (언인스톨)

1. `uninstall.bat` 을 관리자 권한으로 실행한다 — zip 안의 것이든 `C:\Program Files\Jamotong` 안의 사본이든.
   두 입력기 등록을 해제하고(어느 폴더를 가리키든 — 예전 방식 설치도 포함), jamotong.exe 를 끝내고 파일을 지운다.
2. 실행 중인 앱이 물고 있는 DLL 은 지울 수 없어 옆으로 옮겨 두고, 다음 로그인 때 지운다. 재부팅은 필요 없다.
3. 설정은 `%APPDATA%\Jamotong` 에 남는다. 다시 설치하지 않을 거라면 그 폴더도 지운다.

## 빌드

MinGW-w64 크로스 컴파일 (Linux에서):

```sh
make            # dist/jamotong.dll (x64)
make win32      # dist/jamotong32.dll (x86)
make configapp  # dist/jamotong.exe (관리 앱: .jmt 편집/설정/입력 테스트)
make stage      # 위 전부 빌드 + redist/(한자 데이터·설치 스크립트) 를 dist/에 복사
                #  → dist/ 가 곧 설치 폴더 (install.bat 를 관리자 권한으로 실행)
```

`redist/`에는 실행에 필요한 재배포 데이터가 있다: 한자 독음 테이블(`hanja.txt`),
훈음 표(`hanja_hunum.txt`), Unicode License 사본, 설치/삭제 스크립트, 예제 자판(`.jmt`).

## 문서

- **`examples/minimal-tip/`** — 동작하는 최소 TSF IME 예제(약 200줄). 처음 IME를
  만드는 사람이 "COM 서버 → 등록 → 키 싱크 → 삽입"을 먼저 통과해 보라고 만든 것. 매뉴얼 §0.5가 안내한다.
- **`winapi-c-ime-manual.ko.md`** — WinAPI+C만으로 IME를 만드는 종합 매뉴얼 ([English](winapi-c-ime-manual.md))
  (COM 기초부터, 시행착오와 결론 포함. 다른 IME를 만들려면 여기부터)
- `CHANGELOG.md` — 버전별 변경 이력
- 상세 설계 노트·RFC·데이터 출처 명세는 내부 저장소에서 관리 (배포 zip에는
  Unicode License 사본 등 필요한 고지가 동봉됨)

**확장 정책**: 사용자 자판은 코드 없는 `.jmt` 데이터 형식(static/hangul/chord)으로 정의된다 —
자판이 네이티브 코드를 실행하지 않는다. `src/jamotong_plugin.h`의 DLL 플러그인 인터페이스는
**실험적·비활성**(자동 로드 안 함): TIP는 모든 호스트 프로세스에 로드되므로 거기서 서드파티
코드를 실행하는 것은 안전하지 않다. 부활한다면 명시 설치·서명 검증·아웃오브프로세스 모델이 전제.

## 라이선스

코드: [MIT License](LICENSE). 한자 독음 데이터는 Unicode Unihan DB(Unicode License v3, 배포 zip에
고지 동봉), 대법원 인명용 한자 공공데이터(rutopio/Korean-Name-Hanja-Charset, MIT), 단어 매핑
(jemdiggity/hanja-wordlist, MIT; 한글↔한자 매핑만, 뜻풀이 제외)에서 파생. 아이콘 글자는
비트맵 글꼴 [Spleen 5x8](https://github.com/fcambus/spleen)
(Copyright (c) 2018-2026, Frederic Cambus; **BSD 2-Clause License**)에서 파생한 글리프를
사용하며, 고지는 `src/icon_font.h`와 [COPYRIGHT.md](COPYRIGHT.md)(배포 zip에도 동봉)에 있음. GPL/LGPL/CC BY-SA
자료는 사용하지 않음.
