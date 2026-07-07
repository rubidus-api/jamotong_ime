#pragma once
#include "fsm.h"

typedef struct {
    JamoType type;
    int index;
} LayoutResult;

// 한글 자판 종류 (KOREAN_FSM 레이아웃의 kbdVariant)
#define KBD_DUBEOL 0   // 2벌식 (a~z 자음/모음, 종성은 문맥으로 결정)
#define KBD_SEBEOL 1   // 세벌식 최종 (초/중/종성 키 분리, 직접 종성 입력)

LayoutResult Layout_MapKeyToJamo(wchar_t keyChar, int variant);

// 가상키(VK) → QWERTY 문자 (shift 반영). TSF·IMM32 양쪽에서 자모 매핑 전에 사용. 없으면 0.
wchar_t Layout_QwertyChar(unsigned vk, int shift);

// 드보락(미국) 정적 맵을 charMap[256]에 채운다 (공개 표준 ANSI INCITS 207-1991).
void Layout_FillDvorak(wchar_t *charMap);
wchar_t Layout_ChoToCompatJamo(int choIndex);
wchar_t Layout_JungToCompatJamo(int jungIndex);
wchar_t Layout_JongToCompatJamo(int jongIndex);

int Layout_ChoToJong(int choIndex);
int Layout_CombineJung(int j1, int j2);
int Layout_CombineJong(int jong1, int cho2);
int Layout_CombineJongPair(int jong1, int jong2);   // 세벌식: 직접 종성 2개 → 겹받침 (없으면 -1)
int Layout_CombineCho(int cho1, int cho2);          // 세벌식: 같은 초성 거듭치기 → 된소리 (없으면 -1)
void Layout_SplitJong(int combinedJong, int *outJong1, int *outCho2);
