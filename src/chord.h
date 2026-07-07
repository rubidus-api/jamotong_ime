#pragma once
#include <windows.h>
#include <stdbool.h>
#include "hangul_layout.h"

// 모아치기(동시치기) 런타임: 여러 글쇠를 함께 눌러 한 음절을 만들고, 눌린 글쇠가 모두
// 떨어질 때 확정한다(피아노 코드처럼). 자모는 눌린 순서와 무관하게 초/중/종성 자리에 쌓이며,
// 자리별로 결합 규칙(된소리/거센소리/겹모음/겹받침)을 적용한다. Moachigi=1 자판에만 쓰인다.
typedef struct {
    int cho, jung, jong;   // 누적 중인 자모 (-1 = 없음)
    int activeKeys;        // 현재 눌려 있는 자모 글쇠 수
    bool keyDown[256];     // VK → 지금 눌려 있는지 (반복 무시·짝맞춤용)
} ChordContext;

typedef struct {
    wchar_t composing;   // 조합 중 글자 (0 = 없음)
    wchar_t commit;      // 확정 글자 (0 = 없음)
    bool eaten;
} ChordResult;

void Chord_Init(ChordContext *c);
// 자모 글쇠면 누적하고 조합 중 글자를 돌려준다(eaten=true). 아니면 eaten=false.
ChordResult Chord_KeyDown(ChordContext *c, const HangulLayout *hl, UINT vk, wchar_t keyChar);
// 눌렸던 글쇠 해제. 모두 떨어지면 확정 글자를 돌려주고 초기화한다.
ChordResult Chord_KeyUp(ChordContext *c, UINT vk);
