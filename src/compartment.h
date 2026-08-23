#pragma once
// RFC-0012 Phase 1 — TSF compartment 글루 (한/영 상태·변환 모드의 표준 자리).
//
// 무엇을 하나:
//   • 스레드 매니저 compartment GUID_COMPARTMENT_KEYBOARD_OPENCLOSE(0/1) 와
//     GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION(TF_CONVERSIONMODE_*)에 우리 상태를 **발행**.
//     → 시스템 입력 표시기·앱의 IME 상태 조회(ImmGetOpenStatus 경유 CUAS 포함)와 일치한다.
//   • OPENCLOSE 변경 통지(ITfCompartmentEventSink)를 **구독**. 밖(시스템 표시기·앱)이 값을 바꾸면
//     그 종류의 자판으로 전환한다(정책 = comp_state.c, 순수·테스트됨).
//   • 포커스 문맥의 GUID_COMPARTMENT_KEYBOARD_DISABLED 를 **읽어** 존중한다(앱이 끄라면 끈다).
//
// 무엇을 안 하나(의도): HKCU 채널(Passthrough/한 판 병행)은 건드리지 않는다 — RFC-0012 §4,
// 대체물이 실기 PASS 를 받기 전엔 우회로를 지우지 않는다.
// 킬스위치: config.options.useCompartments (config.ini `UseCompartments=0`).
//
// 스레드: 전부 TSF 입력 스레드(STA) 전용. 설정창 스레드에서 부르지 말 것.
#include "jamotong.h"

void Compart_Attach(JamotongTextService *obj);   // TIP_Activate (threadMgr/clientId 설정 뒤)
void Compart_Detach(JamotongTextService *obj);   // TIP_Deactivate (threadMgr 해제 전)
// 현재 자판/무간섭/전각 상태를 발행. 값이 같으면 쓰지 않는다(비용 0) — 포커스 때마다 불러도 된다.
void Compart_Publish(JamotongTextService *obj);
// 포커스 문맥의 KEYBOARD_DISABLED 를 읽어 obj->ctxKeyboardDisabled 에 캐시(TMES_OnSetFocus 에서).
void Compart_ReadContextDisabled(JamotongTextService *obj, ITfContext *pic);
