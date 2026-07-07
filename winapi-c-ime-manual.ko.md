# WinAPI + C 만으로 Windows 한글 IME 구현하기 — 실전 매뉴얼

**한국어** | [English](winapi-c-ime-manual.md)


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
| **네이티브 TSF** | 최신 메모장, WordPad, 브라우저 | ✅ | ✅ | ✅ |
| **CUAS EDIT 컨트롤** | AkelPad, 옛 Win32 EDIT, 카톡 | ❌ 세션 직후 잘림 | ❌ 누적됨 | ✅ |
| **터미널(자체 렌더)** | PuTTY | ❌ | ❌ | ✅(대략) |

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
  (메모장)만 되고 CUAS EDIT 컨트롤(AkelPad)은 교체가 안 돼 `ㄱ가간가나낟...`처럼 누적.**
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
1. TSF 조합 인라인 → CUAS 앱(PuTTY·AkelPad)에서 매 키 종료. "CUAS 한계"로 판단.
2. `SendInput` 유니코드 append 폴백 → PuTTY는 됐으나 EDIT 컨트롤은 합성입력이 조합처럼 덮어써짐.
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
| (사망) IMM32 IME 시도 | `src/imm/` |


*이 매뉴얼의 결론(커밋 전용)과 그 근거(§8)는 20+회 실기 검증으로 얻은 것이다. 다른 IME를
만든다면 §2.2 표와 §8을 먼저 읽어라 — 그게 몇 주를 아껴 준다.*
