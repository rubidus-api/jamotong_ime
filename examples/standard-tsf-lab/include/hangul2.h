/* hangul2.h — 두벌식 현대 한글 최소 FSM (RFC-0009 §5.2, §6)
 *
 * 순수 함수. Windows도 TSF도 모른다. 그래서 리눅스에서도 그대로 테스트된다.
 * 제품 src/fsm.c 와 의도적으로 공유하지 않는다 — 같은 helper를 쓰면 같은 버그가
 * 양쪽에 생겨 대조군의 가치가 사라진다(RFC-0009 §3).
 */
#ifndef LAB_HANGUL2_H
#define LAB_HANGUL2_H

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#  include <windows.h>          /* WCHAR, UINT */
#else                            /* portable test용 최소 정의 */
typedef uint16_t WCHAR;
typedef unsigned int UINT;
#endif

/* ── 초성 19 (인덱스는 유니코드 조합 순서) ── */
enum {
    L_G, L_GG, L_N, L_D, L_DD, L_R, L_M, L_B, L_BB, L_S,
    L_SS, L_NG, L_J, L_JJ, L_C, L_K, L_T, L_P, L_H, L_COUNT
};
/* ── 중성 21 ── */
enum {
    V_A, V_AE, V_YA, V_YAE, V_EO, V_E, V_YEO, V_YE, V_O, V_WA,
    V_WAE, V_OE, V_YO, V_U, V_WO, V_WE, V_WI, V_YU, V_EU, V_UI,
    V_I, V_COUNT
};
/* ── 종성 28 (0 = 없음) ── */
enum {
    T_NONE, T_G, T_GG, T_GS, T_N, T_NJ, T_NH, T_D, T_R, T_RG,
    T_RM, T_RB, T_RS, T_RT, T_RP, T_RH, T_M, T_B, T_BS, T_S,
    T_SS, T_NG, T_J, T_C, T_K, T_T, T_P, T_H, T_COUNT
};

typedef enum JamoKind {
    JAMO_NONE = 0,
    JAMO_INITIAL,
    JAMO_VOWEL
} JamoKind;

typedef struct JamoKey {
    JamoKind kind;
    int      index;      /* JAMO_INITIAL이면 L_*, JAMO_VOWEL이면 V_* */
} JamoKey;

/* 조합 단계 하나. Backspace가 유니코드를 역산하지 않고 이 history를 되감는다
   (RFC-0009 §6.3 — Shift+R로 만든 ㄲ와 R 두 번으로 만든 ㄲ는 코드포인트가 같아도
   사용자가 누른 단계가 다르다). */
typedef struct HangulKeyStep {
    JamoKind kind;
    int      index;
} HangulKeyStep;

#define LAB_HISTORY_MAX 8

/* preedit 한 음절의 상태. 확정된 텍스트는 여기 담지 않는다. */
typedef struct HangulState {
    int  initial;        /* L_*  또는 -1 */
    int  vowel;          /* V_*  또는 -1 */
    int  final;          /* T_*  (T_NONE = 없음) */
    HangulKeyStep history[LAB_HISTORY_MAX];
    uint8_t       history_len;
} HangulState;

/* 키 하나가 문서에 일으키는 변화 + 다음 상태 (RFC-0009 §5.2) */
typedef struct HangulStep {
    HangulState next;
    WCHAR   committed[4];
    uint8_t committed_len;
    WCHAR   preedit[4];
    uint8_t preedit_len;
    bool    handled;
} HangulStep;

void Hangul2_Reset(HangulState *st);
bool Hangul2_IsEmpty(const HangulState *st);

/* 두벌식 매핑. locale 변환이 아니라 virtual key + Shift를 직접 본다 —
   Caps Lock이나 dead-key 상태에 영향받지 않는다(RFC-0009 §6.1). */
JamoKey Hangul2_MapDubeolsik(UINT virtual_key, bool shift);

/* 순수 함수. 상태를 바꾸지 않는다. 호출자는 edit transaction이 성공한 경우에만
   out->next를 컨텍스트 상태에 복사한다(RFC-0009 §5.2). */
bool Hangul2_Step(const HangulState *current, UINT virtual_key, bool shift,
                  HangulStep *out);
bool Hangul2_Backspace(const HangulState *current, HangulStep *out);

/* 현재 상태를 preedit 문자열로. 조합 중이 아니면 0을 돌려준다. */
uint8_t Hangul2_Preedit(const HangulState *st, WCHAR *out, uint8_t cap);

/* 테스트가 표를 전수 순회할 수 있게 노출한다(RFC-0009 §6.2). */
typedef struct PairMap { uint8_t first, second, combined; } PairMap;
extern const PairMap  kCompoundVowels[];
extern const unsigned kCompoundVowelCount;
extern const PairMap  kCompoundFinals[];
extern const unsigned kCompoundFinalCount;

/* 초성↔종성 변환 (도깨비불에 필요) */
int Hangul2_InitialToFinal(int initial);   /* 없으면 -1 */
int Hangul2_FinalToInitial(int final);     /* 없으면 -1 */

WCHAR Hangul2_Compose(int l, int v, int t);

#endif /* LAB_HANGUL2_H */
