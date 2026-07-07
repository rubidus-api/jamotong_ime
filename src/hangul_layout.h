#pragma once
#include <windows.h>
#include <stdbool.h>
#include "layout.h"   // LayoutResult, JamoType

// 설정파일(.jmt)로 정의하는 사용자 한글 자판. 세벌식 계열(초/중/종성 분리, 직접 종성)과
// 그 결합 규칙(거센소리/된소리/겹모음/겹받침)을 데이터로 표현한다. moachigi=1 이면 동시치기
// (chord) 런타임(chord.c)이 켜져, 여러 글쇠를 함께 눌러 한 음절을 만들고 모두 떼면 확정한다.
// moachigi=0 이면 순차(이어치기) FSM으로 처리한다. 이 파일에는 특정 자판의 배열 데이터를 담지 않는다.

#define HL_MAX_COMBINE 256

typedef struct {
    JamoType type;      // JAMO_CHO / JAMO_JUNG / JAMO_JONG
    int a, b;           // 입력된 두 자모 인덱스
    int result;         // 결합 결과 인덱스
} HangulCombine;

typedef struct HangulLayout {
    wchar_t name[64];
    int moachigi;                    // 1=모아치기(순서무관 결합 선언), 0=이어치기(순차)
    LayoutResult keymap[128];        // ASCII 산출문자 → {type, index}
    HangulCombine combines[HL_MAX_COMBINE];
    int combineCount;
} HangulLayout;

// .jmt 파일에서 로드 (heap 할당, 실패 시 NULL). 소유자가 HangulLayout_Free 로 해제.
HangulLayout *HangulLayout_LoadFromFile(const wchar_t *path);
void HangulLayout_Free(HangulLayout *hl);

// (type, a, b) 결합 조회 — 대응 규칙의 result, 없으면 -1. 모아치기 자판은 순서무관으로 (b,a)도 시도.
int HangulLayout_Combine(const HangulLayout *hl, JamoType type, int a, int b);
