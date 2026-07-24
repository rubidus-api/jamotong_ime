# WinAPI + C 만으로 Windows 한글 IME 구현하기 — 실전 매뉴얼

**한국어** | [English](winapi-c-ime-manual.md)

*최종 갱신: 2026-07-24 (AkelPad Meta R3 구조 실기 결과·R4 관찰자 효과 분리 설계)*


이 문서는 **순수 C(C23)와 Win32 API만으로**(C++·ATL·MFC·프레임워크 없이) Windows용
한글 입력기(IME)를 처음부터 구현하는 방법을, 우리가 `jamotong` 프로젝트에서 실제로
겪은 시행착오와 최종 정답까지 포함해 정리한 것입니다.

대상 독자: WinAPI와 C는 알지만 COM/TSF는 처음인 개발자.
목표: 이 문서만 보고 다른 IME를 밑바닥부터 만들 수 있게 하는 것.

> **한 줄 결론(스포일러)**: Windows에는 TSF와 IMM32가 있고, 호환 계층·컨트롤 구현·자체
> 렌더러에 따라 실제 동작 계약이 달라진다. **모든 앱에 통하는 단일 조합·주입 전략은 확인하지
> 못했다.** 그래서 제품은 완성 음절만 확정하는 커밋 전용 엔진, 앱별 주입 경로, 별도 프리뷰를
> 조합했다. 이것은 “TSF 조합이 원리적으로 불가능하다”는 결론이 아니라 현재 앱 행렬에서 검증된
> 신뢰성 선택이다. 실패와 남은 가설은 §8·§12.7·§13에 구분해 기록한다.

---

## 목차
0. [준비물](#0-준비물)
0.5 [★처음부터 따라하기 — 30분에 뜨는 최소 IME](#05-처음부터-따라하기--뜨고-키-하나를-먹는-ime-만들기)
1. [COM 기초 — C로 COM 구현하기](#1-com-기초--c로-com-구현하기)
2. [TSF 지형도 — TSF·IMM32·CUAS](#2-tsf-지형도)
3. [TIP 골격 — 최소 텍스트 서비스](#3-tip-골격)
4. [등록(Registration)](#4-등록)
5. [키 입력 처리](#5-키-입력-처리)
6. [한글 오토마타(조합 로직)](#6-한글-오토마타)
7. [문서에 글자 넣기 — 편집 세션](#7-편집-세션--문서에-글자-넣기)
8. [★핵심 교훈: CUAS의 벽과 "커밋 전용"](#8-핵심-교훈-cuas의-벽과-커밋-전용)
9. [부가 기능: 한자·경계키·설정](#9-부가-기능)
10. [함정 모음(Gotchas)](#10-함정-모음-gotchas)
11. [최소 IME 체크리스트](#11-최소-체크리스트)
12. [★커밋 전용 '이후'의 실전 교훈](#12-커밋-전용-이후의-실전-교훈)
13. [★★텍스트 주입은 한 가지가 아니다 — 앱 클래스별 전략](#13-앱-클래스별-텍스트-주입)

부록 A. [jamotong 소스 매핑](#부록-a-jamotong-소스-매핑)
부록 B. [★참고 자료(공식 문서 링크)](#부록-b-참고-자료)

---

## 0. 준비물

- **컴파일러**: MinGW-w64 (`x86_64-w64-mingw32-gcc`, `i686-w64-mingw32-gcc`). MSVC 없이 됨.
- **결과물**: `.dll` (TSF IME는 in-proc COM 서버 DLL). 32비트 앱을 지원하려면 32/64 둘 다 빌드.
- **핵심 헤더**: `<windows.h>`, `<msctf.h>`(TSF), `<initguid.h>`(GUID 정의), `<olectl.h>`.
- **링크**: `-lole32 -luuid`(COM/TSF GUID) + `-loleaut32`(BSTR·VARIANT·`VariantClear`).
  TSF 심볼은 `msctf.h` + `-luuid`로 대부분 해결한다. `-municode`는 DLL에 불요.
- **빌드 형태**:
  `gcc -shared -o jamotong.dll *.c jamotong.def -lole32 -loleaut32 -luuid -lgdi32 ...`
  - `.def` 파일로 `DllGetClassObject`, `DllCanUnloadNow`, `DllRegisterServer`,
    `DllUnregisterServer`를 export (32비트는 이름 장식 때문에 `.def`가 사실상 필수).

---

## 0.5 처음부터 따라하기 — "뜨고, 키 하나를 먹는" IME 만들기

TSF 문서는 방대해서 어디부터 손댈지 막막하다. 그래서 **동작하는 최소한**을 먼저 만든다.
`examples/minimal-tip/`에 약 300줄짜리 학습용 코드가 있다. 이 예제는 로컬에서 아래 명령으로
빌드해 확인하는 기준 구현이지, 저장소 CI가 모든 변경을 자동 보증한다는 뜻은 아니다.

목표: **Windows 언어 목록에 뜨고, `a` 키를 먹어 문서에 `ㄱ`을 넣는다.** 그게 전부다.
파일 두 개(`minimal.c`, `minimal.def`)면 된다.

### 왜 이 순서인가

IME는 "키를 받아 글자를 넣는 프로그램"이 아니라 **"OS가 로드하는 COM 서버"**다.
그래서 글자를 넣기 전에 넘어야 할 관문이 셋이다.

```
① COM 서버가 되어야     → DllGetClassObject 로 객체를 내줄 수 있어야 한다
② IME로 등록되어야      → 언어 목록에 떠야 사용자가 고를 수 있다
③ 키를 받아야           → 키 싱크를 붙여야 키가 온다
④ 그제서야 글자를 넣는다 → 편집 세션 안에서만
```

①~③ 중 하나라도 빠지면 **아무 일도 안 일어난다.** 그런데 오류 메시지도 안 뜬다 —
이게 처음 하는 사람을 가장 괴롭히는 부분이다. 그래서 최소 예제로 ①~③을 먼저 통과시킨다.

### 1단계 — 빌드

```sh
cd examples/minimal-tip
make            # minimal.dll   (x64)
make win32      # minimal32.dll (x86)
```

`.def` 파일이 왜 필요한가: 32비트에서 `DllRegisterServer`는 `_DllRegisterServer@0` 처럼
장식된 이름으로 export된다. `regsvr32`는 장식 없는 이름을 찾으므로 못 만난다.
`.def`로 이름을 고정해야 한다.

### 2단계 — 등록

**관리자 권한** 명령 프롬프트에서:

```
regsvr32 minimal.dll
```

성공하면 대화상자가 뜬다. 실패하면 대개 이 셋 중 하나다.

| 증상 | 원인 |
|---|---|
| "모듈을 로드할 수 없습니다" | 비트수 불일치. 64비트 `regsvr32`로 32비트 DLL을 등록하려 함 |
| "진입점을 찾을 수 없습니다" | `.def` 누락 또는 이름 불일치 |
| 성공했는데 목록에 안 뜸 | **카테고리 등록 누락** — `GUID_TFCAT_TIP_KEYBOARD` |

### 3단계 — 확인

설정 → 시간 및 언어 → 언어 및 지역 → 한국어 → 옵션 → 키보드 에 **"Minimal TIP"**이 보이면
①②가 통과한 것이다. 안 보이면 다음을 확인한다.

```
레지스트리에 CLSID가 있나:  HKCR\CLSID\{7B1F4C20-...}\InprocServer32
그 값이 실제 DLL 경로인가:  (경로가 틀리면 조용히 실패한다)
```

### 4단계 — 쳐 보기

메모장을 열고 입력기를 "Minimal TIP"으로 바꾼 뒤 `a`를 누른다. `ㄱ`이 나오면 성공이다.

**안 나온다면** — 여기서부터가 진짜 시작이다. §2.2의 앱 분류표를 보라.
이 프로젝트의 시험 행렬에서 메모장은 **알려진 정상 비교 호스트**였다. 여기서도 안 되면 먼저
키 싱크와 활성화·등록 경로를 확인하되, 다른 앱의 성공까지 보장한다고 해석하지 않는다.
`Activate`에 `OutputDebugStringW(L"activated\n")`를 넣고 DebugView로 확인하는 게 가장 빠르다
(§12.6 진단 방법론).

### 5단계 — 제거

```
regsvr32 /u minimal.dll
```

**개발 중에는 반드시 제거하고 다시 등록하라.** DLL 경로가 바뀌었는데 레지스트리가 옛 경로를
가리키면, 이미 로드된 프로세스는 옛 DLL을 계속 쓴다. 증상이 "고쳤는데 안 고쳐진다"로 나타난다.

### 이 예제가 일부러 안 하는 것

`minimal.c`는 **배선 학습용**이다. 현재 짧은 `OnKeyDown`도 `InsertChar`의 HRESULT를 받아
`S_OK`일 때만 `A`를 먹으므로, 즉시 실패 뒤 키를 무조건 삼키지는 않는다. 그러나 그 `S_OK`도
write 수락이지 반영 postcondition 증명은 아니다. 최소 예제는 request/session/inner 결과와
mutation을 제품 수준으로 보존하지 않는다. 실제 구현은 §7.1의 결과 분리와 §7.2의
“`DONE` 증명 뒤 FSM publish” 패턴으로 확장한다.

빌드는 이 절의 `make`와 `Makefile`을 기준으로 한다. DLL에는 `-municode`가 필요 없으며 소스
머리말과 Makefile 모두 이를 생략한다. Makefile은 `-Werror`도 켜서 예제의 경고를 빌드 실패로
취급한다.

| 안 하는 것 | 왜 | 어디서 |
|---|---|---|
| 한글 조합(오토마타) | 키 → 글자 경로만 보이려고 | §6 |
| 조합 미리보기(밑줄) | 시험한 일부 호환 호스트에서 세션 뒤 종료됐다 | §8 |
| 한/영 전환 | 상태 관리가 붙으면 최소가 아니다 | §10 (`ITfCompartment::GetValue`) |
| 앱 클래스별 분기 | 메모장만 되면 충분하다고 착각하기 쉬운데, 아니다 | §13 |
| 실패 뒤 상태·키 처리 | 최소 경로와 제품용 원자성을 분리 | §7.1~§7.2 |

**특히 §8을 반드시 읽어라.** 조합 미리보기가 시험한 일부 호환 호스트에서 반복해서 깨진 것이
이 프로젝트가 20회 넘는 실기에서 부딪힌 벽이고, 최소 예제가 그것을 처음부터 피해 가는 이유다.

---

## 1. COM 기초 — C로 COM 구현하기

TSF는 전부 COM 인터페이스다. C++이면 상속으로 되지만 **C에는 클래스가 없으므로 COM의 ABI를
손으로 만든다.** 사실 COM의 실체는 아주 단순하다.

### 1.1 COM 인터페이스 = "함수 포인터 표(vtbl)를 첫 멤버로 갖는 구조체"

C++의 `interface IFoo`는 런타임에 이렇게 생겼다:

```
객체 ─► [ vtbl 포인터 ] ─► [ QueryInterface, AddRef, Release, Method1, Method2, ... ]
        [ 객체 데이터... ]
```

- 모든 COM 인터페이스는 **`IUnknown`** 을 상속한다 = vtbl의 **첫 3개 함수는 항상**
  `QueryInterface`, `AddRef`, `Release`.
- `msctf.h`가 `ITfTextInputProcessorVtbl` 같은 vtbl 구조체 타입을 이미 정의해 준다.

### 1.2 C에서 인터페이스 하나 구현하기

객체 구조체의 **첫 멤버를 vtbl 포인터**로 두면, 인터페이스 포인터 == 객체 포인터가 된다:

```c
typedef struct {
    ITfKeyEventSinkVtbl *lpVtbl;   // ★ 반드시 첫 멤버
    LONG refCount;
    /* ... 내 데이터 ... */
} MyKeyEventSink;

static HRESULT STDMETHODCALLTYPE KES_QueryInterface(ITfKeyEventSink *This, REFIID riid, void **ppv){
    MyKeyEventSink *self = (MyKeyEventSink*)This;   // 첫 멤버가 vtbl이라 캐스팅만 하면 됨
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfKeyEventSink)) {
        *ppv = self; This->lpVtbl->AddRef(This); return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE KES_AddRef(ITfKeyEventSink *This){
    return InterlockedIncrement(&((MyKeyEventSink*)This)->refCount);
}
static ULONG STDMETHODCALLTYPE KES_Release(ITfKeyEventSink *This){
    MyKeyEventSink *self = (MyKeyEventSink*)This;
    LONG n = InterlockedDecrement(&self->refCount);
    if (n == 0) HeapFree(GetProcessHeap(), 0, self);
    return n;
}
static HRESULT STDMETHODCALLTYPE KES_OnKeyDown(ITfKeyEventSink *This, ITfContext *pic,
                                               WPARAM w, LPARAM l, BOOL *pfEaten){ /* ... */ }
/* ... 나머지 메서드 ... */

// vtbl 인스턴스 (함수 포인터 순서 = 인터페이스 정의 순서. 틀리면 즉시 크래시)
static ITfKeyEventSinkVtbl g_KES_Vtbl = {
    KES_QueryInterface, KES_AddRef, KES_Release,
    KES_OnSetFocus, KES_OnTestKeyDown, KES_OnTestKeyUp,
    KES_OnKeyDown, KES_OnKeyUp, KES_OnPreservedKey
};
// 객체 생성 시: obj->lpVtbl = &g_KES_Vtbl;
```

**호출 규약(`STDMETHODCALLTYPE` = `__stdcall`)과 vtbl의 함수 순서**를 정확히 지키는 게 전부다.

### 1.3 한 객체가 여러 인터페이스를 구현할 때 (IMPL_TO_OBJ 트릭)

IME 객체 하나가 `ITfTextInputProcessor`, `ITfKeyEventSink`, ... 여러 인터페이스를 동시에
구현해야 한다. C에선 **각 인터페이스마다 vtbl 포인터 멤버를 하나씩** 두고, 그 멤버의 주소로부터
`offsetof`으로 객체 시작 주소를 역산한다:

```c
typedef struct JamotongTextService {
    ITfTextInputProcessorVtbl *lpVtblTIP;   // 인터페이스 #1
    ITfKeyEventSinkVtbl       *lpVtblKES;   // 인터페이스 #2
    /* ... */
    LONG refCount;
    /* 데이터 */
} JamotongTextService;

// 임의 인터페이스 포인터 → 객체 포인터
#define IMPL_TO_OBJ(Name, pThis) \
    ((JamotongTextService*)((char*)(pThis) - offsetof(JamotongTextService, lpVtbl##Name)))

// 예: KeyEventSink 메서드 안에서
JamotongTextService *obj = IMPL_TO_OBJ(KES, pThis);   // pThis는 &obj->lpVtblKES를 가리킴
```

- `QueryInterface`는 요청받은 IID에 맞는 **그 인터페이스의 vtbl 멤버 주소**를 돌려준다:
  `*ppv = &obj->lpVtblKES;` (객체 주소가 아니라 **해당 vtbl 멤버의 주소**).
- 모든 인터페이스의 AddRef/Release는 **객체의 단일 refCount**로 위임한다(보통 TIP의 것으로).

### 1.4 클래스 팩토리와 DLL 진입점 4종

TSF는 우리 CLSID로 `CoCreateInstance`를 호출한다 → OS가 우리 DLL의 `DllGetClassObject`를 불러
**클래스 팩토리**(`IClassFactory`)를 얻고, 팩토리의 `CreateInstance`가 우리 TIP 객체를 만든다.

DLL이 반드시 export할 4함수:

```c
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv);  // 팩토리 반환
STDAPI DllCanUnloadNow(void);                 // 살아있는 객체 수 0이면 S_OK
STDAPI DllRegisterServer(void);               // 레지스트리 등록 (regsvr32)
STDAPI DllUnregisterServer(void);             // 등록 해제 (regsvr32 /u)
```

`IClassFactory`도 §1.2 방식으로 C로 구현한다(`QueryInterface/AddRef/Release/CreateInstance/LockServer`).
전역 객체 카운터(`g_cRefDll`)로 `DllCanUnloadNow`를 판단한다.

> **함정**: `.def`에 이 4개를 안 넣으면 `regsvr32`가 "DllRegisterServer를 찾을 수 없음"으로 실패.
> 32비트는 `__stdcall` 이름 장식(`_DllRegisterServer@0`) 때문에 `.def` 없이는 export 이름이 어긋난다.

---

## 2. TSF 지형도

### 2.1 Windows의 입력 시스템은 두 개다

| 시스템 | 시대 | 우리가 만드는 것 |
|---|---|---|
| **TSF** (Text Services Framework) | 모던(XP+) | ← **이걸로 만든다** (TIP = Text Input Processor) |
| **IMM32** (Input Method Manager) | 레거시(9x) | 옛 방식. 이 프로젝트의 Windows 11 시험에서는 설치·활성화에 실패(§8.3) |

- **CUAS** (Cicero Unaware Application Support): 아직 IMM32로 입력받는 앱을 위해, OS가 TSF 조합을
  IMM32 메시지(`WM_IME_*`)로 번역해 주는 호환 경로. 이 프로젝트의 여러 실패는 이 경계에서
  관측됐지만, 모든 호스트 문제의 원인을 CUAS 하나로 일반화하지 않는다.

### 2.2 ★앱은 3부류이고, 각자 되는 게 다르다 (이 표가 이 문서의 심장)

| 앱 부류 | 예 | TSF 조합(밑줄 미리보기) | range 편집(넣은 글자 교체) | 단순 삽입 |
|---|---|:---:|:---:|:---:|
| **네이티브 TSF** | TSF를 온전히 지원하는 에디터·웹 콘텐츠 컨트롤 | ✅ | ✅ | ✅ |
| **호환 텍스트 스토어/CUAS 경로** | TSF를 IMM32 앱에 중개하는 고전 Win32 경로 | ⚠️ 호스트별로 유지 또는 종료 | ⚠️ 구현별 차이 큼 | ⚠️ 대체로 가능하나 예외 있음 |
| **터미널·자체 렌더러** | 표준 에디트 컨트롤 없이 텍스트를 직접 그리는 앱 | ⚠️ 앱별 차이 큼 | ⚠️ 대개 제한적 | ⚠️ 실제 키/메시지가 필요할 수 있음 |

- **네이티브 TSF**는 조합·range 편집 계약을 직접 제공하므로 가장 예측 가능하다.
- 호환 경로와 자체 렌더러를 한 부류로 단정하지 않는다. 같은 TIP가 메모장에서는 조합을
  유지하고 AkelPad에서는 매 키마다 종료됐으며, 터미널은 텍스트 삽입보다 실제 키가 필요하기도 했다.
- 커서 삽입은 넓게 통했지만 이것도 절대적 공통분모는 아니었다(§13). 제품의 “커밋 전용”은
  관측한 앱 행렬에서 실패 면적을 줄인 **폴백 정책**이지 Windows API의 보장 문구가 아니다.

> **2026-07-23 실기 메모.** 별도 표준 TSF 실험체는 Windows 메모장에서 조합 유지에 성공했다.
> 64비트 AkelPad에서는 `가나다` 대신 호환 자모가 각각 확정됐다. 구조 로그상 모든
> `StartComposition`·`SetText`·표시 속성·`SetSelection`·편집 세션은 `S_OK`였지만, 각 세션이
> 닫힌 직후 선택 변경 없는 추가 `OnEndEdit`와 `OnCompositionTerminated`가 차례로 왔다. AkelPad
> 컨텍스트의 정적 상태에는 `TF_SS_TRANSITORY`도 있었다. AkelEdit 소스가 IMM32 조합 메시지를
> 직접 처리한다는 사실까지 합치면 CUAS/IMM 호환 경로라는 강한 증거지만, 로그만으로 종료 요청자를
> 특정하거나 `SetSelection` 하나를 원인으로 단정할 수는 없다. 후속 네 프로필은 Control,
> `TF_AE_NONE`, 선삽입 후 조합, 명시적 선택 생략 모두 AkelPad에서 똑같이 자모 분리됐고
> 메모장에서는 모두 정상 조합됐다. 추가 편집은 텍스트·`GUID_PROP_COMPOSING`·
> `GUID_PROP_ATTRIBUTE`를 바꿨지만 `GUID_PROP_READING`은 바꾸지 않았다. 따라서 이 세 프로토콜
> 차이는 단독 해결책이 아니다. 정정된 metadata 실기에서는 Reading-only만 AkelPad에서
> 하나의 composition을 유지했고, LANGID가 들어간 두 profile은 계속 매 키 종료됐다.
>
> **2026-07-24 R3 구조 결과.** 네 R3 프로필 모두 예상 metadata 호출·read-back·세션 검증은
> 통과했다. 그러나 AkelPad에서는 모두 composition 6회 생성·0회 재사용·6회 종료였고,
> 메모장에서는 모두 1회 생성·5회 재사용·키별 종료 0이었다. R2의 양성 대조였던 Reading도
> R3에서는 실패했다. R3에는 R2에 없던 즉시 `GetValue` read-back과 동기 추적 기록이 들어가
> 있으므로 LANGID 존재·순서·반복 쓰기의 인과를 판정할 수 없다. 제출된 R3 산출물에는 화면
> 문자열 판정도 없어서 이 문서는 **구조 수명만** 보고한다. 이를 분리하는 R4 설계는 §12.7.8에
> 기록한다.

---

## 3. TIP 골격

### 3.1 필수 인터페이스 (최소 IME)

| 인터페이스 | 역할 | 필수? |
|---|---|:---:|
| `ITfTextInputProcessor` | `Activate`/`Deactivate` 진입점 | ✅ |
| `ITfKeyEventSink` | 키 입력 처리 | ✅ |
| (클래스 팩토리 `IClassFactory`) | 객체 생성 | ✅ |
| `ITfDisplayAttributeProvider` | 조합 밑줄 색/스타일 제공 | TIP이 표시 속성을 제공할 때만 |
| `ITfCompositionSink` | 조합이 외부로 종료될 때 알림 | 선택; 종료 관측에는 권장 |
| `ITfFunctionProvider`+`ITfFnConfigure` | "옵션" 버튼→설정창 | 선택 |
| `ITfThreadMgrEventSink`/`ITfTextEditSink` | 문서 포커스/편집 추적 | 선택 |

> **커밋 전용 IME(§8)라면 `ITfDisplayAttributeProvider`·`ITfCompositionSink`는 필요 없다.**
> 조합을 아예 안 하기 때문이다. 현재 jamotong은 `ITfCompositionSink`를 제거했지만,
> `ITfDisplayAttributeProvider`는 쓰지 않는 호환 잔재로 QI 노출·카테고리 등록을 유지한다.
> 이것은 프로젝트 현황이지 커밋 전용 TIP의 일반 요구 사항이 아니다. 조합을 쓰는 TIP도 선택
> sink 자리에 `NULL`을 넘길 수 있지만, 외부 종료를 관측하고 정리해야 한다면 유효한 sink를
> 넘기는 편이 안전하다(§7.3).

### 3.2 Activate / Deactivate

```c
HRESULT STDMETHODCALLTYPE
TIP_Activate(ITfTextInputProcessor *This, ITfThreadMgr *ptim,
             TfClientId tid)
{
    JamotongTextService *obj = (JamotongTextService *)This;
    ITfKeystrokeMgr *ksm = NULL;
    HRESULT hr;

    if (ptim == NULL) return E_INVALIDARG;
    if (obj->threadMgr != NULL) return E_UNEXPECTED;

    obj->threadMgr = ptim;
    ptim->lpVtbl->AddRef(ptim);                 /* Deactivate까지 보관 */
    obj->clientId = tid;
    obj->keySinkAdvised = FALSE;

    hr = ptim->lpVtbl->QueryInterface(
        ptim, &IID_ITfKeystrokeMgr, (void **)&ksm);
    if (SUCCEEDED(hr) && ksm == NULL) hr = E_NOINTERFACE;
    if (SUCCEEDED(hr)) {
        ITfKeyEventSink *sink =
            (ITfKeyEventSink *)&obj->lpVtblKES;
        hr = ksm->lpVtbl->AdviseKeyEventSink(
            ksm, tid, sink, TRUE /* fForeground */);
        if (SUCCEEDED(hr)) obj->keySinkAdvised = TRUE;
    }
    if (ksm != NULL) ksm->lpVtbl->Release(ksm);

    if (FAILED(hr)) {
        /* 성공한 Advise는 없다. 위에서 게시·보관한 필드를 모두 되돌린다. */
        obj->clientId = 0;
        obj->keySinkAdvised = FALSE;
        obj->threadMgr->lpVtbl->Release(obj->threadMgr);
        obj->threadMgr = NULL;
        return hr;
    }
    return S_OK;
}
// Deactivate: keySinkAdvised일 때만 Unadvise → threadMgr Release/NULL → 상태 리셋
```

`clientId`(TfClientId)는 **우리 TIP의 신분증**. 편집 세션 요청 등 거의 모든 TSF 호출에 넘긴다.

---

## 4. 등록

IME는 **세 겹**으로 등록된다. `DllRegisterServer`에서 다 처리한다.

### 4.1 COM 서버 등록 (레지스트리)
```
HKCR\CLSID\{our-clsid}\InprocServer32  (기본값) = DLL 전체경로,  ThreadingModel = Apartment
```

- DLL 비트수에 맞는 `regsvr32`를 사용하면 등록 코드는 양쪽 모두 논리 경로
  `HKCR\CLSID`에 쓴다: 64비트는 `System32\regsvr32`, 32비트는
  `SysWOW64\regsvr32`다. 시스템 범위 등록의 32비트 물리 view를 직접 검사할 때는
  `HKLM\Software\Classes\Wow6432Node\CLSID`를 본다
  (`HKCR`의 대응 WOW64 view를 써도 된다). **비트수별로 따로 등록**해야 각 비트수 앱에서
  로드된다.

### 4.2 TSF 프로파일 등록 (언어에 IME 붙이기)
```c
ITfInputProcessorProfiles *pp;
CoCreateInstance(&CLSID_TF_InputProcessorProfiles, ..., &IID_ITfInputProcessorProfiles, &pp);
// 모던 방식(Win11 트레이 브랜딩 아이콘이 뜨려면 이걸 써야 함):
ITfInputProcessorProfileMgr *mgr;
pp->lpVtbl->QueryInterface(pp, &IID_ITfInputProcessorProfileMgr, (void**)&mgr);
mgr->lpVtbl->RegisterProfile(mgr, &CLSID_Ours, LANGID_KO /*0x0412*/, &GUID_Profile,
        desc, descLen, iconDllPath, iconDllPathLen,
        iconIndex /* ★음수 = 리소스 ID */, NULL, 0, TRUE /*enabled*/, 0);
```

- **아이콘**: `iconIndex`에 **음수 리소스 ID**를 넘긴다(예: 리소스 ID 100 → `-100`).
  `-1`은 특수값이라 피할 것. 아이콘은 DLL에 `.rc`로 임베드(`100 ICON "app.ico"`).
- Win11 트레이 입력 표시기에 우리 아이콘이 뜨려면 **구식 `AddLanguageProfile`이 아니라
  `RegisterProfile`(ProfileMgr)** 을 써야 한다(실측).
- **아이콘이 두 종류다, 헷갈리지 말 것.** 여기 것은 **프로파일 브랜딩 아이콘** — 언어 목록·입력
  전환기에서 IME를 식별하는 *정적* 아이콘으로, 등록 때 한 번 설정한다. 트레이에 떠서 상태(한/A,
  현재 자판)에 따라 바뀌는 **실시간 모드 아이콘**은 별개의 런타임 요소 — §12.4의 언어바 아이템이다.

### 4.3 카테고리 등록 (능력 선언)
```c
/* 이 프로젝트의 MinGW msctf.h에 없는 두 공식 값을 독립 byte-check해 로컬 선언. */
static const GUID GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT_J =
    { 0x13a016df, 0x560b, 0x46cd,
      { 0x94, 0x7a, 0x4c, 0x3a, 0xf1, 0xe0, 0xe3, 0x5d } };
static const GUID GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT_J =
    { 0x25504fb4, 0x7bab, 0x4bc1,
      { 0x9c, 0x69, 0xcf, 0x81, 0x89, 0x0f, 0x0e, 0xf5 } };

static HRESULT SetCategories(BOOL add)
{
    const GUID *categories[] = {
        &GUID_TFCAT_TIP_KEYBOARD,
        /* 표시 속성을 제공하거나 호환 잔재로 유지할 때만 포함한다. */
        &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
        &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT_J,
        &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT_J
    };
    ITfCategoryMgr *cat = NULL;
    HRESULT first = CoCreateInstance(
        &CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
        &IID_ITfCategoryMgr, (void **)&cat);
    if (FAILED(first)) return first;
    if (cat == NULL) return E_UNEXPECTED;

    for (size_t i = 0; i < ARRAYSIZE(categories); ++i) {
        HRESULT step = add
            ? cat->lpVtbl->RegisterCategory(
                  cat, &CLSID_Ours, categories[i], &CLSID_Ours)
            : cat->lpVtbl->UnregisterCategory(
                  cat, &CLSID_Ours, categories[i], &CLSID_Ours);
        if (FAILED(step) && SUCCEEDED(first)) first = step;
        if (add && FAILED(step)) break; /* 아래 wrapper가 best-effort rollback */
    }
    cat->lpVtbl->Release(cat);
    return first;
}

HRESULT RegisterCategories(void)
{
    HRESULT hr = SetCategories(TRUE);
    if (FAILED(hr)) (void)SetCategories(FALSE);
    return hr;
}

HRESULT UnregisterCategories(void)
{
    return SetCategories(FALSE); /* 등록과 정확히 같은 GUID 집합 */
}
```

마지막 `SYSTRAYSUPPORT` 등록은 jamotong의 현재 호환 정책이며 모든 TIP에 보편적으로 필요한
카테고리라고 단정하지 않는다. 사용하는 SDK/MinGW 헤더에 심볼이 없으면 공식 GUID 바이트를
독립적으로 검증해 로컬 심볼을 선언하고, 등록 해제에서도 위와 **동일한 `_J` 이름과 값**을
사용한다.

### 4.4 설치 스크립트
- `install.bat`: 관리자 권한 확인 → 파일 unblock(Mark-of-the-Web) → `regsvr32 app.dll`(64) →
  `SysWOW64\regsvr32 app32.dll`(32). 로그오프/재부팅 후 확실히 반영.
- **DLL 파일 잠금 함정**(§10): TSF DLL은 `ctfmon` 등 여러 프로세스에 매핑돼 파일이 잠긴다.
  갱신하려면 uninstall → **로그오프/재부팅** → 덮어쓰기 → install.

---

## 5. 키 입력 처리

### 5.1 OnTestKeyDown / OnKeyDown 이중 구조 (★중요)

TSF는 키를 두 번 준다:
1. **`OnTestKeyDown`**: 보통은 "이 키 먹을 거야?"를 **예측만** 하고 `*pfEaten`에 답한다.
   문서를 바꾸지 않는 것이 기본 규칙이다.
2. **`OnKeyDown`**: 실제 처리. **단, `OnTestKeyDown`이 `*pfEaten=TRUE`로 답한 키만 호출된다.**

→ **두 함수의 "먹을지 판단" 로직이 반드시 일치**해야 한다. TestKeyDown에서 TRUE라 해놓고
OnKeyDown에서 안 먹으면 키가 사라진다(반대면 OnKeyDown이 아예 안 불림).

예외는 §13.2의 Ctrl/Alt/Win 경계다. 원래 단축키를 재합성하지 않기 위해 `OnTestKeyDown`에서
**완료까지 확인되는 동기 flush**를 하고 그 실제 키는 통과시킨다. 이것은 의도적으로 격리한
경계 처리이며, 일반 문자 예측 경로에 문서 편집을 섞으라는 뜻이 아니다.

```c
HRESULT OnTestKeyDown(..., WPARAM w, LPARAM l, BOOL *pfEaten){
    *pfEaten = FALSE;
    if (내가_합성한_입력(GetMessageExtraInfo())) return S_OK;   // 재진입 방지(§5.4)
    if (한글모드 && 자모키(w)) *pfEaten = TRUE;                  // 예측만
    if (조합중 && 백스페이스(w)) *pfEaten = TRUE;
    /* ... OnKeyDown과 동일 순서·동일 조건 ... */
    return S_OK;
}
```

### 5.2 VK → 문자 매핑
가상 키(VK) + Shift → QWERTY 문자로 바꾼 뒤(예: `'R'`→`'r'`, Shift+`'2'`→`'@'`) 그 문자를
자판 테이블로 자모에 매핑한다. `windows.h` 없이도 되게 VK 상수(0xBA 등)를 직접 써도 된다.

### 5.3 통과시켜야 하는 키
- **Ctrl/Alt/Win 조합**: 앱 단축키(Ctrl+C)다. 소비하지 말고 통과(`*pfEaten=FALSE`).
  (**Shift는 예외** — 대문자·된소리라 텍스트 입력에 필요.)
- **모디파이어/락 키**(Shift/Ctrl/Alt/Caps/Han/Hanja 등): 조합을 확정시키지 않는다.

### 5.4 우리가 합성한 입력의 재진입 방지
경계키 재전달(§9.2)로 `SendInput`을 쓰면 그 키가 다시 `OnKeyDown`으로 들어온다. `SendInput` 시
`ki.dwExtraInfo`에 **매직 값**(예: `0x4A414D4F`)을 심고, 진입부에서
`(ULONG_PTR)GetMessageExtraInfo() == 매직` 이면 즉시 `return`.

---

## 6. 한글 오토마타

IME의 "조합 로직"은 TSF와 무관한 순수 상태 기계다. TSF는 글자를 **문서에 넣는 통로**일 뿐,
**무엇을 넣을지**는 이 FSM이 결정한다.

- 상태: `EMPTY → CHO(초성) → CHO_JUNG(중성 결합=가) → CHO_JUNG_JONG(종성=간)`.
- 입력마다 `{commitChar, preeditChar}` 를 낸다:
  - `preeditChar`: 지금 조합 중인 완성형 음절(예: `가`).
  - `commitChar`: 확정되어 문서로 나갈 음절(0이면 없음).
- **도깨비불**: `옥` 조합 중 `ㅜ` 입력 → `commit=오, preedit=구`. commit이 이전 preedit과
  **다를 수 있다**(§7.2에서 중요).
- 2벌식 종성 판단, 겹받침, 도깨비불 등은 유니코드 한글 음절(0xAC00~) 조합/분해로 구현.

> FSM은 **네이티브 코드로 유닛테스트가 쉽다**(Windows 불필요). 현재
> `examples/standard-tsf-lab`의 portable `hangul2` 스위트(`make test`)는 한글 오토마타
> 123개 assertion을 검증한다.

---

## 7. 편집 세션 — 문서에 글자 넣기

### 7.1 편집은 "편집 세션" 안에서만
문서(`ITfContext`)를 수정하려면 **편집 쿠키(edit cookie)** 가 필요하고, 쿠키는
`RequestEditSession`이 실행한 `ITfEditSession::DoEditSession` 콜백에 준다. 이 API 자체가
항상 동기인 것은 아니다. `TF_ES_SYNC`는 “반드시 지금 실행해 달라”는 요청이며, 그 자리에서
실행할 수 없으면 `TF_E_SYNCHRONOUS`로 실패할 수 있다. 비동기 플래그를 쓰면 콜백은 호출이
돌아온 뒤 실행될 수 있으므로 세션 객체와 입력 데이터도 그때까지 살아 있어야 한다.

```c
typedef enum MutationState {
    MUTATION_NONE,       /* 아무것도 전달되지 않았음이 확인됨 */
    MUTATION_DONE,       /* after-session 또는 host-validated postcondition으로 확인 */
    MUTATION_UNKNOWN     /* write가 수락됐지만 최종 반영 증명은 없음 */
} MutationState;

typedef struct EditOutcome {
    HRESULT request_hr;       // RequestEditSession 자체의 반환값
    HRESULT session_hr;       // phrSession에 돌아온 DoEditSession 결과
    HRESULT inner_hr;         // 우리 문서 편집 함수의 실제 결과
    BOOL callback_ran;
    MutationState mutation;   // 최종 HRESULT와 별도로 계속 보존
} EditOutcome;

/* EditSession은 §1.2 방식의 ITfEditSession 구현이고, 이 포인터를 보관한다. */
static HRESULT STDMETHODCALLTYPE
ES_DoEditSession(ITfEditSession *iface, TfEditCookie ec)
{
    EditSession *es = CONTAINING_RECORD(iface, EditSession, iface);
    es->out->callback_ran = TRUE;
    es->out->inner_hr = ApplyRequiredTextEdit(es, ec, &es->out->mutation);
    return es->out->inner_hr;
}

static BOOL RequestSyncWrite(ITfContext *ctx, TfClientId clientId,
                             EditSession *es, EditOutcome *out)
{
    ZeroMemory(out, sizeof(*out));
    out->request_hr = out->session_hr = out->inner_hr = E_UNEXPECTED;
    out->mutation = MUTATION_NONE;
    es->out = out; /* es와 out은 아래 동기 호출이 끝날 때까지 살아 있다. */

    out->request_hr = ctx->lpVtbl->RequestEditSession(
        ctx, clientId, &es->iface,
        TF_ES_SYNC | TF_ES_READWRITE, &out->session_hr);

    BOOL ok = SUCCEEDED(out->request_hr) &&
              out->callback_ran &&
              SUCCEEDED(out->session_hr) &&
              SUCCEEDED(out->inner_hr);
    es->out = NULL; /* TF_ES_SYNC이므로 반환 뒤 늦은 callback은 없다. */
    return ok;
}
```

`request_hr == S_OK` 하나만 보고 성공이라고 하면 안 된다. 요청이 받아들여졌는지, 콜백이 실제로
실행됐는지, 세션이 무엇을 반환했는지, write가 수락됐는지, 독립 postcondition으로 반영을
증명했는지는 별도 사실이다. 특히 `InsertTextAtSelection == S_OK`는 write 수락이므로 우선
`MUTATION_UNKNOWN`이지 `MUTATION_DONE`이 아니다. 문서 접근은 반드시 콜백이 받은 `ec`와 그
세션의 lock 범위 안에서만 한다.

### 7.2 ★커밋 전용 엔진의 제품용 안전 패턴

**확정된 음절만 `InsertTextAtSelection`으로 삽입한다. 조합 중 음절은 화면에 안 그린다.**

> 아래 trial-FSM/결과 분리/성공 후 publish 코드는 **필수 hardening 목표이자 표준 lab·예제의
> 계약**이다. 2026-07-24 현재 main 제품은 결과를 끝까지 전달하고 상태를 나중에 publish하는
> migration이 완료되지 않았으므로, 아래 코드를 현재 제품의 정확한 구현 스냅샷으로 읽지 않는다.

```c
static HRESULT MoveCaretToEnd(ITfContext *ctx, TfEditCookie ec, ITfRange *range)
{
    TF_SELECTION selection;
    HRESULT hr = range->lpVtbl->Collapse(range, ec, TF_ANCHOR_END);
    if (FAILED(hr)) return hr;

    selection.range = range;
    selection.style.ase = TF_AE_NONE;
    selection.style.fInterimChar = FALSE;
    return ctx->lpVtbl->SetSelection(ctx, ec, 1, &selection);
}

static HRESULT ApplyRequiredTextEdit(EditSession *es, TfEditCookie ec,
                                     MutationState *mutation)
{
    ITfInsertAtSelection *ins = NULL;
    ITfRange *inserted = NULL;
    HRESULT hr;

    *mutation = MUTATION_NONE;
    if (es->committed_len == 0) return S_OK; /* preedit은 문서에 넣지 않는다. */

    hr = es->ctx->lpVtbl->QueryInterface(
        es->ctx, &IID_ITfInsertAtSelection, (void **)&ins);
    if (SUCCEEDED(hr) && ins == NULL) hr = E_NOINTERFACE;
    if (FAILED(hr)) return hr;

    /*
     * 호출 실패도 부분 반영을 배제하지 못하므로 dispatch 직전에 보수적으로 올린다.
     * 같은 lock 안의 즉시 read-back만으로 DONE이라 하지 않는다.
     */
    *mutation = MUTATION_UNKNOWN;
    hr = ins->lpVtbl->InsertTextAtSelection(
        ins, ec, 0, es->committed, es->committed_len, &inserted);

    if (SUCCEEDED(hr)) {
        if (inserted == NULL)
            hr = E_UNEXPECTED; /* 텍스트는 이미 반영됐을 수 있는 부분 성공 */
        else
            hr = MoveCaretToEnd(es->ctx, ec, inserted);
    }

    if (inserted != NULL) inserted->lpVtbl->Release(inserted);
    ins->lpVtbl->Release(ins);
    return hr;
}
```

문서 편집과 FSM 상태 게시도 하나의 논리 트랜잭션으로 취급한다. 다음 상태를 복사본에서 계산하고,
필수 문서 편집이 끝난 뒤에만 실제 상태로 게시한다.

```c
/*
 * write session 반환 뒤 새 READ session을 열어 durable 상태를 확인하거나,
 * 해당 호스트에서 app-visible 반영과 대응한다고 검증한 ack를 검사한다.
 * 같은 write lock 안의 즉시 range read-back만 반복해서는 안 된다.
 */
static HRESULT VerifyCommittedPostconditionAfterSession(
    CTextService *obj, ITfContext *pic,
    const WCHAR *expected, LONG expectedLen, BOOL *proven);

BOOL HandleTextKey(CTextService *obj, ITfContext *pic, UINT vk, BOOL *pfEaten)
{
    HangulState trial = obj->hangul;             /* 아직 publish하지 않은 복사본 */
    FsmResult result = Hangul_Step(&trial, vk);
    WCHAR committed[2] = { result.commitChar, L'\0' };
    EditSession *es;
    EditOutcome out;

    /* Create는 문자열 복사+heap 할당+pic AddRef, Release는 pic 해제+free를 담당한다. */
    es = EditSession_Create(pic, committed, result.commitChar != 0 ? 1 : 0);
    if (es == NULL) {
        ResetTransientComposition(obj);         /* 낡은 preedit/오버레이를 남기지 않음 */
        *pfEaten = FALSE;                         /* 문서를 전혀 건드리지 않음 */
        return FALSE;
    }

    LONG committedLen = result.commitChar != 0 ? 1 : 0;
    BOOL sessionOk = RequestSyncWrite(pic, obj->clientId, es, &out);
    es->iface.lpVtbl->Release(&es->iface);
    if (out.mutation == MUTATION_UNKNOWN) {
        BOOL proven = FALSE;
        HRESULT verifyHr = VerifyCommittedPostconditionAfterSession(
            obj, pic, committed, committedLen, &proven);
        if (verifyHr == S_OK && proven)
            out.mutation = MUTATION_DONE;
        else
            TraceUnprovenPostcondition(verifyHr); /* 값/문서 내용은 기록하지 않음 */
    }

    BOOL noWriteNeeded = (committedLen == 0);
    if (sessionOk && (noWriteNeeded || out.mutation == MUTATION_DONE)) {
        obj->hangul = trial;                      /* 무편집 단계 또는 증명된 반영 뒤 publish */
        *pfEaten = TRUE;
        return TRUE;
    }

    ResetTransientComposition(obj);
    /*
     * NONE만 원래 키를 안전하게 통과시킬 수 있다.
     * UNKNOWN 소비는 중복 위험을 택하지 않는 보수적 제품 정책이지 성공 판정이 아니다.
     */
    *pfEaten = (out.mutation == MUTATION_NONE) ? FALSE : TRUE;
    TraceEditFailure(&out);                       /* 내용이 아닌 HRESULT/mutation만 */
    return FALSE;
}
```

이 코드는 “실패하면 rollback됐다”고 가정하지 않는다. 편집 세션은 데이터베이스 트랜잭션이
아니므로 `SetText`나 삽입 write가 수락된 뒤 후속 단계가 실패해도 앞선 문서 변경은 남을 수
있다. `MUTATION_UNKNOWN`에서는 FSM을 publish하지 않는다. 원래 키도 다시 보내지 않고 소비하는
정책은 **중복 가능성을 입력 유실/복구 가능성보다 더 위험하게 보는, 해당 지원 경로의 명시적
risk policy**다. 이를 반영 성공이라고 기록하면 안 되며, 진단·사용자 복구 또는 그 호스트를
미지원 처리하는 경로가 필요하다.

- 조합 중 음절은 **다음 음절이 시작되거나 경계키(스페이스/엔터)에서** commit이 나올 때 나타난다
  = 한 박자 늦게 보인다. 이게 커밋 전용의 유일한 대가(미리보기 없음).
- **역순 버그 주의**: 확정 후 커서를 삽입 텍스트 **끝으로** 옮기지 않으면(`MoveCaretToEnd`),
  다음 삽입이 앞에 쌓여 `가나다→다나가`가 된다. `InsertTextAtSelection`이 준 range를 `Collapse
  (TF_ANCHOR_END)` 후 `SetSelection`.

### 7.3 (참고) 조합 미리보기를 하려면 — 왜 안 했나
호스트가 필요한 text-store 계약을 제공할 때 쓸 수 있는 두 방법:
- **TSF 조합**: `ITfContextComposition::StartComposition`으로 조합 시작 → `ITfRange::SetText`로
  갱신 → 디스플레이 속성(밑줄) 부여. 시험한 일부 호환 호스트에서는 세션 직후 종료됐다(§8.1).
- **제자리 교체**: 조합 음절을 일반 텍스트로 넣고 바뀌면 지우고 다시 넣기(`ShiftStart`+`SetText`).
  시험한 일부 제한적 text store에서는 뒤쪽 range 교체가 누적(`ㄱ가간...`)으로 깨졌다(§8.2).

→ 지원 행렬의 문제 호스트에서 둘 다 깨져서 제품은 **커밋 전용**으로 귀결했다. 자세한 건 다음 장.

---

## 8. ★핵심 교훈: CUAS의 벽과 "커밋 전용"

이 장이 이 문서의 존재 이유다. jamotong이 **20번 넘는 실기 왕복** 끝에 배운 것.

### 8.1 일부 호환 호스트에서 TSF 조합은 세션 직후 죽는다
- metadata 시험 전 단계에서 우리 조합 설정은 교과서적 기본 경로를 따랐다: 디스플레이 속성
  `TF_ATTR_INPUT`, 카테고리 등록,
  텍스트 삽입 후 조합 시작, `SetSelection` 유무, 동기/비동기, `ITfThreadMgrEventSink`/
  `ITfTextEditSink` 부착을 각각 시험했으나 그 AkelPad 재현을 고치지 못했다.
- 로그로 확인: `StartComposition`은 성공(hr=0)하고 조합 텍스트도 들어가는데, **편집 세션이 끝난
  직후** 세션 밖 추가 편집과 `OnCompositionTerminated`가 조합을 종료시킨다. 로그만으로 그
  편집의 주체가 앱인지 텍스트 스토어인지 CUAS인지 특정할 수는 없다. AkelPad의 transitory
  컨텍스트와 IMM32 처리 코드는 호환 경로 가설을 강하게 만들지만 증명은 아니다(§12.7).
- **AkelPad x64 재현(2026-07-23)**: 한 키의 우리 편집 세션이 완전히 성공하고 닫힌 다음,
  `OnEndEdit(selection_changed=0)` → `OnCompositionTerminated`가 같은 tick에 매번 반복됐다.
  따라서 비트수·COM ABI·즉시 API 실패가 아니라 **세션 이후 호스트 호환 경계**가 조사 대상이다.
  `TF_SS_TRANSITORY`와 AkelEdit의 `WM_IME_*`/`ImmGetCompositionStringW` 구현은 CUAS/IMM 경유
  추론을 뒷받침한다.
- **시작 순서 가설은 AkelPad를 고치지 못했다(2026-07-24).** Mozilla의 한글 IME 실기 기록과
  같은 선삽입 순서, `TF_AE_NONE` 선택 스타일, 명시적 `SetSelection` 생략은 모두 Control과
  같은 결과였고 네 프로필 모두 메모장에서는 정상 동작했다. 후속 metadata 결과와 남은 질문은
  §12.7.7~§12.7.8에 시간순으로 보존한다.
- **API 계약과 현장 관찰을 분리하라.** 공식 `StartComposition` 계약에서
  `ITfCompositionSink *pSink`는 선택 사항이며 NULL을 허용한다. 그러나 이 프로젝트가 시험한
  호스트/경로 중에는 NULL에서 `E_INVALIDARG`가 난 경우가 있었다. 이식성과 외부 종료 관측을
  위해 실제 sink를 넘기는 것이 안전하지만 “모든 Windows에서 필수”라고 쓰면 안 된다.
- `StartComposition`이 `S_OK`여도 `*ppComposition == NULL`일 수 있다. 이는 context owner가
  조합을 받아들이지 않은 정상적인 거절 표현이다. NULL을 역참조하거나 로컬 FSM에 “조합 시작”을
  게시하지 않는다.

```c
static HRESULT TryStartComposition(ITfContextComposition *cc,
                                   TfEditCookie ec, ITfRange *range,
                                   ITfCompositionSink *sink,
                                   ITfComposition **out)
{
    HRESULT hr;

    if (cc == NULL || range == NULL || out == NULL) return E_INVALIDARG;
    *out = NULL;
    hr = cc->lpVtbl->StartComposition(cc, ec, range, sink, out);
    if (FAILED(hr)) return hr; /* 시험 호스트의 NULL-sink E_INVALIDARG도 여기 */
    if (*out == NULL) return S_FALSE; /* wrapper 의미: owner가 거절, API 오류 아님 */
    return S_OK;
}

/* 외부 종료를 관측해야 하는 실제 IME에서는 live sink를 넘긴다. */
hr = TryStartComposition(cc, ec, range, &obj->compositionSink, &composition);
if (hr == S_OK) {
    obj->composition = composition; /* 반환된 reference의 소유권을 서비스 상태로 이전 */
    composition = NULL;
    composition_started = TRUE;     /* 이 시점 뒤에만 로컬 상태 publish */
} else if (hr == S_FALSE) {
    /* text를 먼저 넣었다면 이미 부분 편집이다. 자동 rollback됐다고 가정하지 않는다. */
    composition_started = FALSE;
} else if (FAILED(hr)) {
    /* 필수/선택 단계 정책에 따라 처리하되 composition을 publish하지 않는다. */
}
```

저장한 `obj->composition`은 명시적 확정·리셋 또는 `OnCompositionTerminated` 처리에서 정확히 한 번
`Release`하고 NULL로 만든다. sink 객체 자체도 composition보다 오래 살아야 한다.

### 8.2 일부 호환 text store는 뒤쪽 "range 편집"을 지원하지 않았다
- 조합 없이 "제자리 교체"(넣은 `ㄱ`을 지우고 `가`로 바꾸기)로 미리보기를 시도 → **네이티브
  TSF 비교 호스트에서는 됐지만 시험한 제한적 EDIT/호환 경로는 교체가 안 돼
  `ㄱ가간가나낟...`처럼 누적했다.**
- 이는 그 text store에서 `ITfRange::ShiftStart`로 뒤로 확장한 뒤 `SetText`로 덮는 경로가
  제품의 신뢰성 기준을 만족하지 못했다는 관찰이다. CUAS라는 이름이 붙은 모든 앱이 같은 계약을
  갖는다거나 커서 삽입만 가능하다는 Windows API 보장은 아니다.
- **∴ 이 프로젝트의 지원 행렬에서는 TSF range 교체를 범용 인라인 미리보기 기반으로 삼을 수
  없었다.** 지원 여부를 별도로 검증한 호스트에는 네이티브 경로를 둘 수 있다.

### 8.3 IMM32 IME로 우회? — 시험한 Windows 11 환경에서는 막혔다
- "그럼 정식 레거시(IMM32) IME로 만들자"는 자연스런 우회. `.ime`(DLL) 만들어 `ImeInquire`·
  `ImeProcessKey`·`ImeToAsciiEx` 구현까지 했다. 그러나:
  - 시험 환경의 `ImmInstallIME`는 NULL을 반환하고 `GetLastError()==0`인 no-op처럼 보였다.
  - 레지스트리로 직접 등록하면 설정 목록에 **뜨긴 하나 "음영(비활성)"** → Win11이 서드파티
    IMM32 IME를 활성화하지 않았다.
  - 부수 발견: `ImmInstallIME`가 IME의 **버전 리소스(VERSIONINFO)** 를 읽는다 — 없으면
    `1813 ERROR_RESOURCE_TYPE_NOT_FOUND`가 관측됐다.
- 공식 `ImmInstallIME` 문서는 설치 API를 계속 설명하며 “모든 Windows 11이 서드파티 IMM32를
  정책적으로 거부한다”고 보장하지 않는다. 따라서 위 결과는 **이 프로젝트의 시험 환경과 배포
  경로에 대한 관찰**이다. 다만 지원 목표 환경에서 재현됐으므로 제품 우회 경로로는 채택하지 않았다.

### 8.4 제품 결정: 확정 상태만 유지하고 시험한 호스트별로 삽입 경로를 라우팅
당시 개발 단계의 관측 행렬은 다음을 보였다.

- 실패한 호환 호스트에서는 composition 수명이 안정적이지 않았다.
- 시험한 제한적 text store에서는 뒤쪽 range 교체가 안정적이지 않았다.
- 실험용 IMM32 패키지는 시험한 Windows 11 환경에서 활성화되지 않았다.
- 커서 삽입은 당시 시험한 앱들을 포괄했다.

그래서 제품은 preedit을 IME 소유 상태에만 두고 확정 텍스트만 문서에 넣는 **커밋 전용**을
택했다. ~~커서 삽입은 보편적이므로 하나의 TSF 경로가 모든 앱에서 동작한다.~~ 이 강한 결론은
나중에 `S_OK`를 반환하고도 삽입을 화면에 반영하지 않은 시험 대상 AkelEdit 계열 컨트롤로
기각됐다(§13). 현재 구현은 커밋 전용 상태를 유지하되, 확정 텍스트는 호스트별 정책으로
라우팅한다.

대가는 조합 중 음절의 인라인 미리보기(밑줄)가 없다는 것이다. 인라인 미리보기가 필요하면 각
지원 호스트에서 TSF composition을 검증하고, IME 소유 오버레이나 커밋 전용 fallback을 함께
둔다. §13의 클래스명·capability 라우팅은 **실기 검증된 제품 heuristic/allowlist**이지,
일반적인 네이티브 TSF·CUAS·터미널 판별기가 아니다. “composition을 시작해 죽는지 본다” 같은
파괴적 검사는 첫 키를 망가뜨릴 수 있으므로 jamotong은 capability probe로 사용하지 않았다.

> **v0.12 갱신(§13):** `InsertTextAtSelection`은 시험한 비교·터미널 경로를 포괄했지만, 한
> EDIT 계열 호스트는 `hr=0`을 반환하고도 글자를 표시하지 않았다. 그래서 제품은 명시적으로
> 검증한 라우팅 정책에 따라 TSF 삽입·`EM_REPLACESEL`·실제 키 경로를 고른다. 클래스 이름만으로
> 시험하지 않은 앱의 지원 여부를 추론하지 않는다.

### 8.5 시행착오 연대기 (요약)
1. TSF 조합 인라인 → 시험한 호환 앱·터미널에서 매 키 종료. 처음에는 “CUAS 전체의 한계”로
   과대 일반화했으나, 뒤의 A/B에서 호스트별 관찰로 범위를 좁혔다.
2. `SendInput` 유니코드 append 폴백 → 터미널은 됐으나 EDIT 컨트롤은 합성입력이 조합처럼 덮어써짐.
3. IMM32 IME 정식 구현 → 지원 목표 Windows 11 시험에서 설치/활성화 실패(§8.3).
4. NULL-sink 시험 경로에서 `E_INVALIDARG`가 나 조합 없이 삽입만 남았고, 그 우연으로 커밋 전용
   가능성을 발견했다. NULL 금지는 API 보장이 아니라 그 현장 결과다.
5. 제자리 교체 미리보기 → 시험한 제한적 text store에서 range 편집이 누적(§8.2).
6. **커밋 전용 채택** → 이후 앱 클래스별 주입까지 보강(§13).
7. Metadata v1 → 수기 GUID 오기와 부분 편집 뒤 원래 키 중복 통과 때문에 가설 적용 전에 무효.
8. 정정 R2 → AkelPad Reading-only에서 composition 유지; LANGID가 든 변형은 종료.
9. R3 → 모든 속성 호출·read-back은 성공했지만 Reading 양성 대조군까지 종료. R2에 없던
   read-back·동기 trace가 섞여 순서/LANGID/반복 인과는 미판정.
10. R4 → 같은 바이너리에서 Set-only·trace-only·read-back을 분리하고 앞뒤 baseline으로
    환경 drift를 검사하도록 설계(§12.7.8).

---

## 9. 부가 기능

### 9.1 한자 변환 (커밋 전용 모델)
- 조합 중 음절은 **문서에 없으므로**(커밋 전용), 한자는 **교체가 아니라 삽입**:
  한자키 → 현재 FSM 음절로 사전 조회 → 후보창(자체 팝업 윈도) → 선택 시 **선택 한자를 삽입**
  (해당 앱에서 검증한 §13의 확정 삽입 경로) + FSM 리셋.
- 후보창은 포커스를 뺏지 않는 팝업으로 만들고, 키는 IME가 라우팅(후보창 뜨면 모든 키를 소비→
  후보창 핸들러로 전달).
- 커서 앞을 `ITfRange::ShiftStart`로 읽어 교체하는 고전 단어 변환은 시험상 TSF range 편집을
  온전히 지원한 호스트에 한정됐다. 검증된 EDIT/RichEdit에는 §13.4의 선택 메시지 경로가 있고,
  더 넓게 통한 UX는 **먼저 선택하고 한자키**였다(§12.5).

### 9.2 비자모 경계키 (스페이스/엔터/방향키)
조합 중 비자모 키가 오면 먼저 현재 음절을 확정한다. 그 다음 전달 경로는 하나가 아니다.

- 스페이스: 가능하면 **음절+공백을 한 번의 텍스트 삽입**으로 보낸다(§12.3).
- 검증된 EDIT/RichEdit 경계키: 해당 창의 큐에 `PostMessage`하고 반환값을 검사한다(§13.3).
- 자체 렌더러·터미널: 실제 시스템 키가 필요하면 `SendInput`과 §5.4 매직 표식을 쓴다.
- Ctrl/Alt/Win 단축키: 합성하지 않는다. §13.2처럼 동기 flush 뒤 원래 물리 키를 통과시킨다.

앱 종류와 키 의미를 확인하지 않은 채 모든 경계키를 `SendInput`으로 재전달하면 CUAS 결과 문자와
큐 순서가 경합하거나, 단축키가 다음 사용자 입력 뒤에 도착할 수 있다.

### 9.3 설정창 열기 (ITfFnConfigure)
- 시험한 Windows 11 모던 설정 화면은 이 서드파티 TIP에 "옵션" 버튼을 제공하지 않았다.
  `ITfFnConfigure`는 클래식 경로에서는 유효하지만, 모던 UI 노출을 보장하는 계약으로 보지 않는다.
- 실용적 대안: **단축키**(예: Ctrl+Alt+K)로 설정창을 띄우거나, **별도 설정 exe**를 실행해
  공용 설정 파일(`%APPDATA%\App\config.ini`)을 편집·저장하고 IME가 다음 활성화 때 로드.

### 9.4 언어바 아이콘 (ITfLangBarItemButton) — 선택. 최신 Windows에선 잘 안 보임.

---

## 10. 함정 모음 (Gotchas)

- **vtbl 함수 순서/호출규약**: 하나만 틀려도 즉시 크래시. 인터페이스 정의 순서 그대로.
- **문서에 없는 COM 인자를 만들지 마라**: C에서 직접 선언한
  `ITfDisplayAttributeProvider::GetDisplayAttributeInfo`는 `This`, `REFGUID`,
  `ITfDisplayAttributeInfo **`뿐이다. 과거 표준 lab의 추가 `BSTR *`와 main 제품에 남아 있던
  추가 `TfGuidAtom *`는 둘 다 이 메서드의 인자가 아니다. x86은 스택이 어긋나고 x64도 ABI
  계약 위반이다. 설명은 별도 `ITfDisplayAttributeInfo::GetDescription`에서 얻고, 표시 속성
  atom은 `ITfCategoryMgr::RegisterGUID` 결과로 관리한다. 직접 선언한 IID도 이름만 믿지 말고
  공식 Provider IID `{FEE47777-163C-4769-996A-6E9C50AD8F54}`와 바이트 단위로 비교한다.
- **`StartComposition`의 sink는 공식 계약상 선택 사항**: NULL도 허용된다. 다만 시험 호스트에서
  NULL이 `E_INVALIDARG`였고 외부 종료 관측에도 sink가 필요했으므로 실제 sink를 권장한다.
  `S_OK + composition==NULL`은 owner 거절이므로 별도로 처리한다(§8.1).
- **역순 입력**: 확정 후 커서를 삽입 끝으로 옮겨라(`MoveCaretToEnd`). 안 하면 `가나→나가`.
- **OnTestKeyDown ↔ OnKeyDown 조건 불일치**: 키 유실/미호출.
- **합성입력 재진입**: `SendInput`엔 `dwExtraInfo` 매직 심고 진입부에서 걸러라.
- **DLL 파일 잠금**: TSF DLL은 여러 프로세스에 매핑됨. 갱신은 uninstall→로그오프/재부팅→덮기→install.
  로그 포맷 바꿨는데 옛 포맷 찍히면 = 옛 DLL 로드 중(배포 실패 신호).
- **32/64 둘 다**: 앱 비트수와 DLL 비트수가 일치해야 로드됨. 각각 등록.
- **`RegisterProfile` vs `AddLanguageProfile`**: Win11 트레이 아이콘은 전자라야 뜬다.
- **아이콘 인덱스 음수 규약**: `-리소스ID`. `-1`은 특수값이라 피하라.
- **레지스트리 리다이렉션**: 32비트 `regsvr32`는 논리 `HKCR\CLSID`의 32비트 view를 쓴다.
  물리 위치를 검사할 때는 `HKLM\Software\Classes\Wow6432Node\CLSID`를 본다(§4.1).
- **유니코드/로케일**: 소스·문자열 전부 UTF-16(`wchar_t`, `-W` API). `.rc`에 한글 넣으려면
  windres 코드페이지 주의(안전하게 ASCII 또는 리소스 문자열 회피).
- **`RequestEditSession`은 항상 동기가 아니다**: `TF_ES_SYNC`가 동기 실행을 요구하며 불가능하면
  `TF_E_SYNCHRONOUS`일 수 있다. `request_hr`·`session_hr`·실제 편집 HRESULT·callback 실행을
  분리하고, 문서 수정은 반드시 콜백의 `ec` 안에서만 한다(§7.1).
- **`S_OK`는 composition 생존 보증이 아니다**: 세션 중 또는 직후
  `OnCompositionTerminated`가 올 수 있다. 요청/세션 HRESULT만 보지 말고 트랜잭션 세대,
  종료 epoch, 세션 반환 후 composition 존재 여부를 함께 확인하라.
- **wide-scanf 변환 지정자**: C 표준에서 `swscanf`의 `%[`/`%c`/`%s`는 `l` 없이는 **narrow(char) 대상**이다.
  MSVCRT만 MS 특유로 wide 취급해 Windows에선 우연히 돌아간다 — `%l[`/`%lc`/`%ls`로 명시하라
  (양쪽 CRT에서 동일 동작·이식 가능).
- **윈도 클래스 소유권**: 클래스는 반드시 **DLL의 hInstance**로 등록하고(EXE 인스턴스로 등록하면
  WndProc과 소유자가 어긋남), **동적 언로드 시 `UnregisterClassW`** 하라(MSDN: DLL 클래스는 자동
  해제 안 됨). 안 하면 재로드 후 낡은 WndProc을 가리키는 클래스로 크래시.
- **`TF_IAS_NOQUERY`는 ppRange를 안 채울 수 있다**: 교체가 필요하면 `TF_IAS_QUERYONLY`로 선택
  range를 얻어 `ShiftStart`+`SetText` 하라(NULL 역참조 방지).
- **`VT_EMPTY`의 뜻은 API별로 다르다.** 공통 규칙은 `GetValue` 전에 `VariantInit`, 호출 뒤
  모든 경로에서 `VariantClear`하는 것뿐이다.
  - `ITfCompartment::GetValue`의 `S_FALSE + VT_EMPTY`는 **그 compartment에 값이 없음**이다.
    Boolean FALSE로 바꾸지 말고 명시적 `VT_I4`가 올 때까지 알려진 로컬 fallback을 유지한다.
  ```c
  VARIANT v;
  VariantInit(&v);
  HRESULT hr = compartment->lpVtbl->GetValue(compartment, &v);
  if (hr == S_FALSE || (SUCCEEDED(hr) && V_VT(&v) == VT_EMPTY))
      enabled = localFallback; /* 값 없음, FALSE가 아님 */
  else if (SUCCEEDED(hr) && V_VT(&v) == VT_I4)
      enabled = (V_I4(&v) != 0);
  VariantClear(&v);
  ```

  - `ITfReadOnlyProperty::GetValue`의 `S_FALSE + VT_EMPTY`는 다른 계약이다. 질의 range가
    property에 덮이지 않았거나, 여러 property 값에 걸쳤거나, 일부만 덮였다는 뜻이다. 따라서
    이것을 “property가 전역 미설정”이나 “직전 `SetValue`가 확실히 실패”로 단정하지 않는다.
    정확한 read-back 성공은 `hr == S_OK`이고 기대 VARTYPE·값까지 일치할 때뿐이다.
  ```c
  VARIANT actual;
  VariantInit(&actual);
  HRESULT readHr = property->lpVtbl->GetValue(property, ec, range, &actual);
  if (readHr == S_FALSE && V_VT(&actual) == VT_EMPTY) {
      /* 값 없음/여러 값/부분 coverage 중 하나: exact read-back 아님 */
  } else if (readHr == S_OK) {
      /* 여기서만 기대 VARTYPE과 값을 비교한다. */
  }
  VariantClear(&actual);
  ```

- **락 범위**: `OnTestKeyDown`도 config/레이아웃을 읽는다면 **똑같이 락**을 잡아라. 설정 스레드가
  레이아웃 리소스를 해제하는 순간 키가 오면 UAF다.

---

## 11. 최소 체크리스트

커밋 전용 한글 TSF IME 최소 부품:

> 이 목록은 구현 완료표가 아니라 **release gate**다. 특히 main 제품에는 아직
> `CommitText`/`OutputResult`의 mutation outcome 전달, trial FSM의 성공 후 publish,
> `EM_REPLACESEL`의 `MUTATION_UNKNOWN` 처리가 남아 있다.

- [ ] COM: `IClassFactory`, `DllGetClassObject/CanUnloadNow/RegisterServer/UnregisterServer` + `.def`
- [ ] `ITfTextInputProcessor` (Activate에서 `AdviseKeyEventSink`)
- [ ] `ITfKeyEventSink` (OnTestKeyDown/OnKeyDown 조건 일치, OnSetFocus에서 FSM 리셋)
- [ ] 한글 FSM (2벌식 조합 → `{commit, preedit}`)
- [ ] 편집 세션(`ITfEditSession`)의 request/session/inner 결과와 `NONE`/`DONE`/`UNKNOWN` 분리
- [ ] 독립 postcondition으로 `DONE`일 때만 FSM publish; `NONE`만 원래 키 통과,
      `UNKNOWN` 소비는 경로별로 문서화한 risk policy
- [ ] 앱 클래스별 확정 삽입(`InsertTextAtSelection`/EDIT 메시지/실제 키) + 커서 끝 이동
- [ ] 경계키 분기(공백 단일 삽입, EDIT 큐, 터미널 `SendInput`+표식, 단축키 무주입)
- [ ] 등록: COM CLSID + `RegisterProfile` + `RegisterCategory(TIP_KEYBOARD)` + install/uninstall.bat
- [ ] 32/64 두 벌 빌드·등록

**있으면 좋은 것**: 한자 후보창, 설정(단축키/별도 exe), 트레이 브랜딩 아이콘.
**커밋 전용이면 필요 없는 것**: `ITfCompositionSink`, `ITfDisplayAttributeProvider`, 조합/밑줄 로직.

> 언어바 모드 아이콘을 넣는다면 별도 release gate가 필요하다: `ITfLangBarItem`/
> `ITfLangBarItemButton`의 SDK 상속 vtable 순서, 별도 `ITfSource`, canonical button/item
> prefix로 `AddItem`,
> Advise/Unadvise cookie·소유 sink의 1회 Release, 정확한 `TF_LBI_ICON=0x1`/
> `TF_LBI_STYLE_SHOWNINTRAY=0x2`, `GetIcon`의 `S_OK + NULL` 계약을 검사한다.

---

## 12. 커밋 전용 '이후'의 실전 교훈

커밋 전용(§8)으로 이야기가 끝나지 않았다. 미리보기·한자·트레이 아이콘을 붙이는 과정에서
시험한 호환 호스트에서 실패 세 가지가 더 드러났다. 각각 "문제 → 증거 → 제품 수정"으로
정리한다. 아래 내용은 이름을 밝힌 지원 행렬(v0.9~v0.11)의 실기 로그로 검증했으며, 모든
CUAS 경로에 같은 원인과 타이밍이 있다고 일반화하지 않는다.

### 12.1 조합 미리보기는 '문서 밖'에서 — 플로팅 오버레이
- **문제**: 커밋 전용의 대가로 조합 중 음절이 화면에 안 보인다.
- **해법**: 문서를 건드리지 말고, **IME 소유의 반투명 칩 창을 캐럿 위치에 띄워 직접 그린다**
  (고전 IMM32 "기본 조합창"과 같은 폴백 패턴). 이 구조는 미리보기 경로에서 문서 조합의
  수명 문제를 제거하지만, 캐럿 탐색과 실제 텍스트 커밋은 여전히 호스트별 검증이 필요하다.
- **구현 요점**:
  - 스타일: `WS_POPUP` + `WS_EX_LAYERED|NOACTIVATE|TOPMOST|TOOLWINDOW|TRANSPARENT`(클릭 통과).
  - 반투명은 **균일 알파**(`SetLayeredWindowAttributes`) + 일반 WM_PAINT로. 퍼픽셀 알파
    (`UpdateLayeredWindow`)는 GDI 텍스트가 알파를 안 채워 글자가 사라진다.
  - 캐럿 좌표 폴백 체인: 커밋 삽입과 **같은 편집 세션에서** `GetActiveView`→`GetTextExt` →
    실패 시 `GetGUIThreadInfo`(시스템 캐럿; 시험한 옛 EDIT·터미널 경로가 제공) → 둘 다 실패면
    미리보기만 생략. 어느 단계가 동작하는지는 지원 호스트마다 확인한다.
  - 입력 스레드에서 lazy 생성, 포커스 이동·Deactivate에서 숨김/파괴.

### 12.2 ★일부 호환 호스트의 GetTextExt는 "성공하되 낡은 좌표"를 줬다
- **문제**: 칩이 직전 확정 글자 위에 겹치거나, 확정 후 **한 박자 늦게** 다음 칸으로 따라온다.
- **관찰된 원인 모형**: 시험한 호환 앱은 커밋 반영이 비동기처럼 보였다. 같은 세션 안에서 삽입
  '직후' `GetTextExt`를 불러도
  **hr=S_OK로 성공하면서 삽입 반영 전의 옛 좌표**를 준다(실패가 아니라서 폴백으로도 못 잡는다!).
  시험한 비교 컨트롤은 좌표를 즉시 전진시켰다. 이 차이만으로 어느 쪽을 보편적인
  “네이티브 TSF”로 분류하거나 다른 호스트도 같은 타이밍이라고 단정할 수는 없다.
- **최종 해법 — 낡음 탐지(누적 보정)**:
  1. 칩을 그릴 때마다 **원시(raw) rect를 기억**한다.
  2. "이번 이벤트에 **커밋이 있었는데** rect가 직전 원시값과 **동일**" = 낡은 좌표로 판정 →
     전각 1글자 폭(≈줄높이)을 **누적** 보정해 그린다.
  3. rect가 실제로 움직이면 누적을 리셋한다.
  - 누적이라 빠른 타이핑(여러 키 동안 rect 정체)도 맞고, 시험한 비교 컨트롤에서는 rect가
    즉시 움직여 보정이 발동하지 않았다.
  - 함정: 비교 기준은 반드시 **원시 rect**로 저장하라(보정된 값을 저장하면 다음 비교가 깨진다).

### 12.3 ★어절 마지막 음절 증발 사건 — 합성 경계키와의 경합
- **문제**: 영향을 받은 호환 호스트 실행에서 `대한민국`의 "국", `가나다라`의 "라"처럼
  **어절 마지막 음절이 간헐적으로 소실**됐다. 어느 날은 국, 어느 날은 라 — 내용과 무관하고
  타이밍에 의존했다.
- **진단**: 로그상 모든 `InsertTextAtSelection`은 `S_OK`였다. 이는 호출 수락을 뜻할 뿐 화면
  반영을 증명하지 않는다. 소실은 항상 "flush(마지막 음절 삽입) + `SendInput` 경계키 재전달"과
  함께 관측됐다.
- **강한 현장 추론**: 시스템 입력 큐의 합성 경계키가 앱 안에서 호환 경로의 결과 문자 전달과
  경합했다. 상관관계와 아래 단일 채널 수정은 이 모형을 지지하지만, 로그는 내부 scheduler를
  보여 주지 않았다.
- **최종 해법**: **스페이스는 문자다.** 조합 중 스페이스가 오면 재전달하지 말고
  **"음절+공백"을 한 번의 삽입으로** 처리하라(전달 채널이 하나가 되어 경합이 소멸).
  엔터·방향키는 제어키라 재전달을 유지한다(터미널·자동 들여쓰기 등 앱 고유 처리 필요).
- **이 지원 행렬의 교훈**: 순서가 중요할 때 한 키 이벤트에서 편집 세션 삽입과 `SendInput`을
  섞지 않는다. 성공 HRESULT만으로 두 전달 경로의 앱 가시 순서를 보장할 수 없다.

### 12.4 트레이 입력 표시기의 모드 아이콘 (MS IME의 한/A 같은 것)

트레이에 떠서 IME의 현재 상태를 보여주고 실시간으로 갱신되는 작은 아이콘이다. §4.2의 프로파일
브랜딩 아이콘과는 **다르다** — 이건 TIP이 활성인 동안 추가하는 런타임 *언어바 아이템*이다.
문서만 보면 겁나지만, 실제로는 다섯 조각이다.

먼저 SDK의 `<ctfutb.h>`를 우선 사용한다. 사용 중인 MinGW에 그 헤더/인터페이스가 없을 때만
공식 C ABI와 IID를 바이트 단위로 고정해 다음처럼 선언한다. 타입 없이 포인터만 임의 선언하거나
Learn 페이지의 요약 상속 문장만 보고 vtable을 추측하면 안 된다. 현재
[Microsoft Windows SDK IDL](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/ctfutb.idl)은
`ITfLangBarItemButton : ITfLangBarItem`을 선언하므로 C vtable은 `IUnknown` 3개,
item 메서드 4개, button 메서드 5개 순서다. Learn의 “IUnknown 상속” 문장은 SDK IDL과
충돌하므로 ABI oracle로 쓰지 않는다.

```c
#include <windows.h>
#include <msctf.h>
#include <oleauto.h>
#include <ocidl.h>

#if defined(__has_include)
#  if __has_include(<ctfutb.h>)
#    include <ctfutb.h>
#    define HAVE_CTFUTB_H 1
#  endif
#endif

#ifdef HAVE_CTFUTB_H
#  define IID_LBI_BUTTON IID_ITfLangBarItemButton
#else
typedef struct ITfMenu ITfMenu;
typedef enum J_TfLBIClick {
    J_TF_LBI_CLK_RIGHT = 1,
    J_TF_LBI_CLK_LEFT = 2
} J_TfLBIClick;

typedef struct ITfLangBarItemButton ITfLangBarItemButton;
typedef struct ITfLangBarItemButtonVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(
        ITfLangBarItemButton *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ITfLangBarItemButton *);
    ULONG (STDMETHODCALLTYPE *Release)(ITfLangBarItemButton *);
    HRESULT (STDMETHODCALLTYPE *GetInfo)(
        ITfLangBarItemButton *, TF_LANGBARITEMINFO *);
    HRESULT (STDMETHODCALLTYPE *GetStatus)(
        ITfLangBarItemButton *, DWORD *);
    HRESULT (STDMETHODCALLTYPE *Show)(
        ITfLangBarItemButton *, BOOL);
    HRESULT (STDMETHODCALLTYPE *GetTooltipString)(
        ITfLangBarItemButton *, BSTR *);
    HRESULT (STDMETHODCALLTYPE *OnClick)(
        ITfLangBarItemButton *, J_TfLBIClick, POINT, const RECT *);
    HRESULT (STDMETHODCALLTYPE *InitMenu)(
        ITfLangBarItemButton *, ITfMenu *);
    HRESULT (STDMETHODCALLTYPE *OnMenuSelect)(
        ITfLangBarItemButton *, UINT);
    HRESULT (STDMETHODCALLTYPE *GetIcon)(
        ITfLangBarItemButton *, HICON *);
    HRESULT (STDMETHODCALLTYPE *GetText)(
        ITfLangBarItemButton *, BSTR *);
} ITfLangBarItemButtonVtbl;
struct ITfLangBarItemButton {
    const ITfLangBarItemButtonVtbl *lpVtbl;
};

/* {28C7F1D0-DE25-11D2-AFDD-00105A2799B5}, SDK/metadata와 byte-check. */
static const IID IID_ITfLangBarItemButton_J = {
    0x28c7f1d0, 0xde25, 0x11d2,
    { 0xaf, 0xdd, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 }
};
#  define IID_LBI_BUTTON IID_ITfLangBarItemButton_J
#endif
```

**레시피 — 아이콘이 실제로 뜨게 하는 것:**

1. **상속된 button과 별도 `ITfSource`를 한 객체에 구현** —
   `ITfLangBarItemButton`은 SDK에서 `ITfLangBarItem`을 상속한다. button을 첫 필드로 두면
   그 vtable prefix가 canonical `ITfLangBarItem`이고 `AddItem`에도 같은 주소를 넘길 수 있다.
   `ITfSource`만 별도 인터페이스 주소다. 모든 wrapper는 같은 객체와 refcount를 공유한다.
   ```c
   typedef struct ModeItem {
       ITfLangBarItemButton button; /* 첫 필드: ITfLangBarItem prefix 포함 */
       ITfSource source;
       LONG refs;
       BOOL active;
       ITfLangBarItemSink *sink; /* Advise에서 QI/AddRef해 소유 */
       DWORD sinkCookie;
   } ModeItem;

   static HRESULT ModeItem_QI(ModeItem *obj, REFIID riid, void **ppv)
   {
       if (ppv == NULL) return E_POINTER;
       *ppv = NULL;
       if (IsEqualIID(riid, &IID_IUnknown) ||
           IsEqualIID(riid, &IID_ITfLangBarItem) ||
           IsEqualIID(riid, &IID_LBI_BUTTON))
           *ppv = &obj->button;     /* 상속된 하나의 canonical 주소 */
       else if (IsEqualIID(riid, &IID_ITfSource))
           *ppv = &obj->source;     /* sink 연결용 세 번째 interface */
       else
           return E_NOINTERFACE;
       obj->button.lpVtbl->AddRef(&obj->button);
       return S_OK;
   }
   /* 각 vtable의 QI wrapper는 자기 This에서 ModeItem을 복원해 위 helper를 부른다. */

   static HRESULT ModeItem_AdviseSink(
       ModeItem *obj, REFIID riid, IUnknown *unknown, DWORD *cookie)
   {
       ITfLangBarItemSink *sink = NULL;
       HRESULT hr;

       if (unknown == NULL || cookie == NULL) return E_INVALIDARG;
       *cookie = TF_INVALID_COOKIE;
       if (!IsEqualIID(riid, &IID_ITfLangBarItemSink))
           return CONNECT_E_CANNOTCONNECT;
       if (obj->sink != NULL) return CONNECT_E_ADVISELIMIT;

       hr = unknown->lpVtbl->QueryInterface(
           unknown, &IID_ITfLangBarItemSink, (void **)&sink);
       if (SUCCEEDED(hr) && sink == NULL) hr = E_NOINTERFACE;
       if (FAILED(hr)) return hr;

       obj->sink = sink;             /* QI reference를 Unadvise/파괴까지 소유 */
       obj->sinkCookie = 1;          /* 이 객체는 sink 하나만 허용 */
       *cookie = obj->sinkCookie;
       return S_OK;
   }

   static HRESULT ModeItem_UnadviseSink(ModeItem *obj, DWORD cookie)
   {
       if (obj->sink == NULL || cookie != obj->sinkCookie)
           return CONNECT_E_NOCONNECTION;
       obj->sink->lpVtbl->Release(obj->sink);
       obj->sink = NULL;
       obj->sinkCookie = TF_INVALID_COOKIE;
       return S_OK;
   }
   /*
    * ITfSource vtable wrapper는 위 두 helper를 호출한다. 생성 시 cookie=TF_INVALID_COOKIE,
    * 최종 Release/Deactivate 정리도 연결이 남았으면 sink를 정확히 한 번 Release한다.
    */
   ```

2. **TIP 활성화 때 추가하고, 비활성화 때 제거한다.** `ActivateEx`에서 thread manager로부터
   `ITfLangBarItemMgr`를 얻어 `AddItem`, `Deactivate`에서 `RemoveItem`.
   ```c
   static HRESULT AddModeItem(ITfThreadMgr *ptim, ITfLangBarItem *item)
   {
       ITfLangBarItemMgr *mgr = NULL;
       HRESULT hr;

       if (ptim == NULL || item == NULL) return E_INVALIDARG;
       hr = ptim->lpVtbl->QueryInterface(
           ptim, &IID_ITfLangBarItemMgr, (void **)&mgr);
       if (SUCCEEDED(hr) && mgr == NULL) hr = E_NOINTERFACE;
       if (SUCCEEDED(hr)) hr = mgr->lpVtbl->AddItem(mgr, item);
       if (mgr != NULL) mgr->lpVtbl->Release(mgr);
       return hr;
   }

   /* Activate/ActivateEx에서 앞선 key sink 등록 뒤: */
   hr = AddModeItem(
       ptim, (ITfLangBarItem *)&myLangBarItem->button); /* 상속 prefix */
   if (FAILED(hr)) {
       UnwindActivatedSinksAndReferences(obj); /* Advise·AddRef 등 앞선 성공을 역순 정리 */
       return hr;
   }
   obj->langBarItemAdded = TRUE;
   /* Deactivate는 이 flag가 TRUE일 때만 RemoveItem하고 flag를 지운다. */
   ```

3. **`GetInfo`에서 중요한 건 딱 두 개** — 아이템의 정체(GUID)와 스타일:
   ```c
   pInfo->guidItem = GUID_LBI_INPUTMODE;                          // ★반드시 이 GUID
   pInfo->dwStyle  = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;  // legacy hint
   pInfo->clsidService = CLSID_Ours;  lstrcpyW(pInfo->szDescription, L"...");
   ```

   - `GUID_LBI_INPUTMODE = {2C77A81E-41CC-4178-A3A7-5F8A987568E6}` — **MinGW 헤더에 없으니
     직접 정의한다.** Win8+ 트레이 표시기는 **guidItem이 이 값인 아이템만 호스팅**하고, 다른
     GUID는 레거시 데스크톱 언어바에만 뜬다(사람들이 하루를 날리는 지점: `AddItem`은 성공하는데
     아무것도 안 뜸).
   - `TF_LBI_STYLE_SHOWNINTRAY`의 정확한 값은 `0x00000002`다. 다만 이 style 하나가 레거시
     아이템을 Win8+ 입력 표시기에 올리는 보편 스위치는 아니다. 이 설계에서는
     `GUID_LBI_INPUTMODE`가 결정적이고, style은 함께 주는 표시 hint다.
4. **`GetIcon`에서 현재 `HICON`을 반환한다.** 셸이 소유·파괴하므로 호출마다 새 아이콘을 만들어
   돌려준다(예: 현재 자판 축약을 그려서). 출력 포인터를 먼저 검증·NULL 초기화하고, 아직 아이콘이
   없으면 NULL을 둔 채 **`S_OK`**를 반환한다. 이 경우의 `S_FALSE` 계약은 문서화돼 있지 않다.
   ```c
   static HRESULT STDMETHODCALLTYPE
   Button_GetIcon(ITfLangBarItemButton *This, HICON *phIcon)
   {
       ModeItem *obj = ButtonToModeItem(This);
       if (phIcon == NULL) return E_INVALIDARG; /* GetIcon 공식 계약 */
       *phIcon = NULL;
       if (!obj->active) return S_OK;       /* no icon은 성공 + NULL */
       *phIcon = CreateFreshModeIcon(obj);  /* 반환 icon은 shell 소유 */
       return S_OK;                         /* NULL도 문서화된 성공 결과 */
   }
   ```

5. **상태가 바뀌면 셸에 재조회를 요청한다** — `AdviseSink`에서 받은 sink로
   `sink->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT)`. `TF_LBI_ICON`의 정확한 값은
   `0x00000001`이며 이것은 update event flag이지 style이 아니다. 이 한 번의 호출이
   "아이콘 갱신" 경로다.

이게 전부다: 아이템 하나, 활성/비활성에서 add/remove, 올바른 GUID+스타일, 요청 시 아이콘,
바뀔 때 OnUpdate.

**우클릭 메뉴(선택).** `TF_LBI_STYLE_BTN_BUTTON`이면 **우클릭도 `OnClick(TF_LBI_CLK_RIGHT)`으로
온다** — 메뉴는 직접 구성한다. (`InitMenu`/`ITfMenu` COM 메뉴는 `BTN_MENU` 전용인데, `BTN_MENU`면
좌클릭에도 메뉴가 떠서 "좌클릭=토글" UX가 깨진다. 그래서 `BTN_BUTTON`을 쓰고 우클릭을 직접 처리.)
```
OnClick(RIGHT, point):
    CreatePopupMenu + InsertMenuItem            // 메뉴를 직접 구성
    point.x를 모니터 work-area로 클램프
    cmd = TrackPopupMenu(TPM_NONOTIFY|TPM_RETURNCMD|TPM_LEFTBUTTON,
                         point, owner=GetFocus())   // ★owner 필수, NONOTIFY 필수
    cmd에 따라 실행
```

**아이콘 지침(MS 요구)**: **흑백 전용**(흰 글리프+검정 외곽/검정 배지), 기본 16px에 DPI 스케일.

### 12.5 선택(블록) 텍스트 한자 변환 — range 편집 없이 '교체'하기
- **통찰**: 선택이 활성인 상태에서 호스트가 `InsertTextAtSelection`을 실제로 반영하면 그
  연산은 **선택을 교체**한다.
- **결과**: "텍스트 선택 → 한자키" UX는 시험한 비교 경로에서 한 번의 연산으로 단어 단위
  한자 변환을 제공했다. 제품 행렬의 EDIT 계열은 §13.4의 명시적 선택 메시지 사다리를 쓴다.
  어느 결과도 시험하지 않은 호스트의 지원을 증명하지 않는다.
- 선택 읽기는 READ 세션에서 `GetSelection`→`GetText`. 후보창은 포커스를 안 뺏으므로(NOACTIVATE)
  선택이 유지되고, 취소 시 문서 무접촉이라 선택도 그대로다.

### 12.6 진단 방법론 — 로깅이 추측을 이긴다
- 조합 사망(§8)도, 음절 증발(§12.3)도, 칩 지연(§12.2)도 **%TEMP% 구조 로그**로 풀 수 있다.
  그러나 입력 글자·키값·문서 내용·포인터는 기록하지 않아도 수명 문제를 판정할 수 있다.
- **composition 진단 최소 필드**: 단조 증가 순번, PID/TID, 컨텍스트 세대, 트랜잭션 번호,
  현재 단계, 각 HRESULT, 입력/결과 길이, composition 존재 여부, 종료 epoch.
  `StartComposition`·`SetText`·`SetSelection`·`ShiftStart`·`EndComposition` 전후와
  `OnCompositionTerminated`·`OnEndEdit`를 같은 순번 축에 놓는다.
- **`S_OK` 뒤의 상태를 기록하라.** 호스트가 콜백으로 조합을 끝내도 API 자체는 성공할 수 있다.
  세션 직후 `composition=0` 또는 종료 epoch 증가가 보이면 HRESULT만으로 내린 성공 판정이 틀렸다.
- `OnEndEdit`의 `ITfEditRecord::GetTextAndPropertyUpdates`로 **텍스트,
  `GUID_PROP_COMPOSING`, `GUID_PROP_ATTRIBUTE`, `GUID_PROP_LANGID`,
  `GUID_PROP_READING`에 변경 range가 있었는지**를 각각 묻는다. 실제 range 텍스트나 속성 값은
  읽지 않고 존재 여부만 기록한다. `ITfContext::InWriteSession`도 함께 기록하면 세션 뒤 콜백과
  TIP이 아직 소유한 쓰기 작업을 구분할 수 있다.
- **한 변수씩 A/B한다.** AkelPad 시험판은 (0) 기존 방식, (1) `TF_AE_NONE`만 적용,
  (2) 첫 문자열 삽입 후 composition 시작만 적용, (3) 명시적 `SetSelection`만 생략한 네 개의
  독립 CLSID/프로필로 만든다. 네 변경을 한 DLL에 섞으면 정상화돼도 원인을 알 수 없다.
  단, **계측 자체의 영향**을 비교할 때는 새 CLSID/DLL이 또 다른 변수가 되므로 같은 바이너리의
  런타임 모드로 바꾸고 프로세스만 새로 시작한다.
- **1차 A/B 결과(2026-07-24):** 네 프로필 모두 AkelPad에서는 자모 분리, 메모장에서는 정상
  음절이었다. 세션 뒤 편집의 텍스트·조합·표시 속성 변경과 조합 종료 순서는 유효하다. 다만
  당시 `changed_langid`/`changed_reading` 탐침에 수기로 잘못 옮긴 GUID를 썼음이 2차 실기 뒤
  발견됐다. 따라서 “reading 변경 없음”은 폐기한다. 선택 스타일, 명시적 선택, 시작 순서가
  충분한 해결책이 아니라는 1차 결론은 이 속성 탐침과 무관하므로 유지된다.
- **2차 A/B 결과(2026-07-24):** 실제 LANGID/READING 효과를 판정하지 못했다. 모든 속성
  적용 사건이 `E_FAIL`이었고, 그와 별개로 두 property GUID가 잘못 옮긴 수기 상수였음도
  확인됐다. 당시 schema는 `GetProperty`와 `SetValue`를 분리하지 않아 어느 하위 호출이
  실패했는지나 오기 GUID가 그 실패를 직접 일으켰는지는 모른다. 메모장의
  영어·자모 혼합 출력은 `SetText`가 이미 성공했는데 세션 실패를 rollback처럼 취급하고 원래
  키까지 통과시킨 실험체 오류 경로였다(§12.7.7).
- **정정 R2 실기 결과(2026-07-24):** 여덟 셀 모두 구조 검증을 통과했다. AkelPad의
  Reading-only는 composition 1회 생성·5회 재사용·키별 외부 종료 0으로 `가나다`를 만들었다.
  Control, LANGID-only, LANGID→Reading은 6키 모두 composition 재생성과 외부 종료가 반복돼
  자모가 분리됐다. 메모장은 네 profile 모두 정상이다. 실제 property 호출은 nonempty range에서
  전부 `S_OK`였으므로 이번에는 metadata 가설이 적용됐다. 다만 LANGID 존재·속성 순서·반복
  쓰기가 아직 섞여 있어 R3에서 분리하려 했다. 그 양성 대조 실패와 R4 후속은 §12.7.8에 있다.
- **R3 구조 실기 결과(2026-07-24):** 의도한 8개 matrix 셀 모두 schema·호출 순서·6회
  update·세션 검증을 통과했고, 별도의 AkelPad Reading 반복 1개도 유효했다. 그보다 앞선 시도
  2개는 유효한 새 trace를 만들지 못해 현장 증거에서 제외한다. AkelPad 네 변형은 모두 새
  composition 6·재사용 0·종료 6, 메모장 네 변형은
  모두 새 composition 1·재사용 5·키별 종료 0이었다. R2의 양성 대조인 Reading도 R3에서
  실패했으므로 R3의 순서/LANGID/반복 인과 질문은 답하지 못했다. R3 수집물에는 독립 화면 판정이
  없으므로 표시 문자열은 추정하지 않는다(§12.7.8).
- **관찰자 효과도 프로토콜 변수다.** 진단판이 `GetValue` 같은 TSF API를 더 호출하거나 매 단계
  동기 파일 I/O를 추가하면 조합 수명과 타이밍을 바꿀 수 있다. “같은 기능에 로그만 추가했다”는
  양성 대조가 재현될 때만 성립한다. 계측 없는 baseline, 같은 trace만 있는 control, read-back을
  더한 변형, 마지막 baseline을 같은 바이너리에서 비교한다.
- 비교 빌드는 기존 입력기를 덮어쓰지 않도록 DLL 이름·표시 이름·CLSID·프로필 GUID까지 분리하고,
  프로세스마다 별도 JSONL을 만든다. 고정 입력 시나리오를 메모장과 문제 앱에서 각각 한 번 실행한다.
- **개인정보 원칙**: 입력 내용과 메모리 주소는 어떤 진단판에도 남기지 않는다. 로그는 필요한
  구조 값만 기록하고 테스트 후 삭제한다. 일반 릴리스에서는 로깅을 비활성화한다.
- **낡은 DLL 함정 재확인**: 증상이 불가해하게 나타났다 사라지면 코드보다 먼저 배포를 의심하라
  (§10의 파일 잠금 — 이 프로젝트에서 두 번이나 유령 증상의 범인이었다).

### 12.7 실패 장부 — AkelPad 자모 분리 조사에서 버린 가설과 남은 원인

이 절은 “마지막으로 잘된 코드”보다 **실패를 어떻게 판정했는지**를 보존한다. 같은 증상을 만난
다음 구현자가 API 호출 순서를 무작위로 바꾸거나, `S_OK`만 보고 성공이라 믿거나, 이미 기각된
선택 스타일을 다시 시험하는 시간을 줄이는 것이 목적이다.

#### 12.7.1 먼저 실패를 세 종류로 분류한다

| 종류 | 판정 기준 | 예 | 처리 |
|---|---|---|---|
| **우리 구현 결함** | ABI·HRESULT·수명·상태 전이가 계약을 위반 | 잘못된 vtable 시그니처, `S_OK+NULL` composition 역참조, 실패 뒤 상태 게시 | 고친 뒤 동일 시나리오를 처음부터 재검사 |
| **프로토콜 가설 기각** | 변경한 호출은 의도대로 실행되고 모두 성공했지만 화면·수명 결과가 대조군과 같음 | `TF_AE_NONE`, 선삽입, `SetSelection` 생략 | “해결책이 아님”으로 기록하되 다른 호스트까지 일반화하지 않음 |
| **시험 도구·배포 결함** | 새 코드가 실행되지 않았거나 로그를 수집하지 못함 | 잠긴 옛 DLL, 잠긴 로그 삭제, 배치 변수 선행 확장 | IME 원인 분석을 중단하고 시험 장치부터 수정 |

이 구분을 하지 않으면 “코드는 성공했는데 로그 수집기가 실패한 것”과 “호스트가 조합을 끝낸 것”이
같은 실패로 섞인다. 특히 **API 반환 성공**, **편집 세션 성공**, **조합의 세션 뒤 생존**은 서로
다른 판정값이다.

#### 12.7.2 고정 재현과 실제 로그가 말한 것

실험은 64비트 AkelPad와 Windows 메모장을 비교했다. 각 프로필마다 새 프로세스·새 문서에서
`rkskek`를 한 번 입력했다. 기대 화면은 `가나다`이고, AkelPad의 관측 화면은 네 프로필 모두
호환 자모가 따로 확정된 형태였다. DLL 이름·CLSID·프로필 GUID·표시 속성 GUID·로그 이름을
각각 분리해 이전 DLL이나 다른 변형을 잘못 고르는 위험도 줄였다.

한 번의 고정 6키 실행에서 얻은 AkelPad 로그는 네 프로필 모두 다음과 같았다.

| 구조 사건 | Control | `TF_AE_NONE` | Insert First | No Selection |
|---|---:|---:|---:|---:|
| 편집 세션 결과 | 6 | 6 | 6 | 6 |
| request/session/inner 실패 | 0 | 0 | 0 | 0 |
| `StartComposition` 성공 | 6 | 6 | 6 | 6 |
| 트랜잭션이 닫힌 뒤 `txn=0` 추가 `OnEndEdit` | 6 | 6 | 6 | 6 |
| 추가 편집의 text/composing/attribute 변경 | 6/6/6 | 6/6/6 | 6/6/6 | 6/6/6 |
| 추가 편집의 reading 탐침(후에 무효 판정) | 0* | 0* | 0* | 0* |
| `OnCompositionTerminated` | 6 | 6 | 6 | 6 |

순서는 매 키마다 아래처럼 반복됐다. 숫자는 설명용이며 실제 PID·문자열은 필요 없다.

```text
txn=N  range.set_text.after       S_OK  composition=1
txn=N  display_attribute.apply    S_OK  composition=1
txn=N  edit_session.result        all-S_OK, callback_ran=1
txn=N  edit_transaction.close           composition=1
txn=0  text_edit.end                    selection_changed=0
txn=0  changed_text/composing/attribute = 1, changed_reading = 0*
txn=0  composition_terminated           termination_epoch += 1
```

`*` 2차 실기 분석에서 `changed_reading`과 `changed_langid`가 실제 predefined property가
아니라 잘못 옮긴 GUID를 조회했음이 드러났다. 따라서 그 두 필드의 0은 폐기한다. 텍스트,
`GUID_PROP_COMPOSING`, `GUID_PROP_ATTRIBUTE`, HRESULT, transaction, 종료 epoch는 올바른
상수·독립 필드이므로 위 수명 순서의 증거는 그대로 유효하다.

이 순서에서 중요한 것은 종료가 `SetText` 호출 **안**이나 우리 편집 콜백 **안**에서 발생한 것이
아니라는 점이다. 우리 트랜잭션이 닫힐 때까지 로컬 composition은 살아 있었고, 그 뒤 별도 편집
기록과 종료 콜백이 왔다. 따라서 즉시 API 실패, callback 미실행, 로컬 코드가 직접 호출한
`EndComposition`은 이 재현의 직접 원인이 아니다.

메모장에서는 화면상 네 변형이 모두 정상 음절을 만들었다. 구조 로그도 첫 키에서 만든
composition을 다음 키의 `composition.get_range.existing`으로 재사용했다. 프로그램 종료나
명시적 확정 때 오는 종료 콜백은 정상 수명 사건이므로, 단순히 로그에
`OnCompositionTerminated`가 한 번 있다는 사실만으로 실패라 판정하면 안 된다. **매 키 직후
`txn=0` 편집과 종료가 한 쌍으로 반복되는가**가 AkelPad 실패의 지문이다.

컨텍스트 상태도 달랐다. 해당 AkelPad 실행은 `static_flags=4`
(`TF_SS_TRANSITORY`)와 `dynamic_flags=0x40000000`을, 메모장은 `static_flags=18`과
`dynamic_flags=0`을 보고했다. 이것은 두 앱이 같은 종류의 text store가 아니라는 증거지만,
그 플래그 자체가 종료를 명령했다는 증거는 아니다.

#### 12.7.3 실제로 고친 결함 — 중요하지만 AkelPad의 최종 원인은 아니었던 것

| 결함/잘못된 가정 | 나타난 증상 | 수정과 판정 |
|---|---|---|
| **과거 표준 trace lab**의 `ITfDisplayAttributeProvider::GetDisplayAttributeInfo` C vtable에 존재하지 않는 `BSTR *`를 네 번째 인자로 선언 | COM 호출 스택/레지스터가 어긋날 수 있는 ABI 미정의 동작 | 공식 3인자 메서드(`This`, `REFGUID`, `ITfDisplayAttributeInfo **`)로 수정. 그 정정 lab의 AkelPad 실기에서도 종료가 재현됐으므로 **그 lab 실행의** 충분 원인에서는 제외 |
| **main 제품**의 같은 vtable에 존재하지 않는 `TfGuidAtom *`를 네 번째 인자로 두고, 직접 선언한 Provider IID 이름에는 다른 `ITfDisplayAttributeMgr` IID 값을 붙임 | x86 ABI 손상 가능성과 잘못된 QI 계약이 함께 있어 제품 안정성·진단을 흐림 | 추가 인자를 제거하고 공식 Provider IID `{FEE47777-163C-4769-996A-6E9C50AD8F54}`로 정정. T009 정적/계약 검사와 x64·x86 빌드만 검증했으며 **새 Windows/AkelPad 실기 판정은 아직 없다**. 위 lab 실기 결과를 이 제품 수정의 현장 검증으로 재사용하지 않음 |
| Learn의 “IUnknown 상속” 요약만 보고 SDK의 `ITfLangBarItemButton : ITfLangBarItem`을 무시한 채 item/button을 형제 vtable로 나누려 함 | `OnClick`을 item prefix 위치에서 찾는 실제 Windows 호출자와 ABI가 어긋날 수 있음 | Microsoft SDK IDL을 최종 oracle로 삼아 임시 형제-vtable 수정을 커밋 전에 되돌리고, `IUnknown`→item 4개→button 5개 순서를 T010으로 고정 |
| 언어바의 `ITfSource`/sink 수명과 수기 update/style 상수, no-icon 반환을 추정 | sink 누수/UAF, 잘못된 update mask, `AddItem`은 성공해도 tray가 갱신되지 않는 현상 가능 | 상속된 button/item prefix와 별도 `ITfSource`, Advise/Unadvise cookie·sink 소유권, `TF_LBI_ICON=0x1`, `TF_LBI_STYLE_SHOWNINTRAY=0x2`, `GetIcon`의 `S_OK + NULL`로 정정. Windows tray 실기 판정은 별도 |
| 공식상 선택적인 `StartComposition` sink를 NULL로 전달했지만 시험 호스트의 실제 결과를 확인하지 않음 | 해당 경로에서는 `E_INVALIDARG`; 조합 객체 없이 앞선 삽입만 남아 “부분 성공”처럼 보임 | 이식성과 종료 관측을 위해 실제 sink를 전달. NULL도 API상 유효하므로 `E_INVALIDARG`는 현장 비호환 관찰로 기록 |
| `StartComposition == S_OK`만 검사하고 반환 composition이 NULL인 경우를 고려하지 않음 | owner의 정상 거절 뒤 range 접근 실패 또는 로컬 상태와 문서 불일치 | `S_OK + NULL`은 거절로 처리하고 composition 상태를 publish하지 않음. 앞선 text 편집은 자동 rollback되지 않으므로 별도 정책 적용 |
| `RequestEditSession` 반환값과 세션 콜백의 `phrSession`/내부 HRESULT를 한 값처럼 취급 | 요청만 성공한 세션을 성공으로 오판 | `request_hr`, `session_hr`, `inner_hr`, `callback_ran`을 따로 보존 |
| 문서 편집 실패 뒤에도 FSM을 publish하고 `eaten=TRUE` 유지 | 키는 앱에 가지 않고 내부 상태만 진행되어 자모가 흩어지거나 글자가 남음 | 복사본에서 계산하고 독립 postcondition으로 `MUTATION_DONE`을 증명한 뒤에만 FSM publish. 실패 시 `MUTATION_NONE`일 때만 원래 키를 통과시키고, `DONE` 뒤 후속 실패나 `UNKNOWN`이면 중복 회피 risk policy에 따라 소비·정리한다. 이후 AkelPad에서는 모든 세션이 성공해도 외부 종료가 남았음 |
| 한/영 compartment의 `VT_EMPTY`를 꺼짐으로 해석 | PuTTY 등에서 한/영이 한 번만 동작 | `VT_EMPTY`는 “미설정”으로 보고 로컬 fallback 상태 유지. 조합 종료와는 별개 문제 |
| capability GUID의 이름/값 교차와 immersive GUID 오타 | 특정 환경 등록·활성화 판단을 흐릴 수 있음 | Microsoft 상수로 정정. 데스크톱 AkelPad에서 이미 프로필이 로드·활성화됐으므로 매 키 종료의 유력 원인은 아니지만 실험 교란 요인은 제거 |
| x86 DLL 또는 COM ABI 문제라는 추측 | 64비트 앱이 다른 DLL을 로드했을 가능성 | AkelPad가 64비트임을 확인하고 PE32+ x86-64 DLL·필수 export를 검사. 동일 증상으로 비트수 가설 제외 |
| TSF DLL이 여러 프로세스에 매핑된 상태에서 파일만 덮어씀 | 수정 전후 증상이 유령처럼 섞임 | 변형별 새 identity와 로그 stem 사용, 편집기 종료·등록 해제·필요 시 로그아웃 후 재설치 |
| 수집 시작 전에 임시 JSONL을 전부 삭제 | 다른 프로세스가 옛 로그를 열고 있으면 시험 자체가 중단 | 삭제를 없애고 시작 시각 marker 이후 갱신된 해당 변형 로그만 복사; 잠긴 후보 하나는 건너뜀 |
| `cmd.exe` 괄호 블록 안에서 `set /p` 직후 `%TARGET%` 사용 | 사용자가 입력한 AkelPad 경로가 빈 옛 값으로 평가 | 입력과 사용을 블록 밖으로 분리. 이후 기본 설치 경로를 자동 사용해 수동 입력 자체를 제거 |

교훈은 “고칠 가치가 없는 버그였다”가 아니다. 이 결함들을 먼저 제거했기 때문에 그 다음 로그를
신뢰할 수 있었다. 다만 **결함을 고친 뒤에도 같은 세션 밖 종료가 남았으므로**, 그것들을 현재
AkelPad 자모 분리의 충분한 설명으로 계속 붙들고 있으면 안 된다.

> **구현 상태 주의:** 위 transaction 행은 반드시 채택해야 할 hardening과 표준 lab/예제의
> 계약을 기록한다. 2026-07-24 현재 main 제품의 `CommitText`/`OutputResult`는 outcome을
> end-to-end로 보존하지 않고 FSM이 문서 성공 전에 바뀔 수 있으며, `EditCtl_ReplaceSelection`은
> `SendMessage`의 최종 mutation을 증명하지 못한다. 따라서 이 장부를 “main 제품 migration
> 완료”로 읽지 않는다. 완료 조건은 trial 상태에서 계산하고, `CommitOutcome`을 호출자까지
> 전달하며, `MUTATION_NONE`일 때만 원래 키를 통과시키는 것이다.

**2차 실기에서 추가로 발견했고 Meta R2에서 수정한 결함:**

- `GUID_PROP_LANGID`와 `GUID_PROP_READING`의 이름은 맞았지만 16바이트 값이 Windows SDK와
  달랐다. 기존 계약 테스트는 capability GUID만 값 비교하고 이 두 property GUID는 호출 모양만
  검사했으므로 통과했다. 수기 WinAPI 상수는 이름이 아니라 바이트를 공식 원본과 비교해야 한다.
- 같은 오기 상수를 속성 적용과 `OnEndEdit` 변경 탐침 양쪽에 재사용해, “적용 실패”와 “변경
  없음”이 서로를 그럴듯하게 확인하는 폐쇄 루프가 생겼다. 독립 기준값 없는 자기일치 검사는
  정확성 검사가 아니다.
- `SetText` 성공 뒤 선택적 metadata 실패를 edit-session 전체 실패로 반환했다. Windows 편집
  세션은 데이터베이스 transaction처럼 이전 편집을 자동 rollback하지 않는다. 이미 문서를
  바꾸고 `pfEaten=FALSE`로 원래 키까지 보내면 한 물리 키가 두 번 반영된다.
- 수집기는 activation-only 파일과 빈 profile 폴더도 정상 수집처럼 남겼다. 다음 판은 build
  identity, 예상 transaction 수, property event 수가 없으면 해당 셀을 불완전으로 판정해야 한다.

Meta R2는 이 네 결함을 수정했으며 제품 입력기와 v1 시험판을 바꾸지 않는 새 CLSID/profile/
display-attribute/trace identity를 썼다. **이 문장이 처음 작성됐을 당시에는** 빌드와 패키지
검증만 통과하고 Windows 실기는 대기 중이었다. 이후 완료된 8셀 실기와 판정은 §12.7.8에
기록한다. 이 역사적 체크포인트만 떼어 metadata 성공 결과로 읽으면 안 된다.

#### 12.7.4 한 변수 A/B에서 기각된 AkelPad 해결책

| 가설 | 격리한 변경 | 기대 | 실제 결과 | 지금 말할 수 있는 것 |
|---|---|---|---|---|
| 선택 active-end가 조합을 밖으로 밀어낸다 | `TF_AE_END` → `TF_AE_NONE`만 변경 | 호스트가 조합을 유지 | AkelPad 동일 실패, 메모장 정상 | 선택 스타일 하나는 충분한 해결책이 아님 |
| MS 한글 IME처럼 먼저 삽입해야 한다 | `TF_IAS_NO_DEFAULT_COMPOSITION`으로 첫 문자열을 넣고 반환된 비어 있지 않은 range에서 같은 write session 안에 조합 시작 | 시작 순서가 호환층 기대와 맞음 | AkelPad 동일 실패, 모든 호출 `S_OK` | 선삽입 순서 하나는 충분하지 않음. MS IME의 전체 비공개 동작까지 재현했다는 뜻은 아님 |
| 명시적 `SetSelection`이 종료를 유발한다 | 그 호출만 생략 | 추가 편집·종료가 사라짐 | 자체 세션의 `selection_changed`는 1→0으로 바뀌었지만 세션 뒤 추가 편집·종료는 그대로 | `SetSelection` 호출 자체는 종료 트리거가 아님 |
| 기본 경로가 이미 API 단계에서 실패한다 | Control의 모든 단계와 세션 HRESULT 관측 | 실패 HRESULT 발견 | 6회 모두 request/session/inner `S_OK`, callback 실행 | 즉시 호출 실패 가설 기각; 수명 관측으로 이동해야 함 |

이 결과는 **AkelPad x64의 이 고정 시나리오**에 대한 것이다. 다른 text store에서
`TF_AE_NONE`이나 시작 순서가 중요하지 않다는 보편 명제가 아니다.

#### 12.7.5 이전 레거시 호스트 우회가 낭비한 시간

AkelPad A/B 전에도 PuTTY·메신저·고전 컨트롤에서 여러 우회를 시험했다. 이 기록은 호스트가
다르므로 현재 AkelPad 결과와 섞어 “같은 원인”이라 부르면 안 되지만, 다시 시도할 가치가 낮은
접근을 알려 준다.

| 시도 | 실패 양상 | 배운 것 |
|---|---|---|
| `TF_ST_CORRECTION` 대신 0 사용 | 표준에는 맞지만 해당 호스트 종료는 지속 | 올바른 플래그는 필요조건이지 호환 해결책은 아님 |
| 빈 composition을 별도 세션에서 먼저 정착 | 빈 조합은 살지만 텍스트 갱신 뒤 종료 | 세션 분리만으로 충분하지 않음 |
| 선택 range/조합 range clone 등 캐럿 range 변경 | 메모장은 정상, 문제 호스트는 동일 | 캐럿 range 종류 하나로 설명 불가 |
| 표시 속성과 캐럿 적용 순서 교환 | 동일 종료 | 순서 무작위 재배열을 계속할 근거 없음 |
| PuTTY에서 `GUID_PROP_LANGID` 설정 | 개선 없음 | 그 호스트에서 LANGID 하나가 충분하지 않았을 뿐, AkelPad의 아직 안 한 실험을 대신하지 않음 |
| composition 없는 range 치환 | 기존 글자가 교체되지 않고 `ㄱ가간…`처럼 누적 | 제한적 text store의 뒤쪽 range 편집을 범용으로 가정하지 말 것 |
| `VK_BACK`으로 지우고 Unicode 재전송 | 원격 셸 echo 지연·wide character 폭과 충돌해 누락/겹침 | 터미널의 표시·라인 편집 문제를 IME accounting만으로 고칠 수 없음 |
| append-only 확정 | 느릴 때는 되나 빠른 입력에서 누락 | 경합하는 합성 입력은 정확성 기반이 될 수 없음 |
| `ImmSetCompositionStringW` 직접 호출 | 프리에딧은 보였지만 `CPS_COMPLETE`가 문서 확정을 만들지 않아 중간 음절 소실 | TSF TIP 안에서 IMM context를 직접 움직이는 혼합 경로는 원자적 commit 계약이 없음 |

다른 성숙한 TSF IME 하나가 PuTTY에서 같은 증상을 보였던 관측도 있었지만, **표본 하나는
“순수 TSF는 모든 CUAS 앱에서 불가능하다”는 증명이 아니다.** 제품이 commit-only를 택한 것은
현재 지원 행렬의 위험을 줄이기 위한 공학적 결정이며, 표준 실험체 연구는 별도로 계속한다.

#### 12.7.6 현재 가장 강한 원인 모형

| 확신 수준 | 설명 | 근거와 한계 |
|---|---|---|
| **확정** | 실패 실행에서는 우리 write transaction이 성공하고 닫힌 뒤, 별도 편집과 외부 종료 콜백이 온다 | R1·R2 실패 프로필과 R3 네 프로필에서 `txn=N` 성공 뒤 `txn=0` 변경과 종료가 키마다 반복 |
| **확정** | 같은 시험판에서도 AkelPad와 메모장의 composition 수명이 다르다 | R3 AkelPad는 네 프로필 모두 새 6/재사용 0/종료 6, 메모장은 새 1/재사용 5/키별 종료 0 |
| **확정** | R2 Reading은 유지됐지만 R3 Reading은 유지되지 않았다 | R2 Reading은 1회 생성·5회 재사용·종료 0, R3 Reading은 6회 생성·종료. 두 판의 프로토콜·계측이 동일하지 않으므로 모순이 아니라 교란 증거 |
| **강한 추정** | AkelPad의 TSF context가 IMM32 호환 경로와 상호작용하면서 결과 상태를 다시 쓰고 composition을 끝낸다 | `TF_SS_TRANSITORY`, AkelEdit의 `WM_IME_*`·`ImmGetCompositionStringW` 처리, 실패 때 세션 밖 변경. 그러나 로그는 행위자나 내부 결정을 식별하지 못함 |
| **열린 가설** | R3에 새로 들어간 즉시 property read-back 또는 동기 trace I/O가 수명/타이밍에 영향을 줬다 | Reading 양성 대조군도 실패했다. 두 변경이 함께 들어가 어느 쪽인지, 또는 환경 drift인지 R3만으로 구분 불가 |
| **충분조건으로 기각** | active-end 스타일, 명시적 selection, 선삽입 순서, LANGID-only, 즉시 API 성공/실패, 앱 비트수 | 각각 독립 A/B·R2 또는 바이너리 검사에서 단독 해결책이 아니었음 |
| **아직 판정 금지** | LANGID 존재 자체, Reading/LANGID 순서, 반복 LANGID 쓰기 | 이것을 묻는 R3의 Reading 양성 대조군이 재현되지 않아 실험의 인과 전제가 무너짐 |
| **말할 수 없음** | “AkelPad가 직접 종료했다”, “CUAS 버그가 확정 원인”, “MS IME는 비밀 API를 쓴다” | 현재 로그에는 호출 주체·스택·호환층 내부 상태가 없음 |

즉 현재 최선의 표현은 **“실패한 AkelPad 실행에서는 성공한 TIP 편집 뒤 두 번째 편집과 조합
종료가 이어지며, 이 수명은 metadata와 계측 조건에 민감했다”**이다. 이를 곧바로 “CUAS가
범인” 또는 “GetValue가 범인”으로 줄이면 추정이 사실로 둔갑한다.

#### 12.7.7 두 번째 메타데이터 A/B — 실행했지만 가설 적용 전에 실패했다

시험판은 나머지 프로토콜을 고정하고 네 프로필을 비교하도록 만들었다.

1. Control: 추가 메타데이터 없음
2. LANGID: 현재의 **비어 있지 않은** composition range에 `GUID_PROP_LANGID=ko-KR`
   (`VT_I4`) 설정
3. Reading: 같은 range에 현재 reading 문자열(`VT_BSTR`) 설정
4. Both: 둘 다 설정

그러나 업로드된 구조 로그는 이 가설이 적용되기 전에 실험체 자체가 실패했음을 보였다.

| 완결 로그 | edit session | metadata 적용 결과 | session/inner | `hangul_step.failed` |
|---|---:|---:|---:|---:|
| AkelPad LANGID | 6 | 6회 모두 `E_FAIL` | 6회 모두 `E_FAIL` | 6 |
| AkelPad Both | 6 | 12회 모두 `E_FAIL` | 6회 모두 `E_FAIL` | 6 |
| 메모장 LANGID(완결 실행 2개) | 각 6 | 각 6회 모두 `E_FAIL` | 각 6회 모두 `E_FAIL` | 각 6 |
| 메모장 Both | 6 | 12회 모두 `E_FAIL` | 6회 모두 `E_FAIL` | 6 |

AkelPad Control은 activation 사건만 있고 입력 transaction이 없으며, 두 앱의 Reading-only
로그는 비어 있다. 화면 관찰은 보존하되 이 세 셀의 내부 동작을 추정해서 채우면 안 된다.

실험 무효를 확정한 설정 결함은 공식 Win32 metadata와의 바이트 비교로 찾았다.

| 속성 | 시험 DLL의 값 | Windows SDK 값 |
|---|---|---|
| `GUID_PROP_LANGID` | `3280CE20-8032-11D2-B603-00C04F93D015` | `3280CE20-8032-11D2-B603-00105A2799B5` |
| `GUID_PROP_READING` | `5463F7C0-8E31-11D3-A9F3-00805F8EFFF8` | `5463F7C0-8E31-11D2-BF46-00105A2799B5` |

```c
static const GUID kPropLangId = {
    0x3280ce20, 0x8032, 0x11d2,
    { 0xb6, 0x03, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 }
};
static const GUID kPropReading = {
    0x5463f7c0, 0x8e31, 0x11d2,
    { 0xbf, 0x46, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 }
};
```

`ITfContext::GetProperty`는 custom GUID도 받을 수 있으므로 이름이 그럴듯한 객체 경로가
predefined property 검증은 아니다. 이번 trace event는 `GetProperty`와 `SetValue` 중 마지막
HRESULT 하나만 남겼고 모두 `E_FAIL`이었다. 정확히 어느 하위 호출이 실패했는지는 이 로그로
말할 수 없다. `OnEndEdit`도 같은 잘못된 GUID로 변경 range를 조회했다.

메모장의 이상 화면은 사건 순서로 설명된다.

```text
StartComposition / SetText     S_OK  (문서는 이미 변경됨)
wrong-property apply path      E_FAIL
edit session                   E_FAIL (앞선 SetText는 자동 rollback되지 않음)
display attribute/selection    실행 전 조기 반환
local composition release
pfEaten = FALSE                원래 물리 키도 메모장으로 통과
```

선택을 range 끝으로 옮기기 전에 돌아왔으므로 원래 키는 커서 왼쪽에 쌓이고, 매번 같은 경계에
삽입한 자모는 오른쪽으로 밀리며 역순으로 보였다. 사용자 관찰
`rkskek(커서)ㅏㄷㅏㄴㅏㄱ`과 구조 로그가 일치한다. 이것은 메모장의 TSF 조합 한계가 아니라
**부분 편집 뒤 실패 처리 결함**이다.

따라서 기존 결론은 둘로 나눠야 한다.

- **유지:** 1차 A/B에서 AkelPad는 모든 TIP 호출과 세션이 성공한 뒤 별도 편집·종료로 조합을
  매 키 끝냈다. 선택 스타일·선삽입·선택 생략이 충분하지 않았다는 결론도 유지된다.
- **미판정:** 실제 LANGID/READING이 그 종료를 바꾸는지는 이번에도 시험하지 못했다. “둘 다
  효과 없음”이라고 쓰면 안 된다.

정정판은 다음 조건을 모두 만족해야 한다.

1. 적용 코드와 변경 탐침 양쪽의 GUID를 공식 16바이트 값으로 교체하고 실행 테스트로 고정한다.
2. `SetText` 뒤 `ITfComposition::GetRange`를 다시 얻어 `IsEmpty`와 길이만 기록한 다음
   `SetValue`한다. Microsoft SampleIME도 언어 속성을 설정할 때 composition range를 다시 얻는다.
3. 선택적 metadata 실패를 이미 성공한 `SetText`의 가짜 rollback으로 전파하지 않는다.
   기본 조합을 계속 살리거나, 원래 키를 통과시키기 전에 모든 편집을 명시적으로 되돌린다.
4. 새 DLL/schema identity를 쓰고, 수집기가 activation-only·빈 profile 셀을 거부한다.
5. Control/LANGID/Reading/Both × AkelPad/메모장 8개 셀을 모두 다시 실행한다.

선택 metadata는 핵심 텍스트 편집의 성공 여부와 분리한다. 다음 예제는 Reading 설정 실패를
진단에는 남기되, 이미 성공한 `SetText`를 실패나 rollback으로 위장하지 않는다.

```c
static HRESULT SetReadingOptional(ITfContext *ctx, TfEditCookie ec,
                                  ITfRange *nonemptyRange,
                                  const WCHAR *reading, UINT32 readingLen)
{
    ITfProperty *prop = NULL;
    VARIANT value;
    HRESULT hr;

    VariantInit(&value);
    hr = ctx->lpVtbl->GetProperty(ctx, &GUID_PROP_READING, &prop);
    if (FAILED(hr)) return hr;
    if (prop == NULL) return E_UNEXPECTED;

    V_VT(&value) = VT_BSTR;
    V_BSTR(&value) = SysAllocStringLen(reading, readingLen);
    if (V_BSTR(&value) == NULL && readingLen != 0) {
        prop->lpVtbl->Release(prop);
        return E_OUTOFMEMORY;
    }

    hr = prop->lpVtbl->SetValue(prop, ec, nonemptyRange, &value);
    VariantClear(&value); /* 성공·실패와 무관하게 BSTR 해제 */
    prop->lpVtbl->Release(prop);
    return hr;
}

static HRESULT ApplyTextWithOptionalReading(EditSession *es, TfEditCookie ec,
                                            ITfRange *range,
                                            MutationState *mutation)
{
    ITfRange *live = NULL;
    BOOL empty = TRUE;
    *mutation = MUTATION_NONE;
    *mutation = MUTATION_UNKNOWN; /* SetText dispatch부터 부분 반영 가능성을 보수적으로 추적 */
    HRESULT core_hr = range->lpVtbl->SetText(
        range, ec, 0, es->text, es->text_len);
    if (FAILED(core_hr)) return core_hr; /* 아직 성공했다고 게시하지 않는다. */

    HRESULT metadata_hr = S_FALSE; /* composition이 없으면 선택 metadata도 없음 */
    if (es->composition != NULL) {
        metadata_hr = es->composition->lpVtbl->GetRange(
            es->composition, &live); /* SetText 뒤 살아 있는 range를 다시 얻는다. */
        if (SUCCEEDED(metadata_hr) && live == NULL)
            metadata_hr = E_UNEXPECTED;
        if (SUCCEEDED(metadata_hr))
            metadata_hr = live->lpVtbl->IsEmpty(live, ec, &empty);
        if (SUCCEEDED(metadata_hr) && empty)
            metadata_hr = E_INVALIDARG;
        if (SUCCEEDED(metadata_hr))
            metadata_hr = SetReadingOptional(
                es->ctx, ec, live, es->reading, es->reading_len);
    }
    if (live != NULL) live->lpVtbl->Release(live);
    TraceMetadataResult(metadata_hr);    /* 입력값이 아닌 HRESULT만 */

    return S_OK; /* metadata는 선택 사항: 수락된 SetText를 가짜 실패로 바꾸지 않음 */
}
```

`SetValue`에는 write cookie와 비어 있지 않은 range가 필요하다. metadata가 제품 의미상 필수라면
위처럼 무시하면 안 되고, **텍스트를 바꾸기 전에** 사전 조건을 확인하거나 검증된 보상 편집을
구현해야 한다. “필수냐 선택이냐”를 먼저 결정하지 않고 HRESULT만 전파하는 것이 v1의 실수였다.
위 교육용 함수는 `IsEmpty`까지만 검사한다. R2/R3 진단판은 별도 bounded length helper의 결과도
event로 남겼으며, validator가 요구하는 그 길이 관측을 `IsEmpty` 하나로 대체하지 않았다.
`SetText == S_OK`도 write 수락이지 최종 반영 증명이 아니므로 이 helper는 mutation을
`UNKNOWN`에 둔다. 호출자는 별도의 호스트별 postcondition이 맞을 때만 `DONE`으로 승격하고 FSM을
publish한다. `UNKNOWN`을 이용해 원래 키 중복 통과를 막는 것과 성공으로 판정하는 것은 별개다.

`make akel-metadata-r2-suite`로 만드는 정정판은 1–4를 구현했다. production initializer와
portable test가 공식 두 GUID 바이트를 공유하고, schema 2의 각 update는 다음 사건을 분리한다.

```text
metadata.range.get / is_empty / length
metadata.langid.get_property / set_value
metadata.reading.get_property / set_value
metadata.summary
```

속성 실패는 `metadata.summary`에 남지만 성공한 `SetText`의 edit session을 실패로 바꾸지 않는다.
따라서 v1처럼 문서를 일부 바꾼 뒤 `pfEaten=FALSE`로 같은 물리 키를 또 보내지 않는다. 수집기는
schema/build/variant, 최소 여섯 update, 비어 있지 않은 range, 예상 속성의 두 HRESULT, session
성공, `hangul_step.failed=0`을 검사해 `validation.json`을 만든다. 하나라도 맞지 않으면 화면이
그럴듯해도 그 셀은 실패다. 이 조건으로 조건 5의 실제 8셀을 판정했다.

그때 판정은 다음처럼 한다.

| 결과 | 해석 |
|---|---|
| 한 단독 변형만 반복해서 성공 | 그 속성이 이 호스트의 후보 필요조건. 다른 앱과 반복 실행 뒤 제품 이식 검토 |
| Both만 성공 | 두 속성의 상호작용 후보. 단독·결합을 각각 다시 실행 |
| 네 개 모두 실패하고 property 설정도 `S_OK` | LANGID/READING은 충분한 해결책이 아님. TEXTOWNER/호스트 경계 등 다음 가설로 이동 |
| property 설정 HRESULT가 실패 | 해당 변형은 가설을 실제로 적용하지 못했으므로 “속성 효과 없음”으로 판정 금지 |
| Control까지 갑자기 성공 | DLL 선택·캐시·앱 버전·시험 순서가 바뀐 것. 원인 결론을 내리지 말고 재현부터 확인 |

`ITfProperty::SetValue`는 비어 있는 range에서 실패하며 write cookie가 필요하다. 이번 로그의
`InWriteSession=TRUE`는 우리 client가 write lock을 가졌다는 뜻일 뿐, range가 비어 있지 않거나
GUID가 정확하다는 보장은 아니다. `GUID_PROP_READING`은 Store 앱에서 지원되지 않으므로 정정판이
성공해도 범용 필수조건으로 만들면 안 된다.

#### 12.7.8 정정 R2·R3 실기 결과와 R4 관찰자 효과 분리

R2의 AkelPad/메모장 8셀은 모두 `validation.json: pass`였다. 즉 각 profile의 정확한 schema,
build, variant, 6회 metadata update, nonempty range, 예상 `GetProperty`/`SetValue`, 성공한
edit session과 `hangul_step.failed=0`이 확인됐다. v1의 잘못된 GUID·부분 편집·원래 키 중복
통과와 달리 실제 metadata 효과를 비교할 수 있는 실행이다.

| Profile | AkelPad 화면·수명 | 메모장 |
|---|---|---|
| Control | 자모 분리, 새 composition 6, 재사용 0, 외부 종료 6 | 정상, 1회 생성·5회 재사용 |
| LANGID | 자모 분리, 새 composition 6, 재사용 0, 외부 종료 6 | 정상, 1회 생성·5회 재사용 |
| Reading | **`가나다`, 1회 생성·5회 재사용, 키별 종료 0** | 정상, 1회 생성·5회 재사용 |
| LANGID → Reading | 자모 분리, 새 composition 6, 재사용 0, 외부 종료 6 | 정상, 1회 생성·5회 재사용 |

실패한 세 AkelPad profile은 우리 transaction이 성공한 뒤 `txn=0` composing 변경과
`OnCompositionTerminated`가 매 키 반복됐다. Reading-only에는 두 사건이 없었다. 따라서 화면만
우연히 맞은 것이 아니라 실제로 같은 composition이 여섯 키를 견뎠다.

여기서 확정되는 범위는 좁다.

- LANGID-only는 이 고정 AkelPad 시나리오의 충분한 해결책이 아니다.
- Reading-only는 이 실행의 유효한 양성 대조군이다.
- LANGID→Reading이 실패했으므로 Reading의 단순 존재만으로 충분하다고 말할 수 없다.
- Reading이 모든 호스트의 필수조건인지, LANGID 자체·적용 순서·매 키 반복 중 무엇이
  종료를 만드는지는 아직 모른다.

이를 분리하려고 R3는 새 identity와 schema 3을 쓰는 네 profile을 만들었다.

| R3 profile | 한 update 안의 metadata plan | 원래 질문 |
|---|---|---|
| Reading | Reading | R2 성공 대조군이 재현되는가 |
| LANGID Reading | LANGID → Reading | R2 실패 대조군이 재현되는가 |
| Reading LANGID | Reading → LANGID | 같은 두 속성에서 순서만 바꾸면 달라지는가 |
| LANGID Once Reading | context 첫 update만 LANGID → Reading, 이후 Reading | 반복 LANGID 쓰기가 종료를 재발시키는가 |

마지막 profile은 “새 composition마다 한 번”이 아니다. LANGID가 composition을 끝내더라도
다음 키에서 LANGID를 다시 쓰지 않도록 context 수명 동안 한 번만 설정한다. 그래야 반복 쓰기와
첫 설정의 지속 효과를 구분할 수 있다.

Schema 3 validator는 정확히 6회 update, profile별 plan 사건, transaction 안의 속성 순서와
호출 횟수, `SetValue` 직후 `GetValue`의 expected-value 일치, nonempty range, 성공한 session을
검사했다. 로그에는 실제 속성값·입력 글자·키값·문서 내용·포인터·경로·명령줄을 쓰지 않았다.

**R3 구조 실기 결과:** 의도한 4 profile × 2 host matrix의 8셀은 모두 validator를 통과했다.
그 matrix와 별도로 AkelPad Reading 반복 1개도 유효했고 같은 수명을 보여, 유효한 현장 캡처는
총 9개다. 앞선 실행 시도 2개는 유효한 새 trace를 만들지 못했으므로 matrix 셀·실패 결과·현장
증거 어느 쪽에도 세지 않는다. 수집 로그에는 정확한 Windows build와 AkelPad 버전 문자열이
들어 있지 않으므로 이 문서는 확인되지 않은 버전 번호를 보충하지 않는다.

| R3 profile | AkelPad 구조 수명 | 메모장 구조 수명 |
|---|---|---|
| Reading | 새 6 / 재사용 0 / 종료 6 | 새 1 / 재사용 5 / 키별 종료 0 |
| LANGID → Reading | 새 6 / 재사용 0 / 종료 6 | 새 1 / 재사용 5 / 키별 종료 0 |
| Reading → LANGID | 새 6 / 재사용 0 / 종료 6 | 새 1 / 재사용 5 / 키별 종료 0 |
| 첫 update만 LANGID → Reading | 새 6 / 재사용 0 / 종료 6 | 새 1 / 재사용 5 / 키별 종료 0 |

모든 최종 셀에서 기대한 property `GetProperty`/`SetValue`와 즉시 `GetValue` 검증은 nonempty
range 안에서 성공했고, session/inner 실패와 `hangul_step.failed`는 없었다. AkelPad에서는
transaction 뒤 text/composing/attribute/reading 변경과 종료가 6회 반복됐고, 메모장에서는
그 키별 종료 지문이 없었다. 수집 시작 때 새 로그가 생기지 않아 실패한 예비 캡처는 **수집
실패**이지 호스트 결과가 아니며, 후속 완결 캡처로 대체했다.

그러나 이 표로 원래 네 질문에 답하면 안 된다. 가장 중요한 Reading 양성 대조군이 R2의 “유지”를
재현하지 못했기 때문이다. 소스상 R3 Reading에는 R2에 없던 두 가지가 함께 추가됐다.

1. 매 `SetValue` 직후 같은 property를 `GetValue`로 읽어 expected type/value를 확인했다.
2. 그 검증과 plan을 기록하는 동기 trace I/O가 편집 경로에 추가됐다.

새 build identity와 실행 절차 차이도 있으므로 `GetValue` 하나가 범인이라고 확정할 수도 없다.
R2에는 있던 pre-context `keyboard.toggle` 사건이 R3에는 없었다. 더구나 제출된 R3 묶음에는
독립적인 화면 문자열 판정이 없으므로 “가나다였다/자모가 보였다”라고 보충해서는 안 된다.
R3가 증명한 것은 **속성 호출과 수집이 구조적으로 완결됐지만, 인과 시험의 양성 대조가
실패했다**는 사실이다.

**R4 설계 — 계측을 원인 변수로 분리한다.** DLL·CLSID·profile identity는 하나로 고정하고,
프로세스 시작 전에 선택한 런타임 모드만 바꾼다. 각 모드는 새 AkelPad 프로세스·새 문서에서
같은 6키 고정 키열로 실행한다. selector는 프로세스당 한 번만 읽고 `SET_ONLY`·`TRACE_ONLY`·
`READ_BACK` 세 값만 허용하며, 원시 환경 문자열은 로그에 남기지 않는다.

| 슬롯 | 모드 | 한 update의 동작 | 판정 목적 |
|---:|---|---|---|
| 0 | Baseline A (`SET_ONLY`) | Reading `GetProperty`/`SetValue`; probe marker 없음 | R2형 Set-only 수명 재현 |
| 1 | Trace control | `probe.before` → Reading `GetProperty`/`SetValue` → `probe.result(S_OK, false)` | 동기 trace I/O 효과 |
| 2 | Read-back | 같은 before와 property 쓰기 → `GetValue` → 같은 result | property read 추가 효과 |
| 3 | Baseline B (`SET_ONLY` 반복) | 슬롯 0과 동일 | 앱/캐시/환경 drift 탐지 |

Trace control과 Read-back은 `GetProperty`/`SetValue`를 같은 marker 위치로 감싸고 event
이름·필드 수·쓰기·flush 정책을 같게 한다. 오직 성공한 `SetValue` 뒤
`ITfProperty::GetValue` 호출 유무만 다르다. Baseline A와 B가 다르면 중간 모드의 인과를
판정하지 않고 환경 재현부터 고친다.

```c
typedef enum R4Mode {
    R4_SET_ONLY,
    R4_TRACE_ONLY,
    R4_READ_BACK
} R4Mode;

typedef struct R4ProbeResult {
    HRESULT get_property_hr;
    HRESULT set_value_hr;
    HRESULT probe_hr;
    BOOL read_called;
    BOOL matched;
} R4ProbeResult;

static void ApplyR4Reading(R4Mode mode, ITfContext *ctx,
                           TfEditCookie ec, ITfRange *range,
                           const VARIANT *value,
                           const VARIANT *expected,
                           R4ProbeResult *out)
{
    ITfProperty *prop = NULL;
    BOOL traced = (mode == R4_TRACE_ONLY || mode == R4_READ_BACK);

    if (out == NULL) return;
    ZeroMemory(out, sizeof(*out));
    out->get_property_hr = out->set_value_hr = out->probe_hr = E_UNEXPECTED;
    if (ctx == NULL || range == NULL || value == NULL || expected == NULL) {
        out->get_property_hr = E_INVALIDARG;
        return;
    }
    if (mode != R4_SET_ONLY &&
        mode != R4_TRACE_ONLY &&
        mode != R4_READ_BACK) {
        out->get_property_hr = E_INVALIDARG;
        return;
    }

    if (traced)
        TraceR4ProbeBefore(S_OK, FALSE); /* 실제 코드와 같이 GetProperty보다 먼저 */

    out->get_property_hr =
        ctx->lpVtbl->GetProperty(ctx, &GUID_PROP_READING, &prop);
    if (FAILED(out->get_property_hr) || prop == NULL) {
        if (SUCCEEDED(out->get_property_hr)) out->get_property_hr = E_UNEXPECTED;
        return;
    }

    out->set_value_hr = prop->lpVtbl->SetValue(prop, ec, range, value);
    if (SUCCEEDED(out->set_value_hr) && traced) {
        VARIANT actual;
        VariantInit(&actual);

        out->probe_hr = S_OK; /* trace-control marker 자체는 구조적으로 성공 */
        if (mode == R4_READ_BACK) {
            out->read_called = TRUE;
            out->probe_hr = prop->lpVtbl->GetValue(
                prop, ec, range, &actual);
            out->matched = SUCCEEDED(out->probe_hr) &&
                           VariantEquals(&actual, expected);
        }

        /* 두 모드가 동일한 event/필드/flush 경로를 탄다. 실제 값은 기록하지 않는다. */
        TraceR4ProbeResult(out->probe_hr, out->matched);
        VariantClear(&actual); /* VT_EMPTY나 GetValue 실패여도 안전 */
    }
    prop->lpVtbl->Release(prop);

    /* out은 validator/진단용이다. 성공한 SetText의 session 결과로 probe 실패를 전파하지 않는다. */
}
```

이 예제에서 `VariantEquals`는 `V_VT`가 같은지 먼저 보고 `VT_I4`는 정수, `VT_BSTR`은 길이와
문자열을 비교하는 작은 helper다. 시험 코드와 validator가 같은 잘못된 기대값을 공유하지 않도록
GUID 바이트와 기대 타입은 Windows SDK/Win32 metadata라는 **독립 oracle**로 고정한다. 호출자는
입력 `value`도 `VariantInit`으로 시작해 BSTR을 만들고, 함수가 돌아오면 `VariantClear`한다.
함수 안의 `actual`만 함수가 소유한다.

실제 시험판은 metadata summary를 남기되, 선택 probe가 실패했다는 이유로 이미 성공한
`SetText`/`SetValue`를 되돌리거나 같은 키를 중복 전달하거나 edit session 실패로 바꾸지 않는다.
Probe 실패는 인과 분석용 **캡처**를 무효로 만들 수 있지만, 이미 일어난 문서 편집을 소급해
실패로 만들 수는 없다.

네 슬롯은 각각 새 64비트 AkelPad 프로세스·새 문서에서 같은 6 update로 실행한다. 구조
validator는 mode, property 연산, marker 순서, session 성공과 수집 완전성을 검사한다. 반면
composition의 생성·재사용·종료 횟수는 **보고값**이지 캡처 형식의 pass/fail 조건이 아니다.
그래야 “종료를 관측한 유효 캡처”를 도구 실패로 버리지 않는다.

아래에서 **유지**는 새 composition 1·재사용 5·키별 외부 종료 0이고, **종료**는 새 composition
6·재사용 0·키별 외부 종료 6이다.

| R4 수명 결과 | 지지되는 해석 |
|---|---|
| 두 baseline 유지, Trace control 유지, Read-back 종료 | 이 호스트 시나리오에서 즉시 read-back 경로(`GetValue` 호출, 반환 `VARIANT` 처리·비교, 그에 추가된 실행 시간)가 묶음으로 격리된 trigger. 이 결과만으로 묶음 내부의 어느 한 요소를 단독 원인으로 나누지는 못함 |
| 두 baseline 유지, Trace control과 Read-back 종료 | 추가 동기 trace/timing만으로 충분하며 read-back 효과는 격리되지 않음 |
| 어느 baseline이든 종료 | 이전 R2 양성 대조가 현재 재현되지 않음. R3 실패를 read-back 탓으로 돌리지 않음 |
| Baseline A와 B가 다름 | 실행 순서 또는 환경 drift가 인과 실행을 무효화 |
| 네 슬롯 모두 유지 | R3 실패는 identity·배포·프로세스 상태 또는 다른 통제되지 않은 조건에 의존 |

이 표는 **시험 전 인과 판정 규칙**이지 R4 실기 결과가 아니다. 네 슬롯의 구조적으로 유효한
현장 캡처가 모두 모이기 전에는 어느 분기도 제품 정책으로 승격하지 않는다.

#### 12.7.9 다음 구현자가 따라야 할 시간 절약 절차

1. **대조군부터 만든다.** 최소 TIP를 메모장처럼 이 시험에서 정상 수명이 확인된 비교 호스트에서
   먼저 통과시킨다. 앱 이름만으로 “native TSF”라고 단정하지 않는다.
2. **고정 입력을 쓴다.** 같은 키열, 새 문서, 새 프로세스, 같은 입력 속도로 비교한다.
3. **화면과 구조 로그를 함께 받는다.** 로그가 정상이어도 사용자가 본 글자가 틀릴 수 있고,
   화면이 맞아도 조합 수명이 우연히 매 키 재생성된 것일 수 있다. 둘 중 하나가 없으면 그 축은
   “미수집”으로 남기고 추정해서 채우지 않는다.
4. **요청·세션·내부 결과를 분리한다.** `RequestEditSession`의 `S_OK` 하나로 성공 판정하지 않는다.
5. **성공 뒤 생존을 기록한다.** callback 끝, edit transaction 닫힘, `OnEndEdit`,
   `OnCompositionTerminated`를 같은 sequence에 둔다.
6. **한 프로필에 한 변수만 바꾼다.** 기능 프로토콜 비교에서는 DLL·CLSID·profile·trace stem을
   분리한다. 계측의 관찰자 효과 비교에서는 반대로 같은 바이너리 identity의 런타임 모드로
   고정해 identity를 변수가 되지 않게 한다.
7. **두 번 같은 이유로 실패하면 가설을 기록하고 이동한다.** 호출 순서만 계속 섞지 않는다.
8. **실험체가 통과하기 전 제품에 이식하지 않는다.** 제품 FSM·UI·설정 문제와 Windows protocol
   문제를 한 디버깅 세션에 섞지 않는다.
9. **배포 identity를 로그에서 확인한다.** 새 schema/event가 없으면 새 DLL을 실행한 것이 아니다.
10. **로그 수집기는 기존 파일을 삭제하지 않는다.** 시작 marker 뒤의 해당 family만 새 폴더로
    복사하고 잠긴 파일은 개별적으로 건너뛴다.
11. **내용은 기록하지 않는다.** event, HRESULT, 길이, boolean, generation, transaction,
    termination epoch만으로 수명 문제를 판정한다.
12. **호스트 한 곳의 실패를 Windows 전체의 한계로 일반화하지 않는다.** “확정”, “강한 추정”,
    “열린 가설”, “기각”을 문서에서 계속 구분한다.
13. **수기 WinAPI GUID는 16바이트를 독립 원본과 비교한다.** 같은 잘못된 상수를 적용과 로그에
    함께 쓰는 자기일치 검사는 금지한다.
14. **edit session 실패를 rollback으로 착각하지 않는다.** 문서를 한 번 바꿨다면 원래 키를
    통과시키기 전에 명시적 복구가 끝났는지 확인한다.
15. **수집 완전성을 검사한다.** activation-only 파일, 빈 profile, 예상 transaction/event가
    없는 셀은 성공적인 수집이 아니다.
16. **양성 대조군이 먼저 재현돼야 한다.** 대조군이 실패하면 다른 변형의 차이가 아무리
    그럴듯해도 인과 결론을 내리지 않는다.
17. **진단 API와 동기 I/O도 한 변수로 센다.** read-back과 trace를 동시에 추가하지 말고,
    trace-only와 read-back을 같은 event 모양으로 맞춘다.

무작위로 다시 시도할 가치가 낮은 항목은 `TF_AE_NONE`, 명시적 `SetSelection` 생략,
insert-first를 **같은 방식으로 단독 반복**하는 것이다. 다시 시험하려면 새로운 관측 필드나
다른 range/ownership 계약처럼 기존 가설과 구분되는 변수가 있어야 한다.

---

## 13. 앱 클래스별 텍스트 주입

§8의 초기 결론은 “확정 삽입이 가장 넓게 통하므로 커밋 전용”이었다. 이후 실기(v0.12)에서 더 날카로운
결론이 나왔다: **`InsertTextAtSelection`조차 만능이 아니다.** 지원 행렬의 서로 다른 앱에
텍스트를 신뢰성 있게 넣으려면 앱 클래스별 주입 전략과 그에 딸린 수정 묶음이 필요했다.
이 장은 그 관찰과 전략을 설명하며, 아직 시험하지 않은 모든 Windows 앱에 대한 보장은 아니다.

### 13.1 ★★핵심 반전 — 일부 호환 RichEdit 컨트롤은 TSF 삽입을 hr=0으로 받고도 버렸다

- **증상**: **호환 경로로 추정된 시험 대상 RichEdit 계열 에디터 컨트롤**에서 한글을 치면 처음 몇 글자는
  되다가 어떤 음절은 **아예 안 나오고**, 같은 글자를 반복하면 **통째로 씹히고**, 4글자 단어를
  블록 선택해 한자 변환하면 **앞 두 글자만** 교체됐다. 시험한 TSF 비교 에디터는 정상이었다.
- **진단(로그)**: 모든 `INSERT` 줄이 `hr=0x00000000`(성공)인데 캐럿 rect가 **고정**돼 있었다
  (`CHIP raw=(14,825…)`가 세 번의 삽입 내내 그대로, adv 보정만 29→58→87). 비교 앱은 rect가
  전진한다. 이 컨트롤은 성공을 보고하고 아무것도 안 움직였다. **삽입이 no-op였다.**
- **관찰**: `InsertTextAtSelection`은 시험한 TSF 비교 호스트와 일부 터미널 경로에서는 반영됐지만,
  **해당 호환 EDIT 컨트롤은 `S_OK`를 반환하면서 조용히 무시했다.** 모든 RichEdit 또는 모든
  터미널이 이 표본과 같다는 뜻은 아니며, 분기할 공통 HRESULT도 없었다.
- **해법 — 앱 클래스와 검증된 capability로 주입 방법을 가른다.** 시험을 통과한 EDIT 계열은
  `EM_REPLACESEL`(EDIT 표준 메시지; 빈 선택 = 캐럿에 삽입)로 구동하고, TSF 반영이 확인된
  비-EDIT 호스트만 TSF 세션을 유지한다.

```c
#include <richedit.h> /* CHARRANGE, EM_EXGETSEL */
#include <limits.h>   /* UINT_MAX */
#include <wctype.h>   /* towlower */
#include <wchar.h>    /* wcsstr */

// 포커스 컨트롤이 EDIT 계열이면 HWND, 아니면 NULL.
// 클래스명에 "edit"가 있어야 인정 — 자체 렌더링 터미널이 우연히 EM_GETSEL에 응답해도
// 에디터로 오판하지 않게 한다(§13.3).
HWND FocusEditWindow(void){
    GUITHREADINFO gti = { sizeof gti };
    if (!GetGUIThreadInfo(0,&gti) || !gti.hwndFocus) return NULL;
    HWND h = gti.hwndFocus;
    wchar_t cls[64]; int n = GetClassNameW(h, cls, 64);
    if (n<=0) return NULL;
    for (int i=0;i<n;i++) cls[i]=towlower(cls[i]);
    if (!wcsstr(cls, L"edit")) return NULL;          // 예: Edit, RICHEDIT50W, RichEdit 계열
    CHARRANGE cr = {-2,-2};
    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);     // RichEdit 계열 컨트롤이 응답
    if (cr.cpMin >= 0 && cr.cpMax >= 0) return h;
    DWORD s=UINT_MAX,e=UINT_MAX;
    SendMessageW(h, EM_GETSEL,(WPARAM)&s,(LPARAM)&e);  // 플레인 EDIT
    return (s!=UINT_MAX && e!=UINT_MAX) ? h : NULL;
}

typedef enum InjectRoute {
    INJECT_EDIT_MESSAGE,   /* 클래스+선택 메시지+실기까지 통과 */
    INJECT_TSF,            /* 해당 비-EDIT 호스트에서 TSF 반영 확인 */
    INJECT_TEXT_KEY_PATH,  /* 해당 자체 렌더러의 텍스트 키 경로 확인 */
    INJECT_UNSUPPORTED
} InjectRoute;

typedef struct CommitOutcome {
    HRESULT hr;
    MutationState mutation;
} CommitOutcome;

// 확정 텍스트 커밋. "EDIT가 아니면 무조건 TSF"로 폴백하지 않는다.
CommitOutcome CommitText(Svc *svc, ITfContext *pic, const wchar_t *str){
    HWND edit = FocusEditWindow();
    InjectRoute route = ClassifyVerifiedRoute(edit, pic);
    CommitOutcome out = { E_NOTIMPL, MUTATION_NONE };

    if (route == INJECT_EDIT_MESSAGE) {
        out.mutation = MUTATION_UNKNOWN; /* dispatch부터 부분 반영 가능 */
        SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)str);
        svc->lastCaretValid = FALSE; /* 오버레이는 시스템 캐럿 */
        out.hr = S_OK;               /* 동기 dispatch 완료; 화면 반영 ack는 아님 */
        return out;
    }
    if (route == INJECT_TSF)
        return RequestEditSessionInsert(svc, pic, str);
    if (route == INJECT_TEXT_KEY_PATH)
        return SendTextThroughVerifiedHostPath(str);
    return out;                      /* 미지원: 아무것도 전달하지 않음 */
}
```

`ClassifyVerifiedRoute`는 클래스명 하나가 아니라 선택 메시지 응답과 제품의 호스트 capability
표를 함께 본다. 비지원 결과는 `MUTATION_NONE`을 유지하므로 호출자는 FSM을 보존하고 손대지 않은
원래 물리 텍스트 키만 통과시킬 수 있다. 반면 `EM_REPLACESEL`은 동기 dispatch 뒤에도 “앱이 최종
화면에 반영했다”는 독립 확인값을 주지 않으므로 `MUTATION_UNKNOWN`이다. 이 경우 원래 키를 다시
통과시키지 않고 소비·정리하는 것은 해당 경로의 **명시적 중복 회피 risk policy**이지 성공
판정이 아니다. 즉 이 분기는 클래스 판정과 앱 실기로 검증한 호환 정책이지 HRESULT 기반 보장이
아니다. `RequestEditSessionInsert`도 `InsertTextAtSelection == S_OK` 직후에는
`MUTATION_UNKNOWN`을 반환해야 하며, §7.2 같은 독립 postcondition을 만족한 지원 route만
`MUTATION_DONE`으로 승격한다. 검증된 키 helper도 같은 `CommitOutcome` 계약으로 실제 반영,
무변경, 미확정을 구분해야 한다.

이 반환형은 **migration 목표 계약**이다. 현재 main 제품의 `CommitText`/`OutputResult`가
outcome을 버리는 동안에는 위 호출자 정책이 보장되지 않으므로, 함수 시그니처부터 모든 호출
지점까지 한 번에 전파한 뒤 완료로 판정한다.

- 이제 세 앱 클래스가 세 주입 방법에 대응한다:

  | 앱 클래스 | 판정 | 확정 텍스트 주입 |
  |---|---|---|
  | TSF 삽입이 반영되는 비-EDIT 비교 호스트 | EDIT 클래스 아님 + 실기 검증 | `InsertTextAtSelection` (TSF 세션) |
  | 시험한 호환 EDIT (플레인 EDIT·RichEdit 계열) | 클래스명에 `edit` + 선택 메시지 응답 | **`EM_REPLACESEL`** |
  | 시험한 자체 렌더링 터미널 | EDIT 클래스 아님 + 터미널 실기 검증 | 해당 호스트에서 검증한 TSF/실제 키 경로 |

- **교훈**: "`S_OK`를 반환했다"는 "반영됐다"가 **아니다.** 한 앱 계열에서 출력이 조용히 실패하면
  API 반환값 신뢰를 멈추고 **문서·캐럿 이동을 관찰해 검증**하라 — 그리고 그 계열은 그 계열의
  네이티브 수단(`EM_REPLACESEL`)으로 구동하라.

### 13.2 조합 중 앱 단축키(Ctrl/Alt/Win) — '테스트 단계에서' 확정

- **증상**: 조합 중 Ctrl+S를 누르면 **마지막 음절 없이** 저장됐다 — 커밋 전용 엔진은 그 음절을
  FSM에만 가지고 있고 문서엔 안 넣었다.
- **1차(오답) 수정**: OnTestKeyDown에서 조합을 eat하고 OnKeyDown에서 음절을 확정한 뒤 **키를
  재전달**(SendInput). 실기에서 네이티브 에디터에 Ctrl+C/V를 연타하니 **ㅊ, ㅍ**이 찍혔다 — 주입한
  `C`/`V`가 이미 큐에 쌓인 사용자 입력(Ctrl 뗌, 다음 키) *뒤에* 붙어 **Ctrl 없이** 처리돼
  자모로 해석됐다. 합성 재전달은 이후 사용자 입력과 근본적으로 경합한다 — **모디파이어 조합에
  절대 부적합.**
- **최종 수정 — 주입 없는 flush-in-test**: 조합 중이고 Ctrl/Alt/Win이 눌렸으면 **바로
  `OnTestKeyDown`에서 음절을 확정(동기 편집 세션)하고 키를 eat하지 않는다.** 그러면 앱이 원래
  이벤트·원래 타이밍으로 단축키를 처리한다. 주입 없음 ⇒ 경합 없음. 부작용은 조기 확정뿐.

```c
// OnTestKeyDown
if (HasCtrlAltWin()){
    if (obj->fsm.state != EMPTY){             // 조합 중 → 복사본에서 동기 확정
        HangulState trial = obj->fsm;
        FsmResult r = { Fsm_Flush(&trial), 0 };
        CommitOutcome out = OutputResultSync(obj, pic, r);
        if (FAILED(out.hr) || out.mutation != MUTATION_DONE) {
            /*
             * DONE이 아니면 trial을 publish하지 않는다.
             * UNKNOWN 소비는 이 단축키 경로의 중복/불완전 저장 회피 정책이지 성공이 아니다.
             */
            ResetTransientComposition(obj);
            TraceOutputFailure(&out);
            obj->consumeTestedKey = TRUE;        // OnKeyDown에서 재시도 없이 TRUE로 소비
            *pfEaten = TRUE;
            return S_OK;
        }
        obj->fsm = trial;                     // 독립 postcondition으로 DONE인 뒤에만 publish
    }
    *pfEaten = FALSE;                         // 단축키는 건드리지 않고 통과
    return S_OK;
}
```

`consumeTestedKey`는 바로 이어지는 `OnKeyDown`에서 한 번 읽고 지운다. 이 실패 경로에서 flush를
다시 실행하거나 `pfEaten=FALSE`로 뒤집으면 Test/실처리 조건이 어긋난다. 정상 경로에만 원래
단축키를 통과시킨다. 특히 `S_OK + MUTATION_UNKNOWN`을 성공으로 취급하지 않는다. UNKNOWN에서
단축키를 한 번 소비하는 것은 검증되지 않은 문서로 Save/Paste가 실행되거나 같은 음절이 중복되는
위험을 피하려는 **이 경로에 한정된 보수적 정책**이다. FSM은 publish하지 않고 상태를
정리·진단하며, 지원 route는 독립 postcondition을 마련해 `DONE`을 반환해야 한다.

### 13.3 경계키 재전달 — 검증된 EDIT 경계에서는 앱 큐(PostMessage)로

- **증상**: 진짜 키여야 하는 경계키(Enter·방향키 — 터미널이 필요로 하고 EDIT는 자동 들여쓰기)가
  시험한 호환 앱에서 앞 음절을 여전히 잃었다. **SendInput 재전달을 30ms 지연해도.**
- **강한 현장 모형**: `SendInput`은 **시스템 입력 큐**로, 그 호환 경로의 확정 문자는 **앱 메시지
  큐**로 간다고 보면 관찰과 맞는다. 서로
  다른 큐라 **순서 보장이 없다** — 지연은 순서를 못 고친다.
- **해법**: 시험한 EDIT 계열 포커스 창에는 허용 목록의 경계키만 **그 창의 메시지 큐로 직접
  게시**(`PostMessage`, WM_KEYDOWN/UP)했다. 이 시험 경로에서는 원하는 순서를 보존했다.
  터미널(비-EDIT)은 정확히 그 호스트에서 별도 검증한 `SendInput` 경로만 유지한다.
  임의 앱·단축키·문자키에 `PostMessage`를 범용 입력 주입 API처럼 쓰지 않는다.

```c
typedef struct BoundaryOutcome {
    HRESULT hr;
    MutationState mutation;
    BOOL down_posted;
    BOOL up_posted;
} BoundaryOutcome;

static HRESULT LastPostError(void)
{
    DWORD error = GetLastError();
    return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE);
}

BoundaryOutcome ResendBoundaryKey(WPARAM vk, LPARAM lp)
{
    BoundaryOutcome out = {
        E_INVALIDARG, MUTATION_NONE, FALSE, FALSE
    };

    if (!IsEditBoundaryKey(vk) || HasCtrlAltWin())
        return out; /* 문자키·Space·수정키 단축키는 이 경로에 넣지 않음 */

    HWND edit = FocusEditWindow();
    if (edit){
        LPARAM base = lp & 0x01FF0000;                        // 스캔코드 + 확장 비트
        out.down_posted = PostMessageW(edit, WM_KEYDOWN, vk, base | 1);
        if (!out.down_posted) {
            out.hr = LastPostError();
            return out;                                      // 아무것도 queue되지 않음
        }

        out.mutation = MUTATION_UNKNOWN;                      // queue됐지만 적용 ack 없음
        out.up_posted = PostMessageW(
            edit, WM_KEYUP, vk, base | 0xC0000001);
        out.hr = out.up_posted ? S_OK : LastPostError();
        if (!out.up_posted)
            TraceBoundaryPostFailure(TRUE, FALSE);
        return out;
    }
    if (IsVerifiedNonEditKeyHost())
        return SendKeyThroughVerifiedHostPath(vk, lp); /* BoundaryOutcome 반환 */

    out.hr = E_NOTIMPL;                                      // 미지원, 무변경
    return out;
}
```

KEYDOWN이 queue된 뒤 KEYUP만 실패하면 결과는 부분/미확정이다. 두 boolean을 기록하고
**현재 이벤트를 재시도하거나 `SendInput`으로 폴백하지 않는다.** 이미 queue된 KEYDOWN을
중복시킬 수 있기 때문이다. `IsEditBoundaryKey`는 Enter·방향키처럼 작은 명시적 허용 목록이어야
한다. Space는 §12.3의 단일 “음절+공백” 텍스트 연산에 남고, Ctrl/Alt/Win 조합은 §13.2처럼 원래
물리 이벤트를 유지한다.

- **시험 경로의 교훈**: 두 연산을 앱 쪽 경로에 둔 것이 효과가 있었고 다른 경로를 지연하는 것은
  효과가 없었다. 이는 현장 증거이지 일반적인 큐 간 FIFO 보장이 아니다.

### 13.4 선택 읽기·교체 — EM_EXGETSEL 사다리

선택 기반 한자(§12.5)는 선택을 읽고, 단어 변환은 캐럿 앞 구간을 교체한다. 둘 다 선택 API가
필요한데 — **일부 RichEdit 계열 컨트롤은 고전 `EM_GETSEL`에 응답하지 않고 RichEdit의 `EM_EXGETSEL`에만
응답한다.** 그래서 모든 선택 연산은 RichEdit 메시지를 먼저, 플레인 EDIT 메시지를 나중에 시도한다:

- **선택 읽기**: `EM_EXGETSEL`+`EM_GETSELTEXT`(컨트롤이 선택 텍스트를 직접 복사 — 오프셋 단위
  무관) → 실패 시 `EM_GETSEL`+`WM_GETTEXT`(플레인 EDIT, 문자 오프셋). 첫 경로가 "대한민국이
  대한으로 잘리던" 것도 고쳤다: `WM_GETTEXT`로 자를 땐 컨트롤과 오프셋 단위 해석이 어긋났는데,
  `EM_GETSELTEXT`는 정확한 텍스트를 그대로 준다.
- **캐럿 앞 단어 교체**(CUAS 호환, 네이티브 전용이던 `ShiftStart` 대체): `EM_EXGETSEL`(폴백
  `EM_GETSEL`)로 캐럿을 얻고, `EM_EXSETSEL`/`EM_SETSEL`로 구간을 선택한 뒤, **읽어서 기대 단어와
  일치하는지 검증**하고 `EM_REPLACESEL`한다. Windows의 `wchar_t` 길이는 이미 UTF-16 code-unit
  수다. 일부 호스트에서 관찰한 2배 오프셋 후보는 “문자 대 UTF-16” 차이가 아니라 해당 컨트롤의
  doubled/byte-offset 의심치일 뿐이므로, 클래스별로 정확한 read-back이 일치할 때만 채택한다.

```c
#define MAX_REPLACE_WORD 64

static BOOL ReplaceBeforeCaretRich(HWND h, const WCHAR *expected,
                                   const WCHAR *replacement)
{
    CHARRANGE saved = { -1, -1 };
    WCHAR got[MAX_REPLACE_WORD * 2 + 1];
    size_t expected_len = wcslen(expected);
    LONG spans[2];

    if (expected_len == 0 || expected_len > MAX_REPLACE_WORD) return FALSE;
    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&saved);
    if (saved.cpMin < 0 || saved.cpMin != saved.cpMax) return FALSE;

    spans[0] = (LONG)expected_len;
    spans[1] = (LONG)expected_len * 2; /* 이 호스트에서만 관찰한 doubled-offset 후보 */

    for (int i = 0; i < 2; ++i) {
        CHARRANGE target;
        if (saved.cpMin < spans[i]) continue;
        target.cpMin = saved.cpMin - spans[i];
        target.cpMax = saved.cpMin;

        SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&target);
        ZeroMemory(got, sizeof(got));
        LRESULT copied = SendMessageW(h, EM_GETSELTEXT, 0, (LPARAM)got);
        got[MAX_REPLACE_WORD * 2] = L'\0';

        if (copied == (LRESULT)expected_len &&
            wmemcmp(got, expected, expected_len) == 0) {
            SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)replacement);
            return TRUE; /* 동기 dispatch 완료; 독립 화면 ack는 아님 */
        }

        /* 잘못 고른 span으로 사용자 선택을 바꿔 둔 채 돌아가지 않는다. */
        SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&saved);
    }
    return FALSE;
}

static BOOL ReplaceBeforeCaretPlainEdit(HWND h, const WCHAR *expected,
                                        const WCHAR *replacement)
{
    DWORD saved_start = UINT_MAX, saved_end = UINT_MAX;
    DWORD verify_start = UINT_MAX, verify_end = UINT_MAX;
    size_t expected_len = wcslen(expected);
    LRESULT text_len;
    WCHAR *all;
    BOOL match = FALSE;

    if (expected_len == 0 || expected_len > MAX_REPLACE_WORD) return FALSE;
    SendMessageW(h, EM_GETSEL, (WPARAM)&saved_start, (LPARAM)&saved_end);
    if (saved_start == UINT_MAX || saved_end == UINT_MAX ||
        saved_start != saved_end || saved_end < expected_len)
        return FALSE;

    text_len = SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);
    if (text_len < 0 || (ULONGLONG)text_len > 1024u * 1024u) return FALSE;
    all = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                    ((SIZE_T)text_len + 1) * sizeof(*all));
    if (all == NULL) return FALSE;

    LRESULT copied = SendMessageW(
        h, WM_GETTEXT, (WPARAM)((SIZE_T)text_len + 1), (LPARAM)all);
    if (copied >= 0 && (ULONGLONG)saved_end <= (ULONGLONG)copied) {
        size_t begin = (size_t)saved_end - expected_len;
        match = (wmemcmp(all + begin, expected, expected_len) == 0);
    }
    HeapFree(GetProcessHeap(), 0, all);
    if (!match) return FALSE;

    DWORD target_start = saved_end - (DWORD)expected_len;
    SendMessageW(h, EM_SETSEL, target_start, saved_end);
    SendMessageW(h, EM_GETSEL, (WPARAM)&verify_start, (LPARAM)&verify_end);
    if (verify_start != target_start || verify_end != saved_end) {
        SendMessageW(h, EM_SETSEL, saved_start, saved_end); /* 실패 시 복원 */
        return FALSE;
    }

    SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)replacement);
    return TRUE; /* 동기 dispatch 완료; 독립 화면 ack는 아님 */
}

static BOOL ReplaceBeforeCaretExact(HWND h, const WCHAR *expected,
                                    const WCHAR *replacement)
{
    CHARRANGE probe = { -1, -1 };
    if (h == NULL || expected == NULL || replacement == NULL) return FALSE;

    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&probe);
    if (probe.cpMin >= 0 && probe.cpMax >= 0)
        return ReplaceBeforeCaretRich(h, expected, replacement);
    return ReplaceBeforeCaretPlainEdit(h, expected, replacement);
}
```

두 경로 모두 **기대 원문이 정확히 일치할 때만** 교체한다. RichEdit의 2배 span은 API 일반
규칙이 아니라 특정 컨트롤에서 관찰한 후보이며, 정확 read-back이 맞지 않으면 선택을 복원하고
폐기한다. 플레인 EDIT 경로도 sentinel을 먼저 넣어 `EM_GETSEL` 무응답을 성공으로 오판하지 않는다.
두 helper의 `TRUE`는 **기대 원문·선택 검증과 메시지 dispatch 성공**일 뿐 최종 document mutation
증명이 아니다. hardening된 호출자는 독립 ack가 없는 이 결과를 `MUTATION_UNKNOWN`으로 올리고,
원래 키를 중복 통과시키지 않는다. 현재 main 제품에는 이 outcome 전파 migration이 남아 있다.

- **교훈**: RichEdit 계열(모던 리치텍스트 에디터·채팅 입력창 등)은 레거시 EDIT 메시지를 무시할 수
  있다 — **판정도 선택도 `EM_EXGETSEL`을 `EM_GETSEL`보다 먼저** 시도하라. 안 그러면 조용히
  깨진 경로로 폴백한다.

### 13.5 갇힌 글자 유령 키 (모아치기 / 코드 자판)

- **증상**: 특정 글자(예: 대)가 **영원히 입력 불가**가 됐다 — 매번 eat되고 갇힌 조합이 미리보기
  칩에 박혔다. 프리뷰를 꺼도 소용없이 그 글자가 *어딘가에* 갇혀 있었다.
- **원인**: 동시입력(모아치기)·코드 상태 머신은 vk별 `keyDown[]` 플래그로 동작한다. **키업이
  유실**되면(조합 중 포커스 이동, 삼켜진 메시지) 플래그가 박히고, 다음 눌림이 **오토리핏**으로
  보여 머신이 키를 eat하고 옛 조합을 **영원히** 재표시하며 확정을 안 한다.
- **시험한 완화책**: "반복" 분기에서 private 플래그와 `GetKeyState`의 메시지-큐 상태를
  비교해 관찰된 한 종류의 유령 플래그를 자가 치유한다. 이것은 현재 물리 키 상태 oracle이
  아니라 지원 호스트와 TIP callback 순서에서 검증한 heuristic이다.

```c
if (c->keyDown[vk]){                       // 반복처럼 보이지만…
    if (GetKeyState(vk) & 0x8000) return REPEAT;   // 이 메시지-큐 view에서는 down
    c->keyDown[vk] = false;                // 시험에서 확인한 stale-flag 추론
    if (c->activeKeys>0) c->activeKeys--;  // …이번 것을 새 눌림으로 처리
}
```

`GetKeyState` 값은 호출 스레드가 키 메시지를 queue에서 읽는 과정에 맞춰 변하며, 현재 hardware
interrupt 상태를 직접 표본화하지 않는다. 위 규칙은 시험한 callback 순서의 키업 유실 사례를
고쳤지만, 포커스 전환·다른 thread의 queue·주입 입력·queue 지연이 있으면 추론이 빗나갈 수 있다.
따라서 포커스 이동/Deactivate의 전체 리셋을 기본 방어로 유지하고, 예상 밖 전이를 기록하며,
지원 호스트마다 검증한다.

비동기 현재 상태가 설계상 정말 필요하면 `GetAsyncKeyState`의 상위 비트를 별도 신호로 검토할
수는 있다. 그래도 호출 순간과 현재 TIP callback 사이에는 race가 있고, 비활성 desktop/UIPI 등은
0을 반환할 수 있으며, 하위 “최근 눌림” 비트는 신뢰할 수 없다. 그러므로 이를 단순 치환하거나
현재 callback이 어느 물리 이벤트에서 왔다는 증명으로 사용하지 않는다.

- 덤으로 탈출구 둘, 모두 단일 리셋 함수(§13.7) 경유: **Esc = 조합 취소**(MS IME 관례; 순차·
  모아치기 공통), **모아치기 조합 중 백스페이스로 비움**(옛 게이트는 `fsm.state`를 봤는데
  모아치기는 그걸 안 세팅한다).

### 13.6 취소 시 원본 보존 — 커밋 *후* 교체

- **증상**: 가를 치고 한자키를 누른 뒤 후보창에서 **선택 없이** 나오면 → 가가 **사라졌다.**
  조합 중 음절은 문서에 없었고, 엔진은 아무것도 없는 곳에 한자를 *삽입*할 계획이었다.
- **해법**: 조합 중 한자키에서 **음절을 문서에 먼저 커밋**(→ `replaceLen=1`, 그 방금 커밋한 글자
  대상)하고, 그것을 *교체*해 변환한다. 취소하면 커밋된 원본이 남는다 — 단어 변환과 같은 패턴.

### 13.7 리셋 단일 진입점 + 대상 창 캡처

위 부류를 예방하기 위한 **필수 hardening 목표** 둘:

- **`ResetComposition()` — 단일 함수**로 FSM·코드 상태·칩 상태를 비우고 오버레이를 숨긴다.
  목표 구조에서는 포커스 이동·Deactivate·Esc·백스페이스 비움·한자 확정이 모두 이 함수를
  호출한다. 프리뷰 *표시*는
  `OutputResult` 한 곳뿐. 정리가 여러 핸들러에 흩어지면 조합이 깨졌을 때 칩이 어딘가에 갇힌다
  (§13.5) — 중앙화가 그 부류를 없앤다.
- **한자키 시점에 대상 EDIT 창을 캡처.** 후보 콜백은 포커스가 옮겨간 *뒤* 발화하므로, 콜백 안에서
  `GetGUIThreadInfo`를 부르면 엉뚱한 창을 얻고 교체가 깨진 TSF 경로로 폴백한다. 후보창을 띄울 때
  `HWND`를 저장하고 콜백에선 저장된 것을 쓴다.

> **현재 제품 상태:** 2026-07-24 main 소스의 `TIP_Deactivate`는 `Fsm_Init`·`Chord_Init`·
> overlay uninitialize를 따로 호출하며 `ResetComposition` 단일 진입점을 쓰지 않는다. 따라서
> last-caret/chip 보정 필드까지 모두 정리된다고 아직 보장할 수 없다. 이 절은 완료 사실이 아니라
> 해당 migration의 acceptance contract다.

> 오버레이는 **여러 글자**를 담을 수 있어야 한다 — 버퍼를 넉넉히 두라(jamotong은 64). 프리뷰는
> 한국어 전용이 아니다: 다른 언어의 여러 키 로마자 시퀀스나 단어 단위 preedit는 한 번에 여러
> 글자를 표시해야 할 수 있다.

### 13.8 상태 소유권 — 전역 vs 서비스별, 그리고 누가 해제하나

현재 main 제품에는 팝업 창·후보 context·사전의 프로세스 전역 상태가 남아 있다. 다음 표와
`CandidateContext_Clear`는 현 상태가 이미 만족한다는 설명이 아니라, service 수명과 지연 callback
사이의 UAF/stale 상태를 막기 위해 **반드시 구현해야 할 규범적 hardening 계약**이다.
2026-07-24 현재 migration은 미완료이며, `pic`만 Release하고 raw `obj`/`targetHwnd`를 남기는
취소 처리는 이 계약을 만족하지 않는다.

| 상태 | 범위 | 생성 | 파괴 | 비고 |
|---|---|---|---|---|
| FSM / chord 컨텍스트 | `CTextService`별 | `Create` | `Deactivate`(`ResetComposition`) | 서비스당 조합 하나 |
| 프리에딧 오버레이 창/글꼴 | 전역 | 첫 `Show` 시 지연 | `Deactivate`(`Uninitialize`) | 입력 스레드 전용; `Hide`는 재사용 |
| 후보창 + `g_CandCtx` | 전역 | 첫 한자키 | **포커스 이동/Deactivate 시 `Cancel`** | `pic`는 AddRef, `obj`/`targetHwnd`는 raw — Cancel 시 반드시 클리어 |
| 코드 입력 팝업 | 전역 | 첫 `Ctrl+Alt+U` | `Deactivate`(`Uninitialize`) | 입력 스레드 전용 |
| 한자/훈음 사전 | 전역 | **첫 한자 사용**(지연) | 프로세스 종료 | 공유 읽기전용; 1회 로드 |
| 설정창(별도 스레드) | 전역 | `SettingsUI_Show` | `Deactivate`가 스레드 조인 | `g_configLock` 아래 live config 편집 |

아래 규칙도 migration 완료 조건이다.
- **한 번 생성, 한 번 파괴.** 모든 팝업은 `Deactivate`에서 도달하는 `Uninitialize`/`Cancel`을
  가진다. raw 서비스 포인터(`g_CandCtx.obj`)를 저장하는 콜백 컨텍스트는 팝업이 닫힐 때 **반드시**
  클리어해야 — 안 그러면 나중 콜백이 해제된 서비스를 역참조한다.
- **입력 스레드 친화성.** 모든 팝업 창은 입력(TIP) 스레드에 산다. 설정 스레드에서 절대 만지지
  마라. 스레드 간 config 접근만이 `g_configLock`이 지키는 유일한 것.
- **호출보다 오래 사는 것은 AddRef.** 후보 콜백은 나중에 발화하므로 그 `pic`을 show 시점에
  AddRef하고 콜백에서 Release한다. 대상 `HWND`도 그 시점에 캡처한다(§13.7) — 콜백이 돌 땐 이미
  포커스가 옮겨갔기 때문.

```c
/* 규범적 목표 helper: current main 제품에 이 동등 계약을 아직 전파해야 한다. */
static void CandidateContext_Clear(CandidateContext *cc)
{
    if (cc == NULL) return;
    if (cc->pic != NULL) {
        cc->pic->lpVtbl->Release(cc->pic);
        cc->pic = NULL;
    }
    cc->obj = NULL;          /* raw: Release 대상은 아니지만 stale pointer 제거 */
    cc->targetHwnd = NULL;   /* HWND도 다음 후보 세션으로 넘기지 않음 */
    cc->fromSelection = FALSE;
    SecureZeroMemory(cc->word, sizeof(cc->word));
}

/* 선택 완료와 취소, focus-change Cancel, Deactivate가 모두 마지막에 호출한다. */
```

---

## 부록 A: jamotong 소스 매핑

| 개념 | 파일 |
|---|---|
| COM 진입점·클래스 팩토리 | `src/dllmain.c` |
| TIP·키 싱크·OnKeyDown | `src/text_service.c` |
| 편집 세션(커밋 전용) | `src/edit_session.c` |
| 등록(프로파일·카테고리) | `src/register.c` |
| 한글 오토마타 | `src/fsm.c`, `src/layout.c`, `src/hangul_layout.c` |
| 한자 사전·후보창 | `src/hanja_dict.c`, `src/candidate_ui.c` |
| 설정 UI·저장 | `src/settings_ui.c`, `src/config.c` |
| 조합 미리보기 오버레이 (§12.1~12.2) | `src/preedit_overlay.c`, `text_service.c`의 OutputResult |
| 트레이 모드 아이콘 (§12.4) | `src/langbar.c` |
| 앱 클래스별 주입 (§13.1)·EDIT 선택 연산 (§13.4) | `src/edit_session.c`의 `EditCtl_*`/`CommitText`, `text_service.c` |
| 유령 키 자가치유 (§13.5)·리셋 진입점 (§13.7) | `src/chord.c`, `src/chord_layout.c`, `text_service.c`의 `ResetComposition` |
| 코드포인트 입력 팝업 (문자명 표시) | `src/code_input.c` |
| 관리 앱 (`.jmt` 편집/검증/설정/TSF 없는 입력 테스트) | `src/tray_app.c` |
| `.jmt` 로더 + 파싱 진단 (`KlayDiag`) | `src/klay.c`, `src/hangul_layout.c`, `src/chord_layout.c` |
| (사망) IMM32 IME 시도 | `src/imm/` |


## 부록 B: 참고 자료

모든 링크는 이 문서를 쓰는 시점에 접속을 확인했다. 공식 문서의 API 계약과 특정 호스트의
현장 동작은 별도 층이다. 본문 §8·§12·§13은 둘이 달랐던 관찰을 시험 범위와 함께 기록하며,
관찰 하나로 Windows 전체 계약을 다시 정의하지 않는다.

### TSF 전반

| 문서 | 링크 |
|---|---|
| Text Services Framework (개요) | https://learn.microsoft.com/en-us/windows/win32/tsf/text-services-framework |
| TSF Architecture (구조) | https://learn.microsoft.com/en-us/windows/win32/tsf/architecture |
| Using Text Services Framework | https://learn.microsoft.com/en-us/windows/win32/tsf/using-text-services-framework |
| TSF Reference (전체 목록) | https://learn.microsoft.com/en-us/windows/win32/tsf/text-services-framework-reference |
| msctf.h (인터페이스 색인) | https://learn.microsoft.com/en-us/windows/win32/api/msctf/ |

### 이 매뉴얼이 쓰는 인터페이스

| 인터페이스 | 쓰임 | 링크 |
|---|---|---|
| `ITfTextInputProcessor` | TIP 본체(Activate/Deactivate) — §3 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itftextinputprocessor |
| `ITfTextInputProcessorEx` | 활성화 플래그 확장 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itftextinputprocessorex |
| `ITfThreadMgr` | 스레드 관리자 — 모든 것의 시작점 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfthreadmgr |
| `ITfKeystrokeMgr` | 키 싱크 붙이기(Advise) — §5 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfkeystrokemgr |
| `ITfKeyEventSink` | 키 수신 — **vtbl 순서 주의** §5.1 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfkeyeventsink |
| `ITfEditSession` | 편집 세션 — §7 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfeditsession |
| `ITfContext` | 문서 컨텍스트 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfcontext |
| `ITfContext::RequestEditSession` | sync 요청과 request/session 결과 구분 — §7.1 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession |
| `ITfInsertAtSelection` | **삽입(커밋 전용의 핵심)** — §8.4 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfinsertatselection |
| `ITfRange` | range 편집의 호스트별 지원 관찰 — §8.2 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfrange |
| `ITfComposition` | 조합 — 일부 호환 호스트의 외부 종료 §8.1 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfcomposition |
| `ITfCompositionSink` | 조합 종료 통보 — sink는 선택 사항이나 실전에서는 권장 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfcompositionsink |
| `ITfCompositionSink::OnCompositionTerminated` | 외부 종료 시점 추적 — §12.6 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcompositionsink-oncompositionterminated |
| `ITfContextComposition` | 조합 시작 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfcontextcomposition |
| `ITfContextComposition::StartComposition` | optional sink와 `S_OK+NULL` 거절 계약 — §8.1 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontextcomposition-startcomposition |
| `ITfTextEditSink::OnEndEdit` | 편집 종료 순서 관측 — §12.6 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itftexteditsink-onendedit |
| `ITfEditRecord::GetTextAndPropertyUpdates` | 내용 없이 텍스트/속성 변경 range 존재 여부 관측 — §12.6 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfeditrecord-gettextandpropertyupdates |
| `ITfContext::InWriteSession` | 알림 시점에 우리 client가 write lock을 가졌는지 판별 — §12.7 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-inwritesession |
| `ITfContext::GetProperty` | predefined/custom property 객체 획득 — §12.7 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-getproperty |
| `ITfProperty::SetValue` | 비어 있지 않은 range에 LANGID/READING 설정 — §12.7 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfproperty-setvalue |
| `ITfReadOnlyProperty::GetValue` | R3 read-back과 R4 관찰자 효과 분리 — §12.7.8 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfreadonlyproperty-getvalue |
| `ITfCompartment::GetValue` | `VT_EMPTY`를 미설정으로 처리 — §10 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcompartment-getvalue |
| `ITfRange::SetText` | 성공한 편집과 이후 실패의 비원자성 조사 — §12.7 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfrange-settext |
| `ITfInsertAtSelection::InsertTextAtSelection` | insert-first A/B와 `TF_IAS_NO_DEFAULT_COMPOSITION` 계약 — §8.1 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinsertatselection-inserttextatselection |
| `TF_SELECTIONSTYLE` | `TF_AE_NONE` 선택 스타일 A/B — §12.6 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/ns-msctf-tf_selectionstyle |
| `ITfInputProcessorProfiles` | 프로파일 등록 — §4.2 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfinputprocessorprofiles |
| `ITfInputProcessorProfileMgr::RegisterProfile` | 최신 프로파일 등록 경로 — §4.2·§10 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-registerprofile |
| `ITfCategoryMgr` | 카테고리 등록 — §4.3 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfcategorymgr |
| `ITfCategoryMgr::RegisterCategory` | 빠뜨리면 목록에 떠도 활성화 안 됨 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcategorymgr-registercategory |
| `ITfFnConfigure` | 설정창 — §9.3 | https://learn.microsoft.com/en-us/windows/win32/api/ctffunc/nn-ctffunc-itffnconfigure |
| `ITfLangBarItemButton` | 언어바 — §9.4 | https://learn.microsoft.com/en-us/windows/win32/api/ctfutb/nn-ctfutb-itflangbaritembutton |
| `ITfDisplayAttributeInfo` | 조합 표시 속성 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfdisplayattributeinfo |
| `ITfDisplayAttributeProvider::GetDisplayAttributeInfo` | 정확한 C vtbl 시그니처 — §10 | https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfdisplayattributeprovider-getdisplayattributeinfo |

### 개념 문서

| 문서 | 링크 |
|---|---|
| Compositions (조합) | https://learn.microsoft.com/en-us/windows/win32/tsf/compositions |
| Edit Sessions (편집 세션) | https://learn.microsoft.com/en-us/windows/win32/tsf/edit-sessions |
| Text Stores (텍스트 저장소) | https://learn.microsoft.com/en-us/windows/win32/tsf/text-stores |
| Predefined Properties (`GUID_PROP_*` 형식·의미) | https://learn.microsoft.com/en-us/windows/win32/tsf/predefined-properties |
| Microsoft Win32 metadata의 TSF GUID 실제 값 | https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/manual/TextServices.Manual.cs |
| `TF_STATUS` (`TF_SS_TRANSITORY`) | https://learn.microsoft.com/en-us/previous-versions/windows/desktop/legacy/ms629192(v=vs.85) |
| Mozilla 1208043 (MS 한글 IME의 insert-first 순서 관측) | https://bugzilla.mozilla.org/show_bug.cgi?id=1208043 |
| AkelEdit 소스(BSD, IMM32 조합 처리 확인) | https://svn.code.sf.net/p/akelpad/codesvn/trunk/akelpad_4/AkelEdit/AkelEdit.c |

### COM (C로 구현하기)

| 문서 | 링크 |
|---|---|
| What Is a COM Interface? | https://learn.microsoft.com/en-us/windows/win32/learnwin32/what-is-a-com-interface- |
| `IClassFactory` | https://learn.microsoft.com/en-us/windows/win32/api/unknwn/nn-unknwn-iclassfactory |
| `DllRegisterServer` | https://learn.microsoft.com/en-us/windows/win32/api/olectl/nf-olectl-dllregisterserver |
| `SysAllocStringLen` (길이가 명시된 BSTR 생성 — §12.7.7) | https://learn.microsoft.com/en-us/windows/win32/api/oleauto/nf-oleauto-sysallocstringlen |
| `VariantClear` (`VARIANT`의 BSTR/interface 정리 — §10·§12.7.8) | https://learn.microsoft.com/en-us/windows/win32/api/oleauto/nf-oleauto-variantclear |

### IMM32·앱 호환 (§8.3, §13)

| 문서 | 링크 |
|---|---|
| Input Method Manager (IMM32) | https://learn.microsoft.com/en-us/windows/win32/intl/input-method-manager |
| `ImmInstallIMEW` (계약과 현장 관찰 구분 §8.3) | https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-imminstallimew |
| `WM_IME_STARTCOMPOSITION` | https://learn.microsoft.com/en-us/windows/win32/intl/wm-ime-startcomposition |
| `EM_EXGETSEL` (선택 읽기 §13.4) | https://learn.microsoft.com/en-us/windows/win32/controls/em-exgetsel |
| `EM_GETSELTEXT` (RichEdit 선택 텍스트) | https://learn.microsoft.com/en-us/windows/win32/controls/em-getseltext |
| `EM_EXSETSEL` (RichEdit 선택 설정) | https://learn.microsoft.com/en-us/windows/win32/controls/em-exsetsel |
| `EM_GETSEL` (플레인 EDIT 선택 읽기) | https://learn.microsoft.com/en-us/windows/win32/controls/em-getsel |
| `EM_SETSEL` (플레인 EDIT 선택 설정) | https://learn.microsoft.com/en-us/windows/win32/controls/em-setsel |
| `EM_REPLACESEL` (검증된 EDIT 확정 삽입) | https://learn.microsoft.com/en-us/windows/win32/controls/em-replacesel |
| `GetGUIThreadInfo` (포커스·캐럿 폴백) | https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguithreadinfo |
| `GetKeyState` (호출 thread의 메시지-큐 키 상태) | https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getkeystate |
| `GetAsyncKeyState` (제약이 있는 비동기 현재 상태) | https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate |
| `GetClassNameW` (EDIT 클래스 판정) | https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclassnamew |
| `UnregisterClassW` (DLL 창 클래스 정리) | https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-unregisterclassw |
| `SendInput` (검증된 비-EDIT 실제 키 경로) | https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput |
| `PostMessageW` (검증된 EDIT 경계키 큐) | https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postmessagew |

### 한글

| 문서 | 링크 |
|---|---|
| 한글 음절 U+AC00–D7A3 (코드표) | https://www.unicode.org/charts/PDF/UAC00.pdf |
| 한글 자모 U+1100–11FF (코드표) | https://www.unicode.org/charts/PDF/U1100.pdf |
| 유니코드 표준 3장 — 한글 결합 규칙 | https://www.unicode.org/versions/latest/ch03.pdf |

조합 공식은 §6에 있다: `0xAC00 + (초성×21 + 중성)×28 + 종성`.

### 도구·예제

| 항목 | 링크 |
|---|---|
| MinGW-w64 (이 프로젝트의 컴파일러) | https://www.mingw-w64.org/ |
| Microsoft SampleIME (공식 TSF IME 구현 예제) | https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/IME/cpp/SampleIME |

### 이 저장소

| 항목 | 위치 |
|---|---|
| 최소 동작 예제 (§0.5) | `examples/minimal-tip/` |
| 전체 구현 | `src/` — 부록 A의 소스 매핑 참고 |
| 변경 이력 | `CHANGELOG.md` |

---

*이 매뉴얼의 커밋 전용·앱별 주입 선택은 20회가 넘는 지원 행렬 실기에서 실패 면적을 줄인
공학적 결론이다. Windows의 보편 불가능성 증명이 아니다. 다른 IME를 만든다면 §2.2의 관찰
범위와 §7·§8·§12.7의 실패 판정법을 함께 읽어라.*
