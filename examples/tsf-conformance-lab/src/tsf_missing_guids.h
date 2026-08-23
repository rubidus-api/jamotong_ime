// tsf-conformance-lab / tsf_missing_guids.h
//
// MinGW-w64 의 msctf.h/libuuid 에 없는 TSF 상수들. 값은 Windows SDK(uuid.lib)에만 있고
// 헤더에는 `EXTERN_C const GUID ...` 선언만 있으므로, 여기서 **평문 const 로** 정의한다
// (제품 `src/register.c` 가 `_J` 접미사로 쓰는 것과 같은 수법. `DEFINE_GUID` 는 `INITGUID`
//  없이는 선언만 되어 링크 에러가 난다).
//
// 접미사 `_L` = lab-local 정의. 제품 코드로 옮길 때는 이름을 다시 정한다.
//
// ★ 값의 출처와 신뢰도를 반드시 같이 적는다 — 잘못된 GUID 는 조용히 "그 기능이 없는 것처럼"
//   동작하고, 그러면 우리는 "Windows 가 지원 안 한다"는 틀린 결론을 문서에 남기게 된다.
#pragma once
#include <windows.h>

// --- 검증됨 (공개 SDK 헤더 파생 자료에서 값 확인, 2026-08-20) -----------------------------
//     출처: MS Learn Predefined Compartments / .NET WPF 공개 소스의 TSF interop 상수.

// GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION — VT_I4, TF_CONVERSIONMODE_* (스레드 매니저 스코프)
static const GUID GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION_L =
    { 0xccf05dd8, 0x4a87, 0x11d7, { 0xa6, 0xe2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c } };

// GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE — VT_I4, TF_SENTENCEMODE_*
static const GUID GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE_L =
    { 0xccf05dd9, 0x4a87, 0x11d7, { 0xa6, 0xe2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c } };

// GUID_COMPARTMENT_TRANSITORYEXTENSION_DOCUMENTMANAGER — IUnknown(ITfDocumentMgr), 문서관리자 스코프
static const GUID GUID_COMPARTMENT_TRANSITORYEXTENSION_DOCUMENTMANAGER_L =
    { 0x8be347f7, 0xc7a0, 0x11d7, { 0xb4, 0x08, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c } };

// GUID_COMPARTMENT_TRANSITORYEXTENSION_PARENT — IUnknown(ITfDocumentMgr), 문서관리자 스코프
static const GUID GUID_COMPARTMENT_TRANSITORYEXTENSION_PARENT_L =
    { 0x8be347f8, 0xc7a0, 0x11d7, { 0xb4, 0x08, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c } };

// --- 값 확인 필요 (같은 계열의 인접 값으로 추정, 실기 확인 전까지 신뢰하지 말 것) ------------
// GUID_COMPARTMENT_TRANSITORYEXTENSION — VT_I4, TF_TRANSITORYEXTENSION_*
// 확인 방법(§README '값 검증'): Windows 에서 MS 한국어 IME 처럼 이 계약을 쓰는 TIP 의
// 레지스트리 카테고리 항목과 대조하거나, SDK 가 있는 기계에서 uuid.lib 심볼과 대조한다.
static const GUID GUID_COMPARTMENT_TRANSITORYEXTENSION_L_UNVERIFIED =
    { 0x8be347f5, 0xc7a0, 0x11d7, { 0xb4, 0x08, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c } };

// --- 프로파일 등록 능력 플래그 (수치 상수 — 공개 문서에 값이 있다) --------------------------
#ifndef TF_IPP_CAPS_SECUREMODESUPPORT
#define TF_IPP_CAPS_SECUREMODESUPPORT 0x00000002
#endif
#ifndef TF_IPP_CAPS_UIELEMENTENABLED
#define TF_IPP_CAPS_UIELEMENTENABLED  0x00000004
#endif
#ifndef TF_IPP_CAPS_COMLESSSUPPORT
#define TF_IPP_CAPS_COMLESSSUPPORT    0x00000008
#endif

#ifndef TF_TMAE_UIELEMENTENABLEDONLY
#define TF_TMAE_UIELEMENTENABLEDONLY  0x00000004
#endif
#ifndef TF_TMAE_COMLESS
#define TF_TMAE_COMLESS               0x00000008
#endif

#ifndef TF_TRANSITORYEXTENSION_NONE
#define TF_TRANSITORYEXTENSION_NONE        0x0000
#define TF_TRANSITORYEXTENSION_FLOATING    0x0001
#define TF_TRANSITORYEXTENSION_ATSELECTION 0x0002
#endif

#ifndef TF_CONVERSIONMODE_ALPHANUMERIC
#define TF_CONVERSIONMODE_ALPHANUMERIC 0x0000
#define TF_CONVERSIONMODE_NATIVE       0x0001
#define TF_CONVERSIONMODE_KATAKANA     0x0002
#define TF_CONVERSIONMODE_FULLSHAPE    0x0008
#define TF_CONVERSIONMODE_ROMAN        0x0010
#define TF_CONVERSIONMODE_CHARCODE     0x0020
#define TF_CONVERSIONMODE_SOFTKEYBOARD 0x0080
#define TF_CONVERSIONMODE_NOCONVERSION 0x0100
#define TF_CONVERSIONMODE_SYMBOL       0x0400
#define TF_CONVERSIONMODE_FIXED        0x0800
#endif

// --- MinGW msctf.h 에 없는 인터페이스: ITfTextInputProcessorEx --------------------------
// IID 는 Windows SDK `msctf.idl` 의 uuid 속성에서 확인했다(6e4e2102-f9cd-433d-b496-303ce03a6507).
// 상속: ITfTextInputProcessor (Activate/Deactivate) + ActivateEx. **vtbl 순서를 지킬 것** —
// 상속 인터페이스의 vtbl 은 부모 메서드가 먼저다(제품 언어바 T010 에서 같은 함정을 이미 겪었다).
#ifndef __ITfTextInputProcessorEx_INTERFACE_DEFINED__
#define __ITfTextInputProcessorEx_INTERFACE_DEFINED__

typedef struct ITfTextInputProcessorEx ITfTextInputProcessorEx;

typedef struct ITfTextInputProcessorExVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ITfTextInputProcessorEx *, REFIID, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(ITfTextInputProcessorEx *);
    ULONG   (STDMETHODCALLTYPE *Release)(ITfTextInputProcessorEx *);
    HRESULT (STDMETHODCALLTYPE *Activate)(ITfTextInputProcessorEx *, ITfThreadMgr *, TfClientId);
    HRESULT (STDMETHODCALLTYPE *Deactivate)(ITfTextInputProcessorEx *);
    HRESULT (STDMETHODCALLTYPE *ActivateEx)(ITfTextInputProcessorEx *, ITfThreadMgr *,
                                            TfClientId, DWORD);
} ITfTextInputProcessorExVtbl;

struct ITfTextInputProcessorEx { const ITfTextInputProcessorExVtbl *lpVtbl; };

static const IID IID_ITfTextInputProcessorEx_L =
    { 0x6e4e2102, 0xf9cd, 0x433d, { 0xb4, 0x96, 0x30, 0x3c, 0xe0, 0x3a, 0x65, 0x07 } };

#endif // __ITfTextInputProcessorEx_INTERFACE_DEFINED__
