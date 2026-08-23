#pragma once
// RFC-0012 Phase 1 — compartment 상태 정책 (순수 C, WinAPI 비의존 → 네이티브 테스트 가능).
//
// TSF 는 "한/영 열림"(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)과 "변환 모드"
// (GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, IMM32 IME_CMODE 와 같은 뜻)를 스레드
// 매니저 compartment 에 두라고 정한다. 이 모듈은 **우리 상태 ↔ 그 두 값** 의 매핑과,
// 밖에서(시스템 표시기·앱) 값이 바뀌었을 때 **어느 자판으로 갈지** 를 정한다.
// compartment.c(글루)는 이 결정을 그대로 실행만 한다.
#include <stdbool.h>

// TF_CONVERSIONMODE_* (공식 값, msctf/ctffunc 문서). mingw 헤더에 없어 여기 둔다.
#define JAMO_CMODE_ALPHANUMERIC 0x0000
#define JAMO_CMODE_NATIVE       0x0001
#define JAMO_CMODE_FULLSHAPE    0x0008

// 자판 종류(config.h LayoutType 과 같은 수) → 한글 자판인가.
//   KOREAN_FSM(1)·HANGUL_CUSTOM(4) = 한글. PASSTHROUGH(0)·STATIC_MAP(2)·DLL_PLUGIN(3)·CHORD(5) = 아님.
bool CompState_IsHangulType(int layoutType);

// 현재 상태 → compartment 두 값.
//   passthrough(무간섭) 면 닫힘/영숫자. 한글 자판이면 열림/NATIVE(+전각이면 FULLSHAPE).
void CompState_Values(int layoutType, bool passthrough, bool fullWidth, long *openClose, long *convMode);

// 밖에서 OPENCLOSE 가 바뀌었을 때 갈 자판 인덱스.
//   wantHangul: 새 값(비0=열림=한글). types/enabled: 자판 목록(n개). cur: 현재 인덱스.
//   현재 자판이 이미 그 종류면 cur 그대로. 아니면 cur 다음부터 순환해 처음 만나는 켜진 그 종류.
//   없으면 -1 (바꾸지 않는다 — 그리고 호출자는 우리 값을 다시 발행해 밖과 어긋남을 바로잡는다).
int CompState_PickIndex(const int *types, const bool *enabled, int n, int cur, bool wantHangul);
