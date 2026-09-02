#pragma once
// RFC-0013 C — preserved key 매핑 정책 (순수 C, WinAPI 비의존 → 네이티브 테스트 T017).
//
// 우리 단축키(ShortcutKey: vKey + SMOD_* 좌우 무관 모디파이어)를 TSF `TF_PRESERVEDKEY`
// (uVKey + TF_MOD_*)로 옮길 수 있는지, 옮기면 어떤 값인지 정한다. 글루(preserved.c)는
// 이 결정을 그대로 등록만 한다.
//
// 등록 불가(=기존 key sink 경로 폴백) 규칙:
//   · SMOD_GUI(Win) 조합 — TSF TF_MOD_* 에 Windows 키가 없다.
//   · 트리거 자체가 맨 모디파이어(VK_SHIFT/CTRL/MENU/WIN 계열, 예: "오른쪽 Alt만") —
//     TF_PRESERVEDKEY 는 "키+모디파이어" 모델이라 모디파이어 단독을 표현하지 못한다.
#include <stdbool.h>

// TF_MOD_* 공식 값 (msctf.h 와 동일 — 순수 모듈이라 여기 복제, 값 출처 msctf.h 614-623행)
#define JAMO_TFMOD_ALT     0x0001u
#define JAMO_TFMOD_CONTROL 0x0002u
#define JAMO_TFMOD_SHIFT   0x0004u

// vKey(SMOD_* smods 포함)를 TSF preserved key 로 옮긴다.
//   성공: true, *uVKey/*uModifiers 채움 (좌우 무관 모디파이어 → TF_MOD_ALT/CONTROL/SHIFT).
//   불가: false (GUI 조합 / 맨 모디파이어 트리거) — 호출자는 key sink 경로에 남긴다.
bool PreservedMap_Build(unsigned vKey, unsigned smods, unsigned *uVKey, unsigned *uModifiers);

// 트리거 키가 맨 모디파이어인가 (VK_SHIFT/CONTROL/MENU 16..18, VK_LWIN/RWIN 91/92, 좌우 160..165).
bool PreservedMap_IsBareModifier(unsigned vKey);
