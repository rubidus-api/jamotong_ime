# WinAPI + C 만으로 Windows 한글 IME 구현하기 — 실전 매뉴얼

**한국어** | [English](winapi-c-ime-manual.md)

*최종 갱신: 2026-07-08 (v0.11.0 — §12 "커밋 전용 이후의 실전 교훈" 추가, §10 함정 보강)*


이 문서는 **순수 C(C23)와 Win32 API만으로**(C++·ATL·MFC·프레임워크 없이) Windows용
한글 입력기(IME)를 처음부터 구현하는 방법을, 우리가 `jamotong` 프로젝트에서 실제로
겪은 시행착오와 최종 정답까지 포함해 정리한 것입니다.

대상 독자: WinAPI와 C는 알지만 COM/TSF는 처음인 개발자.
목표: 이 문서만 보고 다른 IME를 밑바닥부터 만들 수 있게 하는 것.

> **한 줄 결론(스포일러)**: Windows에는 입력 시스템이 두 개(TSF·IMM32) 있고 그 사이 다리(CUAS)가
> 부실하다. **모든 앱에서 예외 없이 되는 텍스트 연산은 "커서 위치에 삽입"뿐이다.** 그래서 조합
> 미리보기(밑줄 뜨는 그것)를 포기하고 **완성된 음절만 삽입하는 "커밋 전용"** 이 가장 견고한
> 만능 해법이었다. 왜 그런지가 이 문서의 핵심이다(§8).

---

## 목차
0. [준비물](#0-준비물)
1. [COM 기초 — C로 COM 구현하기](#1-com-기초--c로-com-구현하기)
2. [TSF 지형도 — TSF·IMM32·CUAS](#2-tsf-지형도)
3. [TIP 골격 — 최소 텍스트 서비스](#3-tip-골격)
4. [등록(Registration)](#4-등록)
5. [키 입력 처리](#5-키-입력-처리)
6. [한글 오토마타(조합 로직)](#6-한글-오토마타)
7. [문서에 글자 넣기 — 편집 세션](#7-편집-세션)
8. [★핵심 교훈: CUAS의 벽과 "커밋 전용"](#8-핵심-교훈)
9. [부가 기능: 한자·경계키·설정](#9-부가-기능)
10. [함정 모음(Gotchas)](#10-함정-모음)
11. [최소 IME 체크리스트](#11-최소-체크리스트)
12. [★커밋 전용 '이후'의 실전 교훈](#12-커밋-전용-이후의-실전-교훈)
13. [★★텍스트 주입은 한 가지가 아니다 — 앱 클래스별 전략](#13-앱-클래스별-텍스트-주입)

---

## 0. 준비물

- **컴파일러**: MinGW-w64 (`x86_64-w64-mingw32-gcc`, `i686-w64-mingw32-gcc`). MSVC 없이 됨.
- **결과물**: `.dll` (TSF IME는 in-proc COM 서버 DLL). 32비트 앱을 지원하려면 32/64 둘 다 빌드.
- **핵심 헤더**: `<windows.h>`, `<msctf.h>`(TSF), `<initguid.h>`(GUID 정의), `<olectl.h>`.
- **링크**: `-lole32 -luuid`(COM), TSF 심볼은 `msctf.h` + `-luuid`로 대부분 해결. `-municode` 불요(DLL).
- **빌드 형태**: `gcc -shared -o jamotong.dll *.c jamotong.def -lole32 -luuid -lgdi32 ...`
  - `.def` 파일로 `DllGetClassObject`, `DllCanUnloadNow`, `DllRegisterServer`,
    `DllUnregisterServer`를 export (32비트는 이름 장식 때문에 `.def`가 사실상 필수).

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
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfKeyEventSink)) {
        *ppv = self; This->lpVtbl->AddRef(This); return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
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
| **IMM32** (Input Method Manager) | 레거시(9x) | 옛 방식. Win11은 서드파티 IMM32 IME **등록 자체를 봉쇄**(§8.3) |

- **CUAS** (Cicero Unaware Application Support): 아직 IMM32로 입력받는 앱을 위해, OS가 TSF 조합을
  IMM32 메시지(`WM_IME_*`)로 **번역해 주는 브리지**. 이 브리지가 불완전한 게 모든 고생의 근원.

### 2.2 ★앱은 3부류이고, 각자 되는 게 다르다 (이 표가 이 문서의 심장)

| 앱 부류 | 예 | TSF 조합(밑줄 미리보기) | range 편집(넣은 글자 교체) | 단순 삽입 |
|---|---|:---:|:---:|:---:|
| **네이티브 TSF** | TSF를 온전히 지원하는 에디터·웹 콘텐츠 컨트롤 | ✅ | ✅ | ✅ |
| **CUAS EDIT 컨트롤** | CUAS 브리지로 접근되는 고전 Win32 EDIT·RichEdit 계열 컨트롤 | ❌ 세션 직후 잘림 | ❌ 누적됨 | ✅ |
| **터미널(자체 렌더)** | 표준 에디트 컨트롤 없이 텍스트를 직접 그리는 앱 | ❌ | ❌ | ✅(대략) |

- **네이티브 TSF**만 조합·교체가 다 된다.
- **CUAS/터미널**은 **"커서에 삽입"만** 된다. 이미 넣은 글자를 지우거나 교체하는 건 안 된다.
- **모든 부류의 공통분모 = 삽입 하나.** → §8의 "커밋 전용" 이 여기서 나온다.

---

## 3. TIP 골격

### 3.1 필수 인터페이스 (최소 IME)

| 인터페이스 | 역할 | 필수? |
|---|---|:---:|
| `ITfTextInputProcessor` | `Activate`/`Deactivate` 진입점 | ✅ |
| `ITfKeyEventSink` | 키 입력 처리 | ✅ |
| (클래스 팩토리 `IClassFactory`) | 객체 생성 | ✅ |
| `ITfDisplayAttributeProvider` | 조합 밑줄 색/스타일 | 조합 쓸 때만 |
| `ITfCompositionSink` | 조합이 외부로 종료될 때 알림 | 조합 쓸 때만 |
| `ITfFunctionProvider`+`ITfFnConfigure` | "옵션" 버튼→설정창 | 선택 |
| `ITfThreadMgrEventSink`/`ITfTextEditSink` | 문서 포커스/편집 추적 | 선택 |

> **커밋 전용 IME(§8)라면 `ITfDisplayAttributeProvider`·`ITfCompositionSink`는 필요 없다.**
> 조합을 아예 안 하기 때문이다. jamotong도 최종적으로 이 둘을 뺐다.

### 3.2 Activate / Deactivate

```c
HRESULT STDMETHODCALLTYPE TIP_Activate(ITfTextInputProcessor *This, ITfThreadMgr *ptim, TfClientId tid){
    JamotongTextService *obj = (JamotongTextService*)This;
    obj->threadMgr = ptim; ptim->lpVtbl->AddRef(ptim);   // 보관+AddRef
    obj->clientId  = tid;                                 // 이후 모든 TSF 호출에 이 id 사용

    // 키 이벤트 싱크 등록
    ITfKeystrokeMgr *ksm = NULL;
    ptim->lpVtbl->QueryInterface(ptim, &IID_ITfKeystrokeMgr, (void**)&ksm);
    ksm->lpVtbl->AdviseKeyEventSink(ksm, tid, (ITfKeyEventSink*)&obj->lpVtblKES, TRUE /*fForeground*/);
    ksm->lpVtbl->Release(ksm);
    return S_OK;
}
// Deactivate: UnadviseKeyEventSink → threadMgr Release → 상태 리셋
```

`clientId`(TfClientId)는 **우리 TIP의 신분증**. 편집 세션 요청 등 거의 모든 TSF 호출에 넘긴다.

---

## 4. 등록

IME는 **세 겹**으로 등록된다. `DllRegisterServer`에서 다 처리한다.

### 4.1 COM 서버 등록 (레지스트리)
```
HKCR\CLSID\{our-clsid}\InprocServer32  (기본값) = DLL 전체경로,  ThreadingModel = Apartment
```
- 64비트 DLL은 64비트 하이브(regsvr32 in System32), 32비트는 `HKLM\WOW6432Node\Classes`
  (SysWOW64\regsvr32). **비트수별로 따로 등록**해야 각 비트수 앱에서 로드된다.

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
ITfCategoryMgr *cat; CoCreateInstance(&CLSID_TF_CategoryMgr, ...);
cat->lpVtbl->RegisterCategory(cat, &CLSID_Ours, &GUID_TFCAT_TIP_KEYBOARD, &CLSID_Ours);
// 조합 밑줄 쓸 때만:
cat->lpVtbl->RegisterCategory(cat, &CLSID_Ours, &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, &CLSID_Ours);
// Win11 스토어/immersive 앱 호환 선언:
cat->lpVtbl->RegisterCategory(cat, &CLSID_Ours, &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, &CLSID_Ours);
```

### 4.4 설치 스크립트
- `install.bat`: 관리자 권한 확인 → 파일 unblock(Mark-of-the-Web) → `regsvr32 app.dll`(64) →
  `SysWOW64\regsvr32 app32.dll`(32). 로그오프/재부팅 후 확실히 반영.
- **DLL 파일 잠금 함정**(§10): TSF DLL은 `ctfmon` 등 여러 프로세스에 매핑돼 파일이 잠긴다.
  갱신하려면 uninstall → **로그오프/재부팅** → 덮어쓰기 → install.

---

## 5. 키 입력 처리

### 5.1 OnTestKeyDown / OnKeyDown 이중 구조 (★중요)

TSF는 키를 두 번 준다:
1. **`OnTestKeyDown`**: "이 키 먹을 거야?"를 **예측만** 한다. `*pfEaten`에 답한다. **부작용 금지.**
2. **`OnKeyDown`**: 실제 처리. **단, `OnTestKeyDown`이 `*pfEaten=TRUE`로 답한 키만 호출된다.**

→ **두 함수의 "먹을지 판단" 로직이 반드시 일치**해야 한다. TestKeyDown에서 TRUE라 해놓고
OnKeyDown에서 안 먹으면 키가 사라진다(반대면 OnKeyDown이 아예 안 불림).

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

> FSM은 **네이티브 코드로 유닛테스트가 쉽다**(Windows 불필요). jamotong은 47개 테스트로 검증.

---

## 7. 편집 세션 — 문서에 글자 넣기

### 7.1 편집은 "편집 세션" 안에서만
문서(`ITfContext`)를 수정하려면 **편집 쿠키(edit cookie)** 가 필요하고, 쿠키는
`RequestEditSession`이 콜백으로만 준다:

```c
// 1) ITfEditSession 구현(§1.2) — DoEditSession(ec) 하나가 알맹이
// 2) 요청:
ctx->lpVtbl->RequestEditSession(ctx, clientId, (ITfEditSession*)&es, TF_ES_SYNC|TF_ES_READWRITE, &hr);
// 3) TSF가 우리 DoEditSession(ec)를 콜백 → 그 ec로 문서 수정
```
- `TF_ES_SYNC`: 콜백이 그 자리에서 동기로 실행(권장, 단순). `TF_ES_ASYNCDONTCARE`도 있으나
  CUAS 조합 문제엔 무효였다.

### 7.2 ★커밋 전용 엔진 (jamotong 최종 방식)

**확정된 음절만 `InsertTextAtSelection`으로 삽입한다. 조합 중 음절은 화면에 안 그린다.**

```c
HRESULT DoEditSession(ITfEditSession *This, TfEditCookie ec){
    int cLen = wcslen(data.committed);   // 확정 음절(들)
    if (cLen > 0) {
        ITfInsertAtSelection *ins;
        ctx->lpVtbl->QueryInterface(ctx, &IID_ITfInsertAtSelection, (void**)&ins);
        ITfRange *r = NULL;
        ins->lpVtbl->InsertTextAtSelection(ins, ec, 0, data.committed, cLen, &r);
        if (r) { MoveCaretToEnd(ctx, ec, r); r->lpVtbl->Release(r); }  // 커서를 삽입 뒤로(역순 방지)
        ins->lpVtbl->Release(ins);
    }
    return S_OK;   // preedit(조합중)은 아무것도 안 함
}
```
- 조합 중 음절은 **다음 음절이 시작되거나 경계키(스페이스/엔터)에서** commit이 나올 때 나타난다
  = 한 박자 늦게 보인다. 이게 커밋 전용의 유일한 대가(미리보기 없음).
- **역순 버그 주의**: 확정 후 커서를 삽입 텍스트 **끝으로** 옮기지 않으면(`MoveCaretToEnd`),
  다음 삽입이 앞에 쌓여 `가나다→다나가`가 된다. `InsertTextAtSelection`이 준 range를 `Collapse
  (TF_ANCHOR_END)` 후 `SetSelection`.

### 7.3 (참고) 조합 미리보기를 하려면 — 왜 안 했나
네이티브 TSF 앱만 되는 두 방법:
- **TSF 조합**: `ITfContextComposition::StartComposition`으로 조합 시작 → `ITfRange::SetText`로
  갱신 → 디스플레이 속성(밑줄) 부여. **CUAS 앱에선 조합이 세션 직후 종료된다(§8.1).**
- **제자리 교체**: 조합 음절을 일반 텍스트로 넣고 바뀌면 지우고 다시 넣기(`ShiftStart`+`SetText`).
  **CUAS 앱은 range 편집 미지원 → 누적(`ㄱ가간...`)(§8.2).**

→ 둘 다 CUAS에서 깨져서 **커밋 전용**으로 귀결. 자세한 건 다음 장.

---

## 8. ★핵심 교훈: CUAS의 벽과 "커밋 전용"

이 장이 이 문서의 존재 이유다. jamotong이 **20번 넘는 실기 왕복** 끝에 배운 것.

### 8.1 CUAS 앱에서 TSF 조합은 세션 직후 죽는다
- 우리 조합 설정은 교과서적으로 정확했다: 디스플레이 속성 `TF_ATTR_INPUT`, 카테고리 등록,
  텍스트 삽입 후 조합 시작, `SetSelection` 유무, 동기/비동기, `ITfThreadMgrEventSink`/
  `ITfTextEditSink` 부착 — **전부 시도했지만 무효.**
- 로그로 확인: `StartComposition`은 성공(hr=0)하고 조합 텍스트도 들어가는데, **편집 세션이 끝난
  직후** CUAS가 `OnCompositionTerminated`를 던져 조합을 확정·종료시킨다. 이유는 끝내 특정 못 함
  (문서화 안 된 CUAS 내부 동작, MS IME는 되지만 그 비법은 계측 불가).
- **함정**: `StartComposition`에 `ITfCompositionSink`를 **NULL로 넘기면 `E_INVALIDARG`로 실패**한다
  (MSDN과 달리 sink 필수). 우연히 이걸 실패시켰더니 조합 없이 삽입만 하는 게 CUAS에서 오히려
  깔끔히 됐고 — 그게 "커밋 전용"의 발견이었다.

### 8.2 CUAS 앱은 "range 편집"이 안 된다 (미리보기 불가의 진짜 이유)
- 조합 없이 "제자리 교체"(넣은 `ㄱ`을 지우고 `가`로 바꾸기)로 미리보기를 시도 → **네이티브
  TSF 앱만 되고 CUAS EDIT 컨트롤은 교체가 안 돼 `ㄱ가간가나낟...`처럼 누적.**
- 즉 CUAS 앱은 `ITfRange::ShiftStart`로 뒤로 확장해 `SetText`로 덮는 걸 지원하지 않는다.
  **오직 커서 위치 삽입만 된다.**
- **∴ CUAS 앱에서 인라인 조합 미리보기는 TSF로 구조적으로 불가능하다.**

### 8.3 IMM32 IME로 우회? — Win11이 봉쇄
- "그럼 정식 레거시(IMM32) IME로 만들자"는 자연스런 우회. `.ime`(DLL) 만들어 `ImeInquire`·
  `ImeProcessKey`·`ImeToAsciiEx` 구현까지 했다. 그러나:
  - `ImmInstallIME`는 **최신 Windows에서 사실상 폐기된 no-op**(NULL 반환, GetLastError=0).
  - 레지스트리로 직접 등록하면 설정 목록에 **뜨긴 하나 "음영(비활성)"** → Win11이 서드파티
    IMM32 IME **활성화를 정책적으로 거부.**
  - 부수 발견: `ImmInstallIME`가 IME의 **버전 리소스(VERSIONINFO)** 를 읽는다 — 없으면
    `1813 ERROR_RESOURCE_TYPE_NOT_FOUND`. (그래도 결국 봉쇄라 무의미.)
- **∴ Win11에서 서드파티 IMM32 IME는 막다른 길.**

### 8.4 결론: 삽입만이 만능 → 커밋 전용
세 부류 앱 모두에서 되는 유일한 연산은 **"커서에 삽입"**. 그래서:
- TSF 조합 ❌(CUAS가 죽임) → 안 씀
- range 편집 ❌(CUAS 미지원) → 안 씀
- IMM32 IME ❌(Win11 봉쇄) → 안 씀
- **삽입 ✅ → 확정 음절만 삽입 = 커밋 전용** → 감지·분기·orphan 없이 **모든 앱 일관.**

대가: 조합 중 음절 미리보기(밑줄) 없음. 한글은 조합 중 음절이 보통 마지막 한 글자라 실사용엔
견딜 만하다. **미리보기가 정말 필요하면 네이티브 앱 한정으로 TSF 조합을 따로 켜되, CUAS/터미널은
커밋 전용으로 폴백**해야 하는데 — 그 둘을 구분하려면 "조합을 시작해서 죽는지 보는" 파괴적 감지가
필요하고, 그건 첫 입력을 망가뜨린다(orphan). jamotong은 그 orphan이 싫어 커밋 전용을 택했다.

### 8.5 시행착오 연대기 (요약)
1. TSF 조합 인라인 → CUAS 앱·터미널에서 매 키 종료. "CUAS 한계"로 판단.
2. `SendInput` 유니코드 append 폴백 → 터미널은 됐으나 EDIT 컨트롤은 합성입력이 조합처럼 덮어써짐.
3. IMM32 IME 정식 구현 → Win11 봉쇄(§8.3).
4. NULL-sink 사고로 "조합 없이 삽입"이 CUAS서 됨을 발견.
5. 제자리 교체 미리보기 → CUAS는 range 편집 불가(누적, §8.2).
6. **커밋 전용 확정** → 모든 앱 일관, 완성.

---

## 9. 부가 기능

### 9.1 한자 변환 (커밋 전용 모델)
- 조합 중 음절은 **문서에 없으므로**(커밋 전용), 한자는 **교체가 아니라 삽입**:
  한자키 → 현재 FSM 음절로 사전 조회 → 후보창(자체 팝업 윈도) → 선택 시 **선택 한자를 삽입**
  (`InsertTextAtSelection`) + FSM 리셋. 삽입이라 모든 앱에서 됨.
- 후보창은 포커스를 뺏지 않는 팝업으로 만들고, 키는 IME가 라우팅(후보창 뜨면 모든 키를 소비→
  후보창 핸들러로 전달).
- 단어단위 변환(이미 친 텍스트를 읽어 교체)은 **range 편집이라 네이티브 앱 전용.**
  (모든 앱에서 되는 대안: **먼저 선택하고 한자키** — 선택 교체는 삽입 경로다. §12.5)

### 9.2 비자모 경계키 (스페이스/엔터/방향키)
조합 중 비자모 키가 오면 **현재 음절을 확정(flush)** 하고, 그 키는 **실제 키 이벤트로 재전달**
(`SendInput`, §5.4 매직 표식)한다. 편집 세션 삽입은 터미널에 안 통하지만 실제 키 재전달은
스페이스·엔터·방향키·터미널 모두에서 네이티브로 처리되기 때문.

### 9.3 설정창 열기 (ITfFnConfigure)
- Win11 모던 설정 앱은 서드파티 TIP에 "옵션" 버튼을 **제공하지 않는다**(정책). `ITfFnConfigure`는
  클래식 대화상자용으로만 남는다.
- 실용적 대안: **단축키**(예: Ctrl+Alt+K)로 설정창을 띄우거나, **별도 설정 exe**를 실행해
  공용 설정 파일(`%APPDATA%\App\config.ini`)을 편집·저장하고 IME가 다음 활성화 때 로드.

### 9.4 언어바 아이콘 (ITfLangBarItemButton) — 선택. 최신 Windows에선 잘 안 보임.

---

## 10. 함정 모음 (Gotchas)

- **vtbl 함수 순서/호출규약**: 하나만 틀려도 즉시 크래시. 인터페이스 정의 순서 그대로.
- **`StartComposition` sink NULL 금지**: `E_INVALIDARG`. sink 필수.
- **역순 입력**: 확정 후 커서를 삽입 끝으로 옮겨라(`MoveCaretToEnd`). 안 하면 `가나→나가`.
- **OnTestKeyDown ↔ OnKeyDown 조건 불일치**: 키 유실/미호출.
- **합성입력 재진입**: `SendInput`엔 `dwExtraInfo` 매직 심고 진입부에서 걸러라.
- **DLL 파일 잠금**: TSF DLL은 여러 프로세스에 매핑됨. 갱신은 uninstall→로그오프/재부팅→덮기→install.
  로그 포맷 바꿨는데 옛 포맷 찍히면 = 옛 DLL 로드 중(배포 실패 신호).
- **32/64 둘 다**: 앱 비트수와 DLL 비트수가 일치해야 로드됨. 각각 등록.
- **`RegisterProfile` vs `AddLanguageProfile`**: Win11 트레이 아이콘은 전자라야 뜬다.
- **아이콘 인덱스 음수 규약**: `-리소스ID`. `-1`은 특수값이라 피하라.
- **레지스트리 리다이렉션**: 32비트 프로세스의 `HKCR\CLSID`는 `WOW6432Node`로 감. 등록 위치 확인.
- **유니코드/로케일**: 소스·문자열 전부 UTF-16(`wchar_t`, `-W` API). `.rc`에 한글 넣으려면
  windres 코드페이지 주의(안전하게 ASCII 또는 리소스 문자열 회피).
- **`RequestEditSession`은 동기 콜백**: 문서 수정은 반드시 그 `ec` 안에서만.
- **wide-scanf 변환 지정자**: C 표준에서 `swscanf`의 `%[`/`%c`/`%s`는 `l` 없이는 **narrow(char) 대상**이다.
  MSVCRT만 MS 특유로 wide 취급해 Windows에선 우연히 돌아간다 — `%l[`/`%lc`/`%ls`로 명시하라
  (양쪽 CRT에서 동일 동작·이식 가능).
- **윈도 클래스 소유권**: 클래스는 반드시 **DLL의 hInstance**로 등록하고(EXE 인스턴스로 등록하면
  WndProc과 소유자가 어긋남), **동적 언로드 시 `UnregisterClassW`** 하라(MSDN: DLL 클래스는 자동
  해제 안 됨). 안 하면 재로드 후 낡은 WndProc을 가리키는 클래스로 크래시.
- **`TF_IAS_NOQUERY`는 ppRange를 안 채울 수 있다**: 교체가 필요하면 `TF_IAS_QUERYONLY`로 선택
  range를 얻어 `ShiftStart`+`SetText` 하라(NULL 역참조 방지).
- **락 범위**: `OnTestKeyDown`도 config/레이아웃을 읽는다면 **똑같이 락**을 잡아라. 설정 스레드가
  레이아웃 리소스를 해제하는 순간 키가 오면 UAF다.

---

## 11. 최소 체크리스트

커밋 전용 한글 TSF IME 최소 부품:

- [ ] COM: `IClassFactory`, `DllGetClassObject/CanUnloadNow/RegisterServer/UnregisterServer` + `.def`
- [ ] `ITfTextInputProcessor` (Activate에서 `AdviseKeyEventSink`)
- [ ] `ITfKeyEventSink` (OnTestKeyDown/OnKeyDown 조건 일치, OnSetFocus에서 FSM 리셋)
- [ ] 한글 FSM (2벌식 조합 → `{commit, preedit}`)
- [ ] 편집 세션(`ITfEditSession`) + `InsertTextAtSelection` (확정 음절만, 커서 끝 이동)
- [ ] 비자모 경계키 재전달(`SendInput` + 매직 표식)
- [ ] 등록: COM CLSID + `RegisterProfile` + `RegisterCategory(TIP_KEYBOARD)` + install/uninstall.bat
- [ ] 32/64 두 벌 빌드·등록

**있으면 좋은 것**: 한자 후보창, 설정(단축키/별도 exe), 트레이 브랜딩 아이콘.
**커밋 전용이면 필요 없는 것**: `ITfCompositionSink`, `ITfDisplayAttributeProvider`, 조합/밑줄 로직.

---

## 12. 커밋 전용 '이후'의 실전 교훈

커밋 전용(§8)으로 이야기가 끝나지 않았다. 미리보기·한자·트레이 아이콘을 붙이는 과정에서
**CUAS의 함정이 세 번 더** 나왔다. 각각 "문제 → 원인 → 최종 해법"으로 정리한다.
(전부 v0.9~v0.11에서 실기 로그로 검증된 내용이다.)

### 12.1 조합 미리보기는 '문서 밖'에서 — 플로팅 오버레이
- **문제**: 커밋 전용의 대가로 조합 중 음절이 화면에 안 보인다.
- **해법**: 문서를 건드리지 말고, **IME 소유의 반투명 칩 창을 캐럿 위치에 띄워 직접 그린다**
  (고전 IMM32 "기본 조합창"과 같은 업계 표준 폴백 패턴). 문서 무접촉이라 CUAS 제약과 무관.
- **구현 요점**:
  - 스타일: `WS_POPUP` + `WS_EX_LAYERED|NOACTIVATE|TOPMOST|TOOLWINDOW|TRANSPARENT`(클릭 통과).
  - 반투명은 **균일 알파**(`SetLayeredWindowAttributes`) + 일반 WM_PAINT로. 퍼픽셀 알파
    (`UpdateLayeredWindow`)는 GDI 텍스트가 알파를 안 채워 글자가 사라진다.
  - 캐럿 좌표 폴백 체인: 커밋 삽입과 **같은 편집 세션에서** `GetActiveView`→`GetTextExt` →
    실패 시 `GetGUIThreadInfo`(시스템 캐럿; 옛 EDIT·상당수 터미널이 이걸 설정) → 둘 다 실패면 미리보기만 생략.
  - 입력 스레드에서 lazy 생성, 포커스 이동·Deactivate에서 숨김/파괴.

### 12.2 ★CUAS의 GetTextExt는 "성공하되 낡은 좌표"를 준다
- **문제**: 칩이 직전 확정 글자 위에 겹치거나, 확정 후 **한 박자 늦게** 다음 칸으로 따라온다.
- **원인**: CUAS 앱은 커밋 삽입이 **비동기**다. 같은 세션 안에서 삽입 '직후' `GetTextExt`를 불러도
  **hr=S_OK로 성공하면서 삽입 반영 전의 옛 좌표**를 준다(실패가 아니라서 폴백으로도 못 잡는다!).
  네이티브 앱은 동기라 즉시 전진한 좌표가 온다.
- **최종 해법 — 낡음 탐지(누적 보정)**:
  1. 칩을 그릴 때마다 **원시(raw) rect를 기억**한다.
  2. "이번 이벤트에 **커밋이 있었는데** rect가 직전 원시값과 **동일**" = 낡은 좌표로 판정 →
     전각 1글자 폭(≈줄높이)을 **누적** 보정해 그린다.
  3. rect가 실제로 움직이면 누적을 리셋한다.
  - 누적이라 빠른 타이핑(여러 키 동안 rect 정체)도 맞고, 네이티브에선 rect가 즉시 움직여
    보정이 **한 번도 발동하지 않는다**(오발동 없음).
  - 함정: 비교 기준은 반드시 **원시 rect**로 저장하라(보정된 값을 저장하면 다음 비교가 깨진다).

### 12.3 ★어절 마지막 음절 증발 사건 — 합성 경계키와의 경합
- **문제**: CUAS 앱에서 `대한민국`의 "국", `가나다라`의 "라"처럼 **어절 마지막 음절이 간헐적으로
  소실**된다. 어느 날은 국, 어느 날은 라 — 내용과 무관, 타이밍 의존.
- **진단**: 로그를 심어 보니 **모든 `InsertTextAtSelection`이 S_OK** — IME→CUAS는 매번 성공.
  소실 지점은 항상 "flush(마지막 음절 삽입) + `SendInput` 경계키 재전달" 조합이었다.
- **원인**: 합성 경계키(하드웨어 큐)와 CUAS의 결과 문자 전달(메시지)이 **앱 안에서 경합** —
  경계키가 끼어들면 보류 중인 결과 문자가 버려진다.
- **최종 해법**: **스페이스는 문자다.** 조합 중 스페이스가 오면 재전달하지 말고
  **"음절+공백"을 한 번의 삽입으로** 처리하라(전달 채널이 하나가 되어 경합이 소멸).
  엔터·방향키는 제어키라 재전달을 유지한다(터미널·자동 들여쓰기 등 앱 고유 처리 필요).
- **교훈**: **한 키 이벤트에서 두 전달 채널(편집 세션 삽입 + SendInput)을 섞지 마라.**
  CUAS에서는 그 둘의 순서가 보장되지 않는다.

### 12.4 트레이 입력 표시기의 모드 아이콘 (MS IME의 한/A 같은 것)

트레이에 떠서 IME의 현재 상태를 보여주고 실시간으로 갱신되는 작은 아이콘이다. §4.2의 프로파일
브랜딩 아이콘과는 **다르다** — 이건 TIP이 활성인 동안 추가하는 런타임 *언어바 아이템*이다.
문서만 보면 겁나지만, 실제로는 다섯 조각이다.

**레시피 — 아이콘이 실제로 뜨게 하는 것:**

1. **언어바 아이템 하나를 구현** — `ITfLangBarItemButton`(= `ITfLangBarItem` + 버튼 메서드)을
   노출하는 객체. 실제로 일하는 메서드는 몇 개뿐이고 나머지는 `S_OK`/`E_NOTIMPL` 반환.
2. **TIP 활성화 때 추가하고, 비활성화 때 제거한다.** `ActivateEx`에서 thread manager로부터
   `ITfLangBarItemMgr`를 얻어 `AddItem`, `Deactivate`에서 `RemoveItem`.
   ```c
   ITfLangBarItemMgr *mgr;
   ptim->lpVtbl->QueryInterface(ptim, &IID_ITfLangBarItemMgr, (void**)&mgr);
   mgr->lpVtbl->AddItem(mgr, (ITfLangBarItem*)myLangBarItem);   // Deactivate: RemoveItem
   ```
3. **`GetInfo`에서 중요한 건 딱 두 개** — 아이템의 정체(GUID)와 스타일:
   ```c
   pInfo->guidItem = GUID_LBI_INPUTMODE;                          // ★반드시 이 GUID
   pInfo->dwStyle  = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;  // ★트레이 표시
   pInfo->clsidService = CLSID_Ours;  lstrcpyW(pInfo->szDescription, L"...");
   ```
   - `GUID_LBI_INPUTMODE = {2C77A81E-41CC-4178-A3A7-5F8A987568E6}` — **MinGW 헤더에 없으니
     직접 정의한다.** Win8+ 트레이 표시기는 **guidItem이 이 값인 아이템만 호스팅**하고, 다른
     GUID는 레거시 데스크톱 언어바에만 뜬다(사람들이 하루를 날리는 지점: `AddItem`은 성공하는데
     아무것도 안 뜸).
   - 트레이 표시기에 실제로 올리는 것은 `TF_LBI_STYLE_SHOWNINTRAY` 스타일이다.
4. **`GetIcon`에서 현재 `HICON`을 반환한다.** 셸이 소유·파괴하므로 호출마다 새 아이콘을 만들어
   돌려준다(예: 현재 자판 축약을 그려서). 아직 없으면(예: Deactivate 후) `*phIcon=NULL`+`S_FALSE`.
5. **상태가 바뀌면 셸에 재조회를 요청한다** — `AdviseSink`에서 받은 sink로
   `sink->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT)`. 이 한 번의 호출이 "아이콘 갱신"의 전부다.

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
- **통찰**: 선택이 활성인 상태의 `InsertTextAtSelection`은 **선택을 교체**한다 — 이는 앱이
  '타이핑으로 선택 덮어쓰기'에 쓰는 **삽입 경로**라서 **CUAS에서도 동작**한다.
- **결과**: "텍스트 선택 → 한자키" UX로 **단어 단위 한자 변환을 모든 앱에서** 할 수 있다.
  (커서 앞을 읽어 `ShiftStart`로 교체하는 고전 방식은 네이티브 전용 — §8.2)
- 선택 읽기는 READ 세션에서 `GetSelection`→`GetText`. 후보창은 포커스를 안 뺏으므로(NOACTIVATE)
  선택이 유지되고, 취소 시 문서 무접촉이라 선택도 그대로다.

### 12.6 진단 방법론 — 로깅이 추측을 이긴다
- 조합 사망(§8)도, 음절 증발(§12.3)도, 칩 지연(§12.2)도 전부 **%TEMP% 파일 로그**로 풀렸다.
  찍을 것: vk·FSM 상태·commit/preedit 문자·`INSERT`의 hr과 range 포인터·좌표 rect와 출처.
- **경고**: IME 로그는 **사용자가 모든 앱에서 치는 내용**이 기록된다. 진단 빌드는 배포 금지,
  로그는 테스트 후 즉시 삭제, 로깅 코드는 `#ifdef`로 격리해 릴리스에선 no-op으로.
- **낡은 DLL 함정 재확인**: 증상이 불가해하게 나타났다 사라지면 코드보다 먼저 배포를 의심하라
  (§10의 파일 잠금 — 이 프로젝트에서 두 번이나 유령 증상의 범인이었다).

---

## 13. 앱 클래스별 텍스트 주입

§8은 "삽입만이 만능이라 커밋 전용"으로 결론지었다. 그런데 이후 실기(v0.12)에서 더 날카로운
결론이 나왔다: **`InsertTextAtSelection`조차 만능이 아니다.** *모든* 앱에 텍스트를 확실히
넣으려면 앱 클래스별 주입 전략과 그에 딸린 수정 묶음이 필요했다. 이 장이 그 전략과, 그것을
낳은 사건들이다.

### 13.1 ★★핵심 반전 — RichEdit 계열 컨트롤은 TSF 삽입을 hr=0으로 받고도 버린다

- **증상**: **CUAS 브리지로 접근되는 RichEdit 계열 에디터 컨트롤**에서 한글을 치면 처음 몇 글자는
  되다가 어떤 음절은 **아예 안 나오고**, 같은 글자를 반복하면 **통째로 씹히고**, 4글자 단어를
  블록 선택해 한자 변환하면 **앞 두 글자만** 교체됐다. TSF를 온전히 지원하는 네이티브 에디터는 정상.
- **진단(로그)**: 모든 `INSERT` 줄이 `hr=0x00000000`(성공)인데 캐럿 rect가 **고정**돼 있었다
  (`CHIP raw=(14,825…)`가 세 번의 삽입 내내 그대로, adv 보정만 29→58→87). 네이티브 앱은 rect가
  전진한다. 이 컨트롤은 성공을 보고하고 아무것도 안 움직였다. **삽입이 no-op였다.**
- **원인**: `InsertTextAtSelection`은 네이티브 TSF 앱과 터미널(IMM 브리지 경유)에서는 잘 되지만,
  **일부 CUAS EDIT 컨트롤은 `S_OK`를 반환하면서 조용히 무시한다.** 분기할 에러가 없다.
- **해법 — 앱 클래스로 주입 방법을 가른다.** EDIT 계열 컨트롤은 `EM_REPLACESEL`(EDIT 표준
  메시지; 빈 선택 = 캐럿에 삽입)로 직접 구동하고, 나머지는 TSF 세션을 유지한다.

```c
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
    if (cr.cpMin >= 0) return h;
    DWORD s=~0u,e=~0u; SendMessageW(h, EM_GETSEL,(WPARAM)&s,(LPARAM)&e);  // 플레인 EDIT
    return (s!=~0u) ? h : NULL;
}

// 확정 텍스트 커밋. 모든 커밋 경로가 지나는 단일 진입점.
void CommitText(Svc *svc, ITfContext *pic, const wchar_t *str){
    HWND edit = FocusEditWindow();
    if (edit){ SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)str);  // EDIT 계열: 신뢰성
               svc->lastCaretValid = FALSE; }                        // 오버레이는 시스템 캐럿
    else      RequestEditSessionInsert(svc, pic, str);               // 네이티브/터미널: TSF
}
```

- 이제 세 앱 클래스가 세 주입 방법에 대응한다:

  | 앱 클래스 | 판정 | 확정 텍스트 주입 |
  |---|---|---|
  | 네이티브 TSF (TSF 완전 지원 에디터) | EDIT 클래스 아님 | `InsertTextAtSelection` (TSF 세션) |
  | CUAS EDIT (플레인 EDIT·RichEdit 계열, 브리지 경유) | 클래스명에 `edit` | **`EM_REPLACESEL`** |
  | 자체 렌더링 터미널 (자체 렌더러) | EDIT 클래스 아님 | `InsertTextAtSelection` (IMM 브리지로 통함) |

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
    if (fsm.state != EMPTY){                 // 조합 중 → 지금 동기 확정
        FsmResult r = { Fsm_Flush(&fsm), 0 };
        OutputResult(obj, pic, r);           // §13.1 CommitText 경유
    }
    *pfEaten = FALSE;                         // 단축키는 건드리지 않고 통과
    return S_OK;
}
```

### 13.3 경계키 재전달 — 시스템 큐(SendInput)가 아니라 앱 큐(PostMessage)로

- **증상**: 진짜 키여야 하는 경계키(Enter·방향키 — 터미널이 필요로 하고 EDIT는 자동 들여쓰기)가
  CUAS 앱에서 앞 음절을 여전히 잃었다. **SendInput 재전달을 30ms 지연해도.**
- **원인**: `SendInput`은 **시스템 입력 큐**로, CUAS의 확정 문자는 **앱 메시지 큐**로 간다. 서로
  다른 큐라 **순서 보장이 없다** — 지연은 순서를 못 고친다.
- **해법**: EDIT 계열 포커스 창엔 **그 창의 메시지 큐로 키를 직접 게시**(`PostMessage`,
  WM_KEYDOWN/UP). 같은 큐 FIFO ⇒ 키가 확정 문자 *뒤에* 도착. 터미널(비-EDIT)은 SendInput 유지
  (자체 렌더링 터미널에서 검증).

```c
void ResendKey(WPARAM vk, LPARAM lp){
    HWND edit = FocusEditWindow();
    if (edit){
        LPARAM base = lp & 0x01FF0000;                        // 스캔코드 + 확장 비트
        PostMessageW(edit, WM_KEYDOWN, vk, base | 1);
        PostMessageW(edit, WM_KEYUP,   vk, base | 0xC0000001);
    } else SendKeyThrough(vk, lp);                            // 터미널: 시스템 큐
}
```
- **교훈**: CUAS 순서 경합은 다른 큐를 *지연*해서가 아니라 **같은 큐**를 써서 푼다. (스페이스는
  여전히 §12.3처럼 "음절+스페이스" 단일 삽입이라 재전달 자체를 우회한다.)

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
  일치하는지 검증**하고(오프셋 단위 불신 — 문자/UTF-16 단위 둘 다 시도), `EM_REPLACESEL`.

```c
// 캐럿 위치, RichEdit 우선
CHARRANGE cr = {-2,-2}; LONG caret; BOOL rich;
SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);
if (cr.cpMin>=0 && cr.cpMin==cr.cpMax){ caret=cr.cpMin; rich=TRUE; }
else { DWORD s,e; SendMessageW(h,EM_GETSEL,(WPARAM)&s,(LPARAM)&e);
       if (s!=e) return FALSE; caret=e; rich=FALSE; }
// [caret-span, caret] 선택 → 텍스트 검증 → EM_REPLACESEL(h, TRUE, hanja)
```
- **교훈**: RichEdit 계열(모던 리치텍스트 에디터·채팅 입력창 등)은 레거시 EDIT 메시지를 무시할 수
  있다 — **판정도 선택도 `EM_EXGETSEL`을 `EM_GETSEL`보다 먼저** 시도하라. 안 그러면 조용히
  깨진 경로로 폴백한다.

### 13.5 갇힌 글자 유령 키 (모아치기 / 코드 자판)

- **증상**: 특정 글자(예: 대)가 **영원히 입력 불가**가 됐다 — 매번 eat되고 갇힌 조합이 미리보기
  칩에 박혔다. 프리뷰를 꺼도 소용없이 그 글자가 *어딘가에* 갇혀 있었다.
- **원인**: 동시입력(모아치기)·코드 상태 머신은 vk별 `keyDown[]` 플래그로 동작한다. **키업이
  유실**되면(조합 중 포커스 이동, 삼켜진 메시지) 플래그가 박히고, 다음 눌림이 **오토리핏**으로
  보여 머신이 키를 eat하고 옛 조합을 **영원히** 재표시하며 확정을 안 한다.
- **해법**: "반복" 분기에서 **물리** 키 상태를 대조해 유령 플래그를 자가 치유.

```c
if (c->keyDown[vk]){                       // 반복처럼 보이지만…
    if (GetKeyState(vk) & 0x8000) return REPEAT;   // 실제로 눌림 → 진짜 반복
    c->keyDown[vk] = false;                // 키업 유실 → 유령 플래그 해제
    if (c->activeKeys>0) c->activeKeys--;  // …이번 것을 새 눌림으로 처리
}
```
- 덤으로 탈출구 둘, 모두 단일 리셋 함수(§13.7) 경유: **Esc = 조합 취소**(MS IME 관례; 순차·
  모아치기 공통), **모아치기 조합 중 백스페이스로 비움**(옛 게이트는 `fsm.state`를 봤는데
  모아치기는 그걸 안 세팅한다).

### 13.6 취소 시 원본 보존 — 커밋 *후* 교체

- **증상**: 가를 치고 한자키를 누른 뒤 후보창에서 **선택 없이** 나오면 → 가가 **사라졌다.**
  조합 중 음절은 문서에 없었고, 엔진은 아무것도 없는 곳에 한자를 *삽입*할 계획이었다.
- **해법**: 조합 중 한자키에서 **음절을 문서에 먼저 커밋**(→ `replaceLen=1`, 그 방금 커밋한 글자
  대상)하고, 그것을 *교체*해 변환한다. 취소하면 커밋된 원본이 남는다 — 단어 변환과 같은 패턴.

### 13.7 리셋 단일 진입점 + 대상 창 캡처

위 부류를 통째로 예방한 작은 구조적 수정 둘:

- **`ResetComposition()` — 단일 함수**로 FSM·코드 상태·칩 상태를 비우고 오버레이를 숨긴다.
  포커스 이동·Deactivate·Esc·백스페이스 비움·한자 확정이 모두 이걸 호출한다. 프리뷰 *표시*는
  `OutputResult` 한 곳뿐. 정리가 여러 핸들러에 흩어지면 조합이 깨졌을 때 칩이 어딘가에 갇힌다
  (§13.5) — 중앙화가 그 부류를 없앤다.
- **한자키 시점에 대상 EDIT 창을 캡처.** 후보 콜백은 포커스가 옮겨간 *뒤* 발화하므로, 콜백 안에서
  `GetGUIThreadInfo`를 부르면 엉뚱한 창을 얻고 교체가 깨진 TSF 경로로 폴백한다. 후보창을 띄울 때
  `HWND`를 저장하고 콜백에선 저장된 것을 쓴다.

> 오버레이는 **여러 글자**를 담을 수 있어야 한다 — 버퍼를 넉넉히 두라(jamotong은 64). 프리뷰는
> 한국어 전용이 아니다: 다른 언어의 여러 키 로마자 시퀀스나 단어 단위 preedit는 한 번에 여러
> 글자를 표시해야 할 수 있다.

### 13.8 상태 소유권 — 전역 vs 서비스별, 그리고 누가 해제하나

팝업 창들과 사전은 **프로세스 전역·입력 스레드 전용** 싱글턴이다 — 한 프로세스에 `CTextService`
인스턴스가 몇 개든 칩 창 하나·후보창 하나·사전 하나. 의도적(한 프로세스는 한 번에 한 조합만
보인다)이지만 소유권 규칙이 명시적이지 않으면 갇힌 칩·stale 포인터 부류의 버그(§13.5·§13.7)가
난다. jamotong의 계약:

| 상태 | 범위 | 생성 | 파괴 | 비고 |
|---|---|---|---|---|
| FSM / chord 컨텍스트 | `CTextService`별 | `Create` | `Deactivate`(`ResetComposition`) | 서비스당 조합 하나 |
| 프리에딧 오버레이 창/글꼴 | 전역 | 첫 `Show` 시 지연 | `Deactivate`(`Uninitialize`) | 입력 스레드 전용; `Hide`는 재사용 |
| 후보창 + `g_CandCtx` | 전역 | 첫 한자키 | **포커스 이동/Deactivate 시 `Cancel`** | `pic`는 AddRef, `obj`/`targetHwnd`는 raw — Cancel 시 반드시 클리어 |
| 코드 입력 팝업 | 전역 | 첫 `Ctrl+Alt+U` | `Deactivate`(`Uninitialize`) | 입력 스레드 전용 |
| 한자/훈음 사전 | 전역 | **첫 한자 사용**(지연) | 프로세스 종료 | 공유 읽기전용; 1회 로드 |
| 설정창(별도 스레드) | 전역 | `SettingsUI_Show` | `Deactivate`가 스레드 조인 | `g_configLock` 아래 live config 편집 |

정확성을 지키는 규칙:
- **한 번 생성, 한 번 파괴.** 모든 팝업은 `Deactivate`에서 도달하는 `Uninitialize`/`Cancel`을
  가진다. raw 서비스 포인터(`g_CandCtx.obj`)를 저장하는 콜백 컨텍스트는 팝업이 닫힐 때 **반드시**
  클리어해야 — 안 그러면 나중 콜백이 해제된 서비스를 역참조한다.
- **입력 스레드 친화성.** 모든 팝업 창은 입력(TIP) 스레드에 산다. 설정 스레드에서 절대 만지지
  마라. 스레드 간 config 접근만이 `g_configLock`이 지키는 유일한 것.
- **호출보다 오래 사는 것은 AddRef.** 후보 콜백은 나중에 발화하므로 그 `pic`을 show 시점에
  AddRef하고 콜백에서 Release한다. 대상 `HWND`도 그 시점에 캡처한다(§13.7) — 콜백이 돌 땐 이미
  포커스가 옮겨갔기 때문.

---

## 부록: jamotong 소스 매핑

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


*이 매뉴얼의 결론(커밋 전용)과 그 근거(§8)는 20+회 실기 검증으로 얻은 것이다. 다른 IME를
만든다면 §2.2 표와 §8을 먼저 읽어라 — 그게 몇 주를 아껴 준다.*
