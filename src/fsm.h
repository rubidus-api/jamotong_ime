#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>

// 한글 유니코드 베이스
#define HANGUL_BASE 0xAC00
#define HANGUL_CHO_COUNT 19
#define HANGUL_JUNG_COUNT 21
#define HANGUL_JONG_COUNT 28

// 자소 종류
typedef enum {
    JAMO_NONE = 0,
    JAMO_CHO,
    JAMO_JUNG,
    JAMO_JONG
} JamoType;

// FSM 상태
typedef enum {
    STATE_EMPTY = 0,
    STATE_CHO,
    STATE_JUNG,
    STATE_CHO_JUNG,
    STATE_CHO_JUNG_JONG
} FsmState;

// 오토마타 전이 결과
typedef struct {
    wchar_t commitChar; // 완성되어 애플리케이션으로 넘길 글자 (0이면 없음)
    wchar_t preeditChar; // 현재 조합 중인 글자 (0이면 없음)
    bool eaten;         // IME가 이 키 입력을 소비했는지 여부
} FsmResult;

// 오토마타 컨텍스트
typedef struct {
    FsmState state;
    int cho;   // 0 ~ 18
    int jung;  // 0 ~ 20
    int jong;  // 0 ~ 27
} FsmContext;

// FSM 함수 (variant: 내장 자판 — 0=2벌식, 1=세벌식 최종. layout.h의 KBD_* 참조)
// hl != NULL 이면 설정파일로 로드된 사용자 자판(HangulLayout)을 쓴다(세벌식 계열·직접 종성).
struct HangulLayout;
void Fsm_Init(FsmContext *ctx);
FsmResult Fsm_ProcessKey(FsmContext *ctx, wchar_t keyChar, int variant, const struct HangulLayout *hl);
// 백스페이스: 조합 중 마지막 자모 제거. 조합이 있었으면 true(소비)와 새 조합 글자(*outPreedit,
// 0이면 조합 비움), 없었으면 false(앱에 위임).
bool Fsm_Backspace(FsmContext *ctx, wchar_t *outPreedit);
// 현재 조합을 확정 문자로 만들고 상태를 비운다(부분 상태 초성만/중성만도 올바르게 처리).
// 조합이 없으면 0. 비자모 키·스페이스·포커스 이동 등에서 조합을 확정할 때 쓴다.
wchar_t Fsm_Flush(FsmContext *ctx);
wchar_t ComposeHangul(int cho, int jung, int jong);
