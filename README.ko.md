**한국어** | [English](README.md) — **Jamotong v0.73.1** — [MSI](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.73.1/jamotong-0.73.1.msi) · [ZIP](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.73.1/jamotong-0.73.1.zip) · [Cleanup tool](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.73.1/jamotong-cleanup-0.73.1.exe)

# Jamotong (자모통)

**Windows용 순수 C23 + WinAPI 한글 IME** — 프레임워크·외부 라이브러리 없이
TSF(Text Services Framework) 텍스트 서비스로 구현한 한글 입력기.

## 다운로드

| | 최신 릴리스 (클릭 = 바로 다운로드) |
|---|---|
| **자모통 설치 패키지 (권장)** | **[jamotong-0.73.1.msi](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.73.1/jamotong-0.73.1.msi)** — 두 번 눌러 설치(영어·한국어, 폴더 선택). **파일 하나**: 구성 요소 페이지에서 중국어 간체·번체·일본어를 체크하면 함께 설치됩니다(셋 다 약 21MB). 제거는 "설치된 앱"에서 |
| 자모통 zip (설치기 없음) | [jamotong-0.73.1.zip](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.73.1/jamotong-0.73.1.zip) — 같은 파일만 담았습니다. 손으로 두고 등록합니다([설치](#설치) 참고) |
| 정리 도구 | [jamotong-cleanup-0.73.1.exe](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.73.1/jamotong-cleanup-0.73.1.exe) — 이 PC 에서 자모통의 **모든 판**을 지웁니다: 설치본, 예전 언어 팩, 설치기 없이 한 등록(zip), 옛 IMM32 입력기와 그것들이 남긴 것. 내 설정은 칸을 체크할 때만. 실행 → 관리자 확인 → 모두 지우기; 재부팅하라고 하면 재부팅한 뒤 다시 실행하세요 |
| 입력기 목록 복구 도구 | [jamotong-ime-list-repair-0.18.0.zip](https://github.com/rubidus-api/jamotong_ime/releases/download/v0.18.0/jamotong-ime-list-repair-0.18.0.zip) — Win+Space 에 설치 안 한 IME 가 잔뜩 보일 때 (README 동봉) |

전체 버전 목록·릴리스 노트: [Releases](https://github.com/rubidus-api/jamotong_ime/releases)

### 설치 방법

- **MSI (권장)**: `jamotong-0.73.1.msi` 를 두 번 누르면 설치가 시작됩니다. 첫 화면에서 설치기 언어(영어 또는
  한국어 — 윈도가 한국어로 설정돼 있으면 한국어가 미리 골라져 있습니다)를 고르고, 이어서 라이선스와 설치
  폴더를 보여 줍니다. 관리자 권한을 한 번 묻습니다(입력기 등록은 기계 전체 자리에 써야 합니다 — 윈도가 그렇게
  정해 놓았습니다). 업그레이드는 새 MSI 를 실행하면 되고, 제거는 **설정 ▸ 앱 ▸ 설치된 앱 ▸ Jamotong ▸ 수정 ▸
  제거**입니다(자모통 자신의 한국어 창이 뜨도록 "제거" 단추는 막아 두었습니다). 재부팅은 걸지 않습니다 — 그때 돌고 있던 앱은 이전 사본을 계속 쓰다가 다시 로그인하면 새 판을
  씁니다. 창 없는 설치도 됩니다: `msiexec /i jamotong-0.73.1.msi /qn` (폴더를 정하려면
  `INSTALLDIR="D:\Jamotong\"` 를 덧붙입니다).
- **언어 (0.70.0)**: 중국어 간체·중국어 번체·일본어는 같은 MSI 의 구성 요소이고 기본으로 꺼져 있습니다 — 구성 요소
  페이지에서 체크하거나, 창 없는 설치에 `ADDLOCAL=ZhSimplified,ZhTraditional,Japanese`(모두는 `ADDLOCAL=ALL`)를
  덧붙입니다. 나중에 **설치된 앱 ▸ Jamotong ▸ 수정 ▸ 변경**에서 더하거나 뺍니다. 0.62~0.69 의 따로 된 중국어 팩
  MSI 는 업그레이드가 지우고, 깔려 있던 팩은 같은 구성 요소로 그대로 설치됩니다. 예전 일본어 zip 팩(0.49)을 썼다면
  `%APPDATA%\Jamotong\layouts` 의 `japanese.jmt`(와 `japanese.*.jmb`)를 지우세요 — 이름이 같은 사용자 자판이 설치된 것을
  대신하므로 예전 일본어가 남습니다.
- **zip**: 설치기 없이 같은 파일만 담았습니다. 손으로 두고 등록할 분을 위한 것입니다([설치](#설치) 참고).
  `install.bat`·`uninstall.bat` 은 0.61.0 부터 없습니다.

**0.60.0 에서 올라올 때는 먼저 저장하세요.** 0.60.0 패키지에는 "입력기가 실린 앱을 닫지 말라"는 설정이 빠져
있었고, 윈도는 옛 판을 그 판의 규칙대로 지웁니다. 그래서 이번 업그레이드 한 번은 **자모통을 쓰고 있던 앱들이
닫힙니다**(다시 열리지 않습니다). 닫기 전에 설치기가 그 앱들을 보여 주고 기다립니다: 작업을 저장하고 앱을 닫은 뒤
**다시 시도**를 누르거나, 그대로 계속해서 닫히게 둘 수 있습니다. 창 없는 설치(`/qn`)는 물을 수 없어 바로 닫습니다.
그다음 업그레이드부터는 아무것도 닫지 않습니다. 예전 zip 설치가 남긴 `uninstall.bat` 은
MSI 가 지웁니다.

### 설치 위치

기본은 **`C:\Program Files\Jamotong`** 이고, 설치 폴더 화면에서 다른 폴더를 고를 수 있습니다. 조건은 하나입니다:
**그 폴더와 그 위의 모든 폴더를 관리자만 바꿀 수 있어야 합니다.** 입력기는 관리자 권한으로 도는 앱을 포함한
모든 앱에 실리므로, 일반 사용자가 이름을 바꾸거나 쓸 수 있는 폴더라면 그 사용자가 DLL 을 바꿔치기할 수
있습니다. 입력기가 자기를 등록할 때 폴더와 그 위 폴더를 모두 검사하고, 관리자 아닌 누가 바꿀 수 있는 폴더(사용자
프로필 아래 폴더, 연결 지점, 네트워크·이동식 드라이브)가 있으면 설치를 멈추고 되돌립니다. 어느 폴더가 왜
걸렸는지는 `%ProgramData%\Jamotong\install.log` 에 적힙니다. `D:\Jamotong` 처럼 로컬 드라이브에 새로 만드는
폴더는 괜찮습니다. 검사를 통과하면 설치 폴더를 잠급니다: 관리자는 바꿀 수 있고, 그 밖의 모두(Store/UWP 앱
포함)는 읽기만 합니다. 빈 폴더를 고르세요 — 안에 있던 것도 같이 잠깁니다.

설정은 사용자별로 `%APPDATA%\Jamotong` 에 남습니다. 업그레이드는 전에 고른 폴더를 그대로 씁니다.

- 릴리스 바이너리와 MSI 는 **아직 코드 서명되지 않아** 처음 실행할 때 SmartScreen 경고가 뜰 수 있습니다.

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
  토글(`Ctrl+Alt+P`, 또는 트레이 아이콘 우클릭 메뉴). 켜져 있는 동안 아이콘이 회색 줄표로 바뀌고, 자판 전환키까지
  원격으로 그대로 전달되어 원격 PC의 IME로 한글을 입력할 수 있다.
- **관리 앱**(`jamotong.exe`): 트레이 상주가 아니라 작업 표시줄·작업 관리자에 나오는 일반 앱.
  `.jmt` 자판 파일 열기/편집/검증, TSF 없이 입력 테스트, 설정 창 열기를 한곳에서.
- 32/64비트 앱 모두 지원(각각의 DLL), Win11 입력 표시기 브랜딩 아이콘.

## 설치

1. [Releases](https://github.com/rubidus-api/jamotong_ime/releases)에서 `jamotong-0.73.1.msi` 를 내려받아 두 번
   누른다. 프로그램을 설치하고 64비트·32비트 입력기를 등록한다.

   설치기 없이(zip, 또는 `dist/` 에 파일을 모으는 `make stage`): 파일을 관리자만 바꿀 수 있는 폴더(보통
   `C:\Program Files\Jamotong`)에 복사하고, 그 폴더에서 관리자 명령 프롬프트로 `jamotong.exe --register` 를
   실행한다. 일반 사용자가 바꿀 수 있는 폴더는 거절한다(이유는 `%ProgramData%\Jamotong\install.log`). 제거는
   같은 식으로 `jamotong.exe --unregister` 를 실행한 뒤 폴더를 지운다.
2. `Win+Space` → **"Jamotong IME"** 선택. 이미 떠 있던 앱은 재시작해야 IME를 받으며,
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
| 무간섭(직접 입력) 모드 토글 | Ctrl+Alt+P (트레이 아이콘 우클릭 메뉴로도 토글) |

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

> **UWP 앱(작업표시줄 검색, Store 앱)** 에서도 자모통은 데스크톱 앱처럼 자기 후보창과 유니코드 입력창을
> 띄웁니다(0.61.1 부터; 작업표시줄 검색 상자에서 확인). 어떤 앱에서 그 창이 보이지 않으면 `config.ini` 에
> `UwpOwnWindow=0` 을 적어 예전 방식으로 돌아갈 수 있습니다:
> - **한자**: 자모통이 함께 띄우는 **UI 도우미**가 후보창을 대신 그려 줍니다 — 숫자키·↑↓·Enter 로 고릅니다.
>   도우미는 데스크톱 앱에서 자모통을 쓸 때 자동으로 뜹니다. 도우미까지 끄면(`UseUiHelper=0`)
>   **한자키를 거듭 눌러** 후보를 차례로 바꿉니다(그 방식도 끄려면 `UwpHanjaCycle=0`).
> - **유니코드 입력**: 도우미가 있으면 평소와 같고, 껐다면 **16진수를 먼저 치고 `Ctrl+Alt+U`** 를 누릅니다
>   (예: `AC00` 입력 → `Ctrl+Alt+U` → `가`).
> - 조합 중인 글자는 문서에 밑줄로 직접 보이므로 미리보기 칩은 뜨지 않습니다.
>
> 후보창을 아예 만들지 못하면 같은 물러서기(도우미 → 순환)가 저절로 쓰입니다. 일반(데스크톱) 앱에서는 모두
> 종전대로 동작합니다.
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

`.jmt`는 자판을 기술하는 평문 UTF-8 텍스트 파일이다. 이 절은 소개이고, 전부를 적은 참조는
[`jmt-format.ko.md`](jmt-format.ko.md) 에 있다(모든 지시문·사전 형식·굽는 명령·모든 메시지와 코드).
`Type =` 줄로 네 종류를 고른다:

| `Type` | 용도 |
|---|---|
| `static` | 1:1 문자 리맵 (드보락, 콜맥 등) |
| `hangul` | 한글 조합 자판 (세벌식 계열, 결합 규칙 자유) |
| `chord`  | 코드(조합) 자판 (ARTSEY류): 글쇠 조합 → 텍스트/특수키/마우스, 레이어·탭-홀드 |
| `input`  | v3 공통 표면. `Engine = sequence` 면 친 글자열을 다른 글자로 바꾼다 |

**자판은 쓰기 전에 컴파일한다.** 입력기는 구운 자판(`.jmb`)만 읽고, `.jmt` 는 사람이 고치는
원본이다. 굽는 동안 파일 전체를 검사하고, 순차 자판이면 사전까지 본다 — 그래서 목록에 뜨는
자판은 깨끗하게 읽힌 자판이다.

```sh
jamotong --build 내자판.jmt          # 옆에 내자판.v9.jmb 를 만든다
jamotong --build-dir "%APPDATA%\Jamotong\layouts"
jamotong --check 내자판.jmt          # 원본을 검사하고, 구운 것이 최신인지 알려 준다
```

손으로 칠 일은 거의 없다:

- 설치(MSI 가 부르는 `jamotong.exe --register`)가 배포 자판과, 기계 전체·내 자판 폴더에 이미 있는 자판을 굽는다.
- 관리 앱(`jamotong.exe`, 트레이 아이콘)이 뜰 때 새로 넣었거나 고친 것을 굽고, 설정 →
  Layouts → **Apply** 뒤에도 굽는다.
- 설정 → Layouts → **Add** 는 고른 파일을 구워 보고, 잘못이 있으면 그 자리에서 알려 준다.

**자판 로드 방법** — 둘 중 하나:

- `.jmt` 파일을 `%APPDATA%\Jamotong\layouts` 에 넣고 관리 앱을 한 번 띄워 굽게 한다 → 다음 IME
  시작 때 목록에 **꺼진 상태로** 추가됨 (설정 → Layouts에서 켜기), 또는
- 설정 → Layouts → **Add**로 파일 선택 (켜진 상태로 추가 — 관리 앱이 대신 구워 준다).
  스토어 앱(UWP)의 설정창에서는 관리 앱을 띄울 수 없다. 그럴 땐 `jamotong --build` 로 직접 구워
  자판 폴더에 넣는다.

`jamotong.dll` 옆(설치 폴더, 기본은 `C:\Program Files\Jamotong`)에 둔 `.jmt` 는 관리 앱이 굽지 **않는다** —
그 폴더는 관리자 권한이 필요하다. 관리자 명령 프롬프트에서 `jamotong --build-dir "<설치 폴더>"` 를 돌린다.

자판 목록에는 개수 제한이 없다 — 원하는 만큼 넣고 켜 둔 것끼리 돌려 쓴다. 배포판의 `example.jmt`·`example-dvorak.jmt`·`example-artsey.jmt`가
각 종류의 주석 달린 문법 예제다.

`layout-ko-onehand.jmt` 는 **한 손 한글 자판**이다: 왼손 여덟 글쇠로, 윗줄(`q w e r`)을 함께 누르면 자음, 아랫줄
(`a s d f`)을 함께 누르면 모음이 나오고, 낱자는 두벌식처럼 모인다(받침·겹받침, 모음 둘은 ㅘ 같은 겹모음). 된소리는
윗줄 넷을 함께 눌렀다 뗀 뒤 평음을 친다(`q+w+e+r` 다음 ㄱ = ㄲ). 자음을 두 번 치면 그대로 두 자음이다(`먹고`).
나머지는 엄지 셋으로 간다: `v` 를 누른 채 손가락 하나로 영문·기호·숫자·괄호·이동·펑션·낱자 층으로 바꾸고(`v`+`r` 은
한글로), `b` 를 누른 채 다음 키 하나에 Ctrl/Shift/Alt/Win 을 걸고(`b`+`f` 다음 영문 `c` = Ctrl+C), space 를 누른 채
Backspace·Enter·Delete·Tab·Esc 를 친다. 전체 표는 파일 머리에 있다.
다른 자판처럼 추가해 쓴다. 조합을 적는 법은 [jmt-format.ko.md](jmt-format.ko.md) 에 있다.

### 공통 머리부

```ini
# 줄 첫머리 '#' = 주석, 빈 줄 무시
Type   = hangul        # static | hangul | chord  (생략 시 hangul)
Name   = my_layout     # 자판 목록/언어바에 표시되는 이름 (최대 63자)
Abbrev = 마            # 1~4글자: 앞 두 글자가 트레이 아이콘의 언어(ko, en; ZH → cn, JA → jp), 전체로 표지를 고른다 (선택)
```

선택 메타데이터(v2 — 전부 선택이고, 이것들이 없는 v1 파일은 예전 그대로 로드된다):

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
부분만 읽으면 조용히 다른 자판이 되기 때문이다. 알아보지 못한 줄은 v1 파일에서는 경고, **`FormatVersion = 2`
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

### Shift 면·물리 글쇠·블록 (v2)

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

### 정적 자판 (v4 문법)

글쇠 하나가 글자 하나를 낸다. 오토마타는 돌지 않는다. 자판 전체가 `map char` 블록 하나다 — 왼쪽은
그 글쇠가 US 자판에서 내는 글자, 오른쪽은 이 자판이 대신 낼 글자다. 적지 않은 글쇠는 제 글자를
그대로 내므로, **QWERTY 는 배정이 하나도 없는 자판**이다:

```lowlayout
layout name "qwerty" .
layout format 4 .
engine none .                  rem 오토마타 없음 — 글쇠 하나에 글자 하나

rem `map char` 블록이 없다: 모든 글쇠가 그대로 지나간다.
rem `jamotong --export @en_qwerty` 가 쓰는 것이 정확히 이것이다.
```

글쇠를 제 글자에 배정해도 되지만 뜻이 없으므로, 실제 자판은 **바꾸는 것만** 적는다. 아래는 드보락
**전체**다 — 윗글쇠 자리까지 포함한다(`jamotong --export @en_dvorak` 이 쓰는 파일):

```lowlayout
layout name "dvorak" .
layout format 4 .
engine none .

map char do
  "\"" "_" .  "'" "-" .  "+" "}" .  "," "w" .  "-" "[" .  "." "v" .
  "/" "z" .  ":" "S" .  ";" "s" .  "<" "W" .  "=" "]" .  ">" "V" .
  "?" "Z" .  "B" "X" .  "C" "J" .  "D" "E" .  "E" ">" .  "F" "U" .
  "G" "I" .  "H" "D" .  "I" "C" .  "J" "H" .  "K" "T" .  "L" "N" .
  "N" "B" .  "O" "R" .  "P" "L" .  "Q" "\"" .  "R" "P" .  "S" "O" .
  "T" "Y" .  "U" "G" .  "V" "K" .  "W" "<" .  "X" "Q" .  "Y" "F" .
  "Z" ":" .  "[" "/" .  "]" "=" .  "_" "{" .  "b" "x" .  "c" "j" .
  "d" "e" .  "e" "." .  "f" "u" .  "g" "i" .  "h" "d" .  "i" "c" .
  "j" "h" .  "k" "t" .  "l" "n" .  "n" "b" .  "o" "r" .  "p" "l" .
  "q" "'" .  "r" "p" .  "s" "o" .  "t" "y" .  "u" "g" .  "v" "k" .
  "w" "," .  "x" "q" .  "y" "f" .  "z" ";" .  "{" "?" .  "}" "+" .
end
```

글쇠 문자열은 글쇠 하나를 글로 적은 것이라, 구두점 글쇠도 `\"` 와 `\\` 말고는 이스케이프가 필요 없다.
스페이스 글쇠는 `" "` 로 적는다.

### 한글 자판 (v4 문법)

글쇠에 낱자를 배정하고 결합 규칙을 적으면 나머지(조합 미리보기·확정·자소 단위 백스페이스·한자 변환)는
오토마타가 한다. 자판 파일은 **v4 문법**으로 적는다 — 주석은 `rem`, 폼은 ` .` 로 닫고, 기호 대신 낱말을 쓴다.

**두 벌이냐 세 벌이냐에 따라 적는 것이 다르다.**

```lowlayout
rem 두 벌 — 글쇠는 낱자만 말하고, 자리(초성·종성·겹받침)는 오토마타가 정한다
layout name "내 두벌식" .
layout format 4 .
engine hangul .

map jamo do
  "r" "ㄱ" .  "s" "ㄴ" .  "e" "ㄷ" .  "k" "ㅏ" .  "j" "ㅓ" .
end
map jamo do  "R" "ㄲ" .  "E" "ㄸ" .  end        rem 윗글쇠는 글쇠 문자열이 그대로 말한다

combine jong "ㄱ" "ㅅ" be "ㄳ" .                 rem 겹받침
combine mid  "ㅗ" "ㅏ" be "ㅘ" .                 rem 겹모음
```

```lowlayout
rem 세 벌 — 자리를 못 박는다
map cho  do  "k" "ㄱ" .  "h" "ㄴ" .  "j" "ㅇ" .  end
map mid  do  "f" "ㅏ" .  "d" "ㅣ" .  end
map jong do  "s" "ㄴ" .  "x" "ㄱ" .  end
moachigi .                                      rem 모아치기(순서 무관)를 켠다
```

**조건부 글쇠** — 한 글쇠가 조합 상태에 따라 다른 낱자를 낼 수 있다. 먼저 적은 줄이 이긴다.

```lowlayout
guard jongslot be cho and jung and not jong .
map jong when jongslot do  "f" "ㄻ" .  end      rem 종성 자리면 받침
map mid do                 "f" "ㅏ" .  end      rem 아니면 모음
```

물을 수 있는 것은 `cho` `jung` `jong` `empty` 와 번호 비교(`jong eq 8`)이고, 연산자는 낱말이다
(`not and or eq ne lt le gt ge`). 낱자는 자모 글자로 적지만 번호(`cho 0`)도 받는다 —
초성 0ㄱ…18ㅎ, 중성 0ㅏ…20ㅣ, 종성 1ㄱ…27ㅎ.

`jamotong --export @ko_2bul -o 내두벌식.jmt` 로 내장 자판을 v4 파일로 내보내 고쳐 쓰면 쉽다.
설치 폴더에는 이미 자판 여섯 벌(`layout-*.jmt`)이 들어 있다.

**자판 하나를 통째로.** 아래는 두벌식 표준 **전체**다 — 글쇠와 결합 규칙을 남김없이 적었다.
`jamotong --export @ko_2bul` 이 쓰는 파일이자 설치 폴더의 `layout-ko-2bul.jmt` 다:

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

낱자는 자모 글자 그대로 적는다. 윗글쇠 자리도 그냥 글쇠다 — `"Q"` 는 US 자판에서 `Q` 를 내는
글쇠라는 뜻이므로, "shift" 라고 따로 말할 것이 없다.

- **`moachigi` 없이**(기본, 이어치기): 자모를 한 타씩 순서대로 입력하는 일반 방식.
- **`moachigi .` 를 적으면**(모아치기/동시치기): 여러 글쇠를 함께 눌러 한 음절을 만들고,
  `combine` 규칙이 **차례와 무관하게** 맞는다(`"ㄱ" "ㅅ"` 이 `"ㅅ" "ㄱ"` 에도).
  동시타건 세벌식 변형에 쓴다.

`combine` 규칙은 자판당 최대 256개.

### 조합 자판 (v4 문법)

몇 개의 "조합 글쇠"를 **함께 눌렀다 모두 떼면** 그 조합의 동작이 실행된다 — ARTSEY 같은 한 손
자판의 방식이다. 동작은 실제 키/마우스 이벤트로 나가므로 어떤 앱에서든, 한글 모드 밖에서도
동작한다. 조합 자판도 한글 자판과 같은 **v4 문법**으로 적는다: `rem` 주석, ` .` 로 닫는 폼,
`do … end` 블록, 기호 대신 낱말.

```lowlayout
layout name "ex_chord" .
layout format 4 .
engine none .

keys "arts" "eyio" .        rem 이 자판이 쓰는 글쇠와 비트 차례 (0..31)
chordterm 50 .              rem 더 큰 조합을 기다리는 시간 (ms, 1-1000)
holdterm 200 .              rem 탭/홀드 판정 (ms, 1-5000)
holdpolicy interrupt .      rem interrupt: 다른 글쇠가 홀드를 확정 · timeout: holdterm 뒤에만

chord "a"   be text "a" .           rem 한 글쇠
chord "ar"  be text "b" .           rem a+r 동시
chord "art" be text "the" .         rem 세 글쇠 = 단어 통째로
```

`keys` 는 조합보다 먼저 오고, 적은 차례대로 글쇠마다 비트를 준다. 조합은 그 글쇠들을 이어 적은
문자열이다. 레이어 16개, 조합 2048개까지. 한 파일에서 같은 조합을 두 번 적으면 오류다.

#### 동작 (`be` 뒤)

| 꼴 | 뜻 |
|---|---|
| `text "the"` | 글월 입력 (**23자까지**). 이스케이프는 닫힌 v4 집합: `\n` `\t` `\\` `\"` `\xNN` `\uXXXX` `\UXXXXXXXX` … |
| `key enter` | 특수키 하나. 이름은 아래 목록. |
| `key f4 (mods ctrl alt)` | 수정키를 얹어서: `shift ctrl alt gui` 와 오른쪽 `r…` 이름. |
| `mod shift` · `layer num` | 짧은 꼴 — `chord` 자리면 원샷, `hold` 자리면 누르고 있는 동안. |
| `oneshot (mod shift)` | **원샷 수정키** — 다음 조합 한 번만 (`sticky` 도 같은 뜻). |
| `oneshot (layer num)` | **원샷 레이어** — 다음 조합만 그 레이어에서 찾는다. |
| `momentary (layer num)` | 누르고 있는 **동안만** 그 레이어(또는 `mod`). |
| `toggle (layer mouse)` | 레이어 켜기; 같은 조합을 다시 누르면 `base` 로. |
| `switch (layer base)` | 그 레이어로 가서 머문다. |
| `pointer (move 12 0)` | 포인터를 (dx, dy) 픽셀 이동. `(profile fast)` — `slow` `normal` `fast` `scroll`. |
| `pointer (click left)` | 마우스 단추: `click` / `down` / `up` / `dragtoggle`, `left` `right` `middle`. |
| `pointer (wheel 0 -1)` | 스크롤 (`wheel 1 0` 은 오른쪽). |
| `macro 이름` | `macro 이름 do … end` 로 적은 매크로 실행. |
| `cancel` | 이동을 멈추고, 잡고 있던 드래그를 놓고, 돌던 매크로를 멈춘다. |

숫자에 붙은 빼기표는 값의 일부다(`move -12 0`). `pointer move` 는 -10000…10000. 이 언어에는
낱말 안에 빼기표가 올 수 없어 v3 의 `drag-toggle` 은 `dragtoggle` 로 적는다. `key` 가 받는 이름:

```
이동   : left right up down home end pgup pgdn ins del
편집   : back enter tab space esc
숫자판 : kp0-kp9 kpadd kpsub kpmul kpdiv kpdot kpenter
수정키 : lshift rshift lctrl rctrl lalt ralt lwin rwin   (그냥 키로)
잠금등 : caps numlock scroll pause apps prtsc sleep
미디어 : volup voldown mute mplay mnext mprev mstop
브라우저: browserback browserfwd browserrefresh browserhome mail calc mediasel
기능키 : f1 … f24  (보이지 않는 f13-f24 포함)
한 글자: 아무 글자/숫자, 예)  key x
```

#### 레이어와 매크로

```lowlayout
chord "ay" be oneshot (layer num) .   rem 다음 조합 한 번만
chord "as" be toggle (layer mouse) .  rem 켜기/끄기

layer num do
  chord "a" be text "1" .
  chord "r" be text "2" .
end

layer mouse do
  chord "a"  be pointer (move -20 0) .
  chord "ar" be pointer (click left) .
  chord "as" be toggle (layer mouse) .   rem 다시 누르면 base 로
end

macro label do                 rem 유한하고 멈출 수 있는 동작열 (128단계, 3초)
  text "item: " .
  key left .
  wait 40 .                    rem ms (1-2000); 기다리는 동안 입력기는 멈추지 않는다
  with (mods ctrl shift) .
  key right .
  endwith .
end
chord "ao" be macro label .
```

`layer … do … end` 안의 `chord` 는 그 레이어 소속이고, 블록 밖은 `base` 다. 레이어는 정의보다
먼저 써도 된다. 매크로는 그것을 가리키는 조합보다 먼저 적는다. 매크로는 한 번에 하나만 돌고,
다른 글쇠·`cancel`·초점 바뀜·자판 바뀜이 멈추며 눌러 둔 수정키를 놓는다. 되풀이도 조건도 없고,
클립보드나 파일을 읽는 것도 없다.

#### 탭과 홀드

같은 조합이 두 가지 일을 한다 — 짧게 치는 **탭**(`chord`)과 **홀드**(`hold`):

```lowlayout
chord "e" be text " " .
hold  "e" be momentary (layer num) .   rem 누르고 있는 동안: 숫자 레이어
chord "y" be key back .
hold  "y" be mod lctrl .               rem 누른 채 친 글쇠는 Ctrl+<키> 로
```

- 동작이 `mod`/`layer` 인 `hold` 는 **지속형**이다. 누른 채 다른 글쇠를 치면 켜지고 떼면 돌아온다.
  아무것도 안 치고 계속 누르고 있어도 `holdterm` 뒤에 켜진다.
- 그 밖의 홀드 동작(`text`·`key`·`pointer`)은 `holdterm` 을 넘겨 누르고 있다가 뗄 때 탭 대신
  일어난다. 조합에 `hold` 만 있으면 시간과 상관없이 뗄 때 일어난다.
- **큰 조합이 이긴다**: `hold "ar"` 와 `chord "art"` 가 있을 때 a r t 를 `chordterm` 안에 누르면
  세 글쇠 동작이 나간다. `ar` 홀드는 더 큰 조합이 될 수 없을 때 비로소 시작한다.
- **굴리기**: 처음 떼는 순간 조합이 닫힌다. 나머지를 떼기 전에 새 글쇠를 누르면 닫힌 조합이 먼저
  나가고 새 글쇠가 다음 조합을 연다.
- **포인터**: `hold` 의 `pointer (move …)`·`(wheel …)` 는 누르고 있는 동안 이어지며 프로파일 한계까지
  빨라지고, 반대 방향은 상쇄되고, 대각선은 정규화된다. `chord` 자리면 한 번만 일어난다.
  `dragtoggle` 로 시작한 드래그는 같은 조합이나 `cancel`, 또는 초점·자판이 바뀔 때 풀린다.

#### 전체 예제

배포판의 `example-artsey.jmt` 가 위의 것을 한 파일에 담고 있다: 글자, 단어 조합, Backspace/Space/
Enter, 원샷 Shift, 원샷과 모멘터리 숫자 레이어, 토글 마우스 레이어(이동·클릭·휠), 탭/홀드 짝,
미디어 키. 복사해서 `keys` 선언은 그대로 두고 자기 조합표(예: 공개된 ARTSEY 표)를 채우면 된다.
파일 머리의 `note DOC … DOC` 가 문법을 그 자리에서 요약한다.

#### 순차 입력 (v3)

**순차 변환 자판**은 친 글쇠의 *열*을 다른 글자로 바꾼다 — 로마자를 가나로 바꾸는 식이다. 표는
자판 파일에 적지 않는다. 한 번 **컴파일해 둔 사전**에 있고, 자모통은 컴파일된 파일만 읽는다.
사전이 없거나 깨진 자판은 목록에 아예 뜨지 않는다.

사전 원본 `romaji-kana.jdt` 를 쓴다(UTF-8, 한 줄에 하나, 두 칸 사이는 탭):

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

굽는다(`\u{...}` 대신 글자를 그대로 써도 된다):

```sh
jamotong --build-dict romaji-kana.jdt -o romaji-kana.jdb
```

자판 파일은 어느 사전을 쓰는지만 적는다:

```ini
FormatVersion    = 3
Type             = input
Engine           = sequence
RequiresJamotong = 0.39.1
Name             = romaji kana
Abbrev           = KANA
Dictionary       = romaji-kana.jdb
OnUnmatched      = flush     # flush(기본): 보류한 글자를 그대로 친다. cancel: 버린다
```

- **최장 일치가 이긴다.** 사전에 `n`·`na`·`ni` 가 있으면 `n` 은 기다린다. `ni` 는 に 가 되고, `nk` 는
  ん 을 확정한 뒤 `k` 로 다시 시작한다. `konnichiha` 를 치면 こんにちは 가 된다.
- **보류한 글자는 보여줄 뿐 넣지 않는다.** 캐럿 옆 미리보기 칩에 뜨고(관리 앱 시험칸에서는 선택된
  글자로), 판정이 끝나야 문서로 들어간다.
- **백스페이스**는 보류한 글자 하나를 되돌린다. 이미 문서에 들어간 글자는 건드리지 않는다. **Esc**
  는 보류를 버린다. 자판 전환 글쇠로 자판을 바꾸면 보류한 것을 친 그대로 확정하고, 포커스가 옮겨
  가면(다른 창·언어바) 버린다 — 문서에 들어간 적이 없는 글자다.
- **사이띄개·엔터·탭·화살표는 응용의 것이다.** 보류가 있었으면 먼저 문서에 확정하고 넘긴다.
- 사전은 자판 파일 옆 → `%APPDATA%\Jamotong\dicts` → `%PROGRAMDATA%\Jamotong\dicts` 차례로 찾는다.
  이름은 폴더 없는 파일 이름이고 확장자는 `.jdb` 다.
**읽기와 후보(선택).** 후보 사전이 있으면 엔진이 낸 글자를 바로 확정하지 않는다. 입력기가 가진
**읽기**에 쌓이고(캐럿 옆에 보인다), 변환 글쇠를 누르면 그 읽기의 후보를 고를 수 있다.

```ini
Dictionary  = romaji-kana.jdb     # 글쇠열 → 글자
Candidates  = kana-words.jdb      # 읽기 → 후보 여럿
ConvertKey  = space               # space | tab | hanja | convert | f9
```

후보 사전 원본은 `Type = candidates` 이고, 같은 읽기를 후보 수만큼 적는다. 적은 차례가 보여 줄
차례다:

```text
JamotongData 1
Type = candidates
かな	仮名
かな	金娜
```

백스페이스는 읽기 한 글자를 되돌리고, Esc 는 읽기를 버리며, 자판을 바꾸거나 창을 떠나면 읽은
그대로 확정한다 — 입력기가 대신 골라 주지 않는다. 읽기가 바뀐 뒤에 도착한 선택은 버린다.
`Candidates` 가 없으면 예전 그대로다: 글자가 바로 문서로 간다.

**앞단 조합(선택).** 입력 자판은 조합도 선언할 수 있다. 그러면 조합이 먼저 결정하고, 그 조합이
`symbol` 로 낸 것만 엔진으로 들어간다 — 같은 물리 글쇠를 둘이 겹쳐 먹지 않는다:

```ini
Key jkl; = 0
Chord j  = symbol "k"      # 엔진으로 보내는 논리 입력 (키 이벤트가 아니다)
Chord jk = symbol "a"      # 더 큰 조합이 이긴다
Chord l  = text "hello"    # text·key 는 엔진을 건너뛰고 실제 입력으로 나간다
Hold j   = momentary layer(num)
Hold ;   = oneshot mod(shift)   # 길게 눌러 Shift 를 걸어 두고 떼면 다음 조합에 적용
```

엔진이 못 받는 symbol(그 글자로 시작하는 항목이 사전에 없다)은 친 그대로 찍혀 사라지지 않는다.
조합 자판(`Type = chord`)에는 엔진이 없으므로 거기 쓴 `symbol` 은 오류다. 레이어·탭홀드·포인터·
매크로 등 조합 자판의 나머지는 입력 자판에서도 똑같이 쓸 수 있다.

**함께 설치되는 한글 자판.** 설치하면 자판 여섯 벌이 같이 깔린다 — 두벌식 표준, 세벌식 최종(3-91),
세벌식 390, 세벌식 최종 순아래, 세벌식 3-2011, 세벌식 3-2012. 모두 공개된 배열표를 보고 자모통이
새로 적은 것이다(권리 근거는 `COPYRIGHT.md`). 고쳐 쓰려면 설치 폴더의 `layout-*.jmt` 를 복사해
`%APPDATA%\Jamotong\layouts` 에 두고 고치면 된다 — 관리자 앱이 다시 구워 준다.

**윈도우 자판 들여오기.** 쓰고 싶은 자판이 이미 Microsoft Keyboard Layout Creator 원본(`.klc`)으로
있다면 그대로 바꾼다:

```sh
jamotong --import-klc us-dvorak.klc -o dvorak.jmt
jamotong --check dvorak.jmt
```

글쇠마다 기본 면과 Shift 면을 정적 자판으로 옮긴다. 데드키·합자·AltGr 면은 옮기지 않는다 —
몇 개였는지 알려 주므로 손으로 보탤 것을 알 수 있다.

**날개셋 자판 들여오기.** 세벌식 자판들은 대개 날개셋 한글 입력기의 자판 파일(`.key` = 글쇠 배열,
`.ist` = 입력기 유형)로 돌아다닌다. 그 **글쇠 배열**을 한글 자판으로 옮긴다:

```sh
jamotong --import-ngs "세벌식 3-2012.key" -o 3-2012.jmt
jamotong --check 3-2012.jmt
```

나온 파일은 **v4 문법**이다. 낱자 글쇠를 옮기고, 한 글쇠가 **자리에 따라 다른 낱자**를 내면
(순아래·갈마들이) `when jongslot` 같은 가드로 적는다 — 종성 자리면 종성, 중성 자리면 중성,
아니면 초성. 날개셋 수식의 **조건 자체는 읽지 않는다**(그쪽 오토마타 상태 번호라 뜻을 알 수 없다);
우리가 쓰는 것은 "이 글쇠가 어느 자리의 낱자를 낼 수 있는가"라는 꼴뿐이므로, 옮긴 자판은 한 번
쳐 보고 확인하는 것이 좋다. 겹받침·겹모음 표는 표준 현대 한글 규칙으로 얹어 준다(날개셋 파일에는 없다).
옮기지 않는 것은 세어서 알려 준다: 낱자가 없는 수식(기호·기능), 한글 자판에서의 글자 글쇠,
우리가 모르는 낱자 코드(옛한글 등), 오토마타·옵션 일체.

**이미 가진 사전 자료 들여오기.** 쓸 만한 낱말 자료는 크고, 대개 남이 만든 것이다. 자모통은
그런 자료를 동봉하지 않는다 — 고른 것을 바꿔 줄 뿐이다:

```sh
jamotong --import-dict japanese.txt -o kana-words.jdt --limit 50000 \
         --name "Japanese words" --license "see DICTIONARY-LICENSE.txt"
jamotong --build-dict  kana-words.jdt -o kana-words.jdb
```

`--import-dict` 는 두 칸 TSV(`읽기<탭>표기`)와, 열린 일본어 사전 여럿이 쓰는 다섯 칸 형식
(`읽기 lid rid 비용 표기`)을 읽는다. 읽기로 정렬하고 같은 읽기 안에서는 비용 순으로 두므로, 가장
싼(흔한) 후보가 먼저 나온다. `--limit N` 은 싼 것부터 N줄만 남긴다 — 90MB 짜리 사전을 바로
열리는 크기로 줄이는 방법이다. 읽기는 가나·한글 등 어떤 글자든 되고 UTF-8 96바이트(가나 서른두 자
남짓)까지, 표기는 64글자까지다. 주석·빈 줄·한도를 넘는 줄·UTF-8 이 깨진 줄은 건너뛰고 세어
알려 준다 — 변환기는 **컴파일러가 거절할 줄을 내놓지 않는다**. 같은 (읽기, 표기)가 여러 번 오면
(품사만 다른 자료에서 흔하다) 한 번만 남겨 후보창이 중복으로 차지 않게 하고, `--limit` 는 그 뒤에
건다. `--name`·`--license` 는 사전 자체에 새겨져, 남에게 넘어간 사전도 제 출처를 말한다. 들여온 항목은 원래 파일의
라이선스를 그대로 따르니, 남에게 넘기기 전에 확인할 것.

열린 일본어 사전 한 조각(7MB, 128,908줄)으로 실측: 123,381항목을 남기고 5,527줄은 읽기가 길어
제외, 변환 0.08초·굽기 0.08초, `.jdb` 5.3MB, 여는 데 1.6ms.

**일본어(로마자).** 설치기의 **일본어** 구성 요소(0.70.0)가 **Japanese (romaji)** 자판을 켠 채로 넣는다. 로마자로 친다
(`nihongo`, `kka` 는 っか, `-` 는 ー). 읽기는 가나로 보인다. **사이띄개**로 변환: 첫 후보는 문장 전체
(`denshadekaishaniiku` → 電車で会社に行く), 그다음 읽기의 낱말, 히라가나·가타카나. 1~9 로 고르고, **엔터**는 가나
그대로, Esc 는 지운다. **문절 편집(0.73.0)**: 문장이 문절 둘 이상이면 사이띄개가 바꾼 문장을 보이고 고치는 문절을 괄호로
두른다(`[私は]日本語を話します`) — 후보는 그 문절의 것이다. **←/→** 로 문절을 옮기고, **Shift+←/→** 로 그 문절을 줄이거나
늘리고(뒤는 다시 가른다), **↑/↓**·사이띄개로 후보를 옮기고, 1~9 로 고르면 다음 문절로 간다. **엔터**는 문장을 넣고,
백스페이스·Esc 는 가나로 돌아간다. 이어서 치면 문장이 먼저 들어간다. 고른 것을 배우지는 않는다. **F6** 히라가나, **F7** 가타카나, **F8** 반각 가타카나, **F9** 전각·**F10** 반각 로마자. 문장부호는
、。「」・〜！？ 로 나온다(Layout Options 에서 끌 수 있다). **문구 치환**: Layout Options ▸ Edit custom phrases 가
`japanese-phrases.txt` 를 연다 — 한 줄에 하나, 가나 읽기 다음에 글(`よろ よろしくお願いします`), 후보 맨 앞에 나온다. 사전은
오픈소스 Mozc 사전(약 50만 항목)과 그 **품사 연결 비용**이다(0.71.0): 낱말 비용에 앞 낱말 뒤에 올 때의 비용을 더해
Mozc 와 같은 셈으로 문장을 고른다(私は日本語を話します, 友達と映画を見ました) — 시험 문장 50개 중 42개가 곧바로 맞는다(0.70.0 은
15개). 더 넓은 문맥이 필요한 낱말(降る/フル, 重い/思い)은 후보에서 고를 수 있다. 학습은 하지 않는다. 라이선스는 `licenses\japanese` 에 깔린다.

**중국어(병음).** 설치기의 구성 요소 둘이다(0.70.0; 0.62~0.69 는 따로 된 MSI): **중국어 간체**, **중국어 번체**. 각각 제 자판 —
**Chinese Simplified (pinyin)** 또는 **Chinese Traditional (pinyin)** — 을 자판 목록에 **켠 채로** 넣는다(끄면 꺼진 채
남는다). 성조 없이 병음을 치면 다른 중국어 입력기처럼 **치는 동안 후보가 뜬다**. 사이띄개는 하이라이트된 후보, 1~9 는 그
후보, `-` `=` 는 쪽 넘김, `[` `]` 는 그 낱말의 첫·끝 글자만(以词定字), 엔터는 친 로마자 그대로, Esc 는 읽기를 지운다.
첫 후보는 **문장 전체**(`woaini` → 我爱你, `jintiantianqihenhao` → 今天天气很好), 그다음 읽기 그대로의 낱말과 **추천
단어** — 친 것으로 시작하는 더 긴 낱말·성어(`yijian` → 一箭双雕)와 **첫 글자 줄임**(`zg` → 中国, `wsm` → 为什么, `y` → 有) —
이다. 앞부분만 쓰는 낱말을 고르면 나머지가 남고 그 후보가 곧바로 뜬다. `'` 는 음절 끊기(`xi'an` → 西安), `ü` 는 `v`
(`lv` → 绿), `rq`·`sj`·`xq` 는 날짜·시각·요일. 문장부호는 중국어 꼴(，。？！、“”‘’ — 번체는 「」『』)로 바뀌고 숫자 바로
뒤의 `.` `,` `:` 는 그대로다(3.14). 번체 자판은 대만 표기(裡·為·眾)를 쓰고, 번체가 여럿인 글자는 모두 낸다(发 → 發·髮).
설정 → **Layout Options** 에서 자판마다 문장 후보·추천 단어·중국어 문장부호·모호음(z=zh, n=l, an=ang …)을 켜고 끄고,
**글쇠**를 고른다: 온 병음 또는 쌍병 — 小鹤·自然码·微软, 한 음절을 두 글쇠로. `v` 뒤의 숫자(쌍병은 Shift+V)는 **V 모드**다:
`v123` → 一百二十三 / 壹佰贰拾叁, `v3.14` → 三点一四, `v2026.10.3` → 날짜, `v1+2*3` → 7. "Edit custom phrases..." 는
`chinese-phrases.txt` 를 연다: 한 줄에 글쇠와 구(`dz 北京市海淀区`), 맨 앞 후보로 뜬다. **이모지·기호**는 그 낱말
뒤에 뜨고(`kaixin` → 开心 ☺ 😄, `dianhua` → ☎ 📞 — 이름은 Unicode CLDR), 후보 옆에 **성조 병음**(你 nǐ, 中国 zhōng guó)이
보인다. 둘 다 끌 수 있다. 읽기는 mozillazg 의
pinyin-data·phrase-pinyin-data, 차례는 jieba 의 낱말 빈도(셋 다 MIT), 번체 표기는 OpenCC(Apache-2.0)에서 왔고 라이선스
원문은 자모통 옆 `licenses\chinese-*` 에 설치된다. 아직 중국어 화자의 확인은 받지 않았다.

- 한도: 순차 사전의 친 쪽은 볼 수 있는 ASCII 32자까지, 후보 사전의 읽기는 UTF-8 96바이트까지,
  한 항목이 내는 글자는 64자까지, 사전 하나에 50만 항목까지. 32바이트를 넘는 읽기를 쓰는 사전은
  v2 로 구워지고, 0.44 이하 자모통은 그 사전을 **잘못 읽는 대신 거절**한다.
- **자판을 읽을 때 사전을 전수로 본다** — 검사합·키 차례·키 글자. 그래서 사전이 깨졌으면 그 자판은
  어느 길로 들어오든 목록에 뜨지 않는다. 읽을 때마다 파일을 한 번 훑는다(10만 항목에 약 7ms).
  `jamotong --check 자판.jmt` 가 똑같은 검사를 하고 쓴 사전 이름을 알려 준다.


## 삭제 (언인스톨)

1. **설정 ▸ 앱 ▸ 설치된 앱 ▸ Jamotong ▸ 수정 ▸ 제거.** ("제거" 단추는 일부러 막았다 — 윈도의 맨 창이 영어로, 기본 단추
   Cancel 로 뜨기 때문이다.) 두 입력기 등록을 해제하고 파일을 지운다. (zip 으로 손수
   설치했다면: 관리자 명령 프롬프트에서 `jamotong.exe --unregister` 를 실행하고 폴더를 지운다.)
2. 실행 중인 앱이 물고 있는 DLL 은 지울 수 없어 윈도가 옆으로 옮겨 두고 다음 재시작 때 지운다. 재부팅은
   필요 없다 — 돌던 앱은 끝날 때까지 이전 사본을 쓴다.
3. 설정은 `%APPDATA%\Jamotong` 에 남는다. 다시 설치하지 않을 거라면 그 폴더도 지운다.

## 빌드

MinGW-w64 크로스 컴파일 (Linux에서):

```sh
make            # dist/jamotong.dll (x64)
make win32      # dist/jamotong32.dll (x86)
make configapp  # dist/jamotong.exe (관리 앱: .jmt 편집/설정/입력 테스트)
make stage      # 위 전부 빌드 + redist/(한자 데이터·자판) 를 dist/에 복사
                #  → dist/ 가 설치할 파일 (설치 절의 --register 참고)
```

`redist/`에는 실행에 필요한 재배포 데이터가 있다: 한자 독음 테이블(`hanja.txt`),
훈음 표(`hanja_hunum.txt`), Unicode License 사본, 예제·배포 자판(`.jmt`).

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
