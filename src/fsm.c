#include "fsm.h"
#include "layout.h"
#include "hangul_layout.h"

// 내장 variant / 사용자 자판(hl) 공통 디스패치
static LayoutResult fsm_map(wchar_t k, int variant, const HangulLayout *hl) {
    if (hl) {
        if ((unsigned)k < 128) return hl->keymap[(int)k];
        LayoutResult n = {JAMO_NONE, 0}; return n;
    }
    return Layout_MapKeyToJamo(k, variant);
}
static int fsm_combineCho(int a, int b, int variant, const HangulLayout *hl) {
    if (hl) return HangulLayout_Combine(hl, JAMO_CHO, a, b);
    return (variant == KBD_SEBEOL) ? Layout_CombineCho(a, b) : -1;
}
static int fsm_combineJung(int a, int b, const HangulLayout *hl) {
    if (hl) return HangulLayout_Combine(hl, JAMO_JUNG, a, b);
    return Layout_CombineJung(a, b);
}
static int fsm_combineJongPair(int a, int b, const HangulLayout *hl) {
    if (hl) return HangulLayout_Combine(hl, JAMO_JONG, a, b);
    return Layout_CombineJongPair(a, b);
}

// 초/중/종성 인덱스를 유니코드 완성형 한글로 변환
wchar_t ComposeHangul(int cho, int jung, int jong) {
    if (cho < 0 || jung < 0) return 0;
    return HANGUL_BASE + (cho * 21 * 28) + (jung * 28) + (jong < 0 ? 0 : jong);
}

void Fsm_Init(FsmContext *ctx) {
    ctx->state = STATE_EMPTY;
    ctx->cho = -1;
    ctx->jung = -1;
    ctx->jong = -1;
}

FsmResult Fsm_ProcessKey(FsmContext *ctx, wchar_t keyChar, int variant, const HangulLayout *hl) {
    FsmResult res = {0, 0, false};
    // 직접 종성 자판(세벌식/사용자)은 2벌식식 도깨비불(초성↔종성 문맥 전환)을 쓰지 않는다.
    bool directJong = (hl != NULL) || (variant == KBD_SEBEOL);

    LayoutResult layoutRes = fsm_map(keyChar, variant, hl);

    if (layoutRes.type == JAMO_NONE) {
        res.commitChar = Fsm_Flush(ctx);   // 조합 중이면 확정(부분 상태 포함), 아니면 0
        res.eaten = false;
        return res;
    }

    res.eaten = true;

    switch (ctx->state) {
        case STATE_EMPTY:
            if (layoutRes.type == JAMO_CHO) {
                ctx->state = STATE_CHO; ctx->cho = layoutRes.index;
                res.preeditChar = Layout_ChoToCompatJamo(ctx->cho);
            } else if (layoutRes.type == JAMO_JUNG) {
                ctx->state = STATE_JUNG; ctx->jung = layoutRes.index;
                res.preeditChar = Layout_JungToCompatJamo(ctx->jung);
            } else {   // JAMO_JONG: 홀 받침 → 자모 그대로 출력
                res.commitChar = Layout_JongToCompatJamo(layoutRes.index);
            }
            break;

        case STATE_CHO:
            if (layoutRes.type == JAMO_CHO) {
                int tense = fsm_combineCho(ctx->cho, layoutRes.index, variant, hl);
                if (tense != -1) {   // 같은/특정 초성 결합 → 된소리·거센소리
                    ctx->cho = tense; res.preeditChar = Layout_ChoToCompatJamo(ctx->cho);
                } else {
                    res.commitChar = Layout_ChoToCompatJamo(ctx->cho);
                    ctx->cho = layoutRes.index; res.preeditChar = Layout_ChoToCompatJamo(ctx->cho);
                }
            } else if (layoutRes.type == JAMO_JUNG) {
                ctx->state = STATE_CHO_JUNG; ctx->jung = layoutRes.index;
                res.preeditChar = ComposeHangul(ctx->cho, ctx->jung, -1);
            } else {   // JAMO_JONG: 초성만 있는데 종성 → 초성 확정 (남은 종성 소비)
                res.commitChar = Layout_ChoToCompatJamo(ctx->cho);
                Fsm_Init(ctx);
            }
            break;

        case STATE_JUNG:
            if (layoutRes.type == JAMO_CHO) {
                res.commitChar = Layout_JungToCompatJamo(ctx->jung);
                Fsm_Init(ctx);
                ctx->state = STATE_CHO; ctx->cho = layoutRes.index;
                res.preeditChar = Layout_ChoToCompatJamo(ctx->cho);
            } else if (layoutRes.type == JAMO_JUNG) {
                int combined = fsm_combineJung(ctx->jung, layoutRes.index, hl);
                if (combined != -1) {
                    ctx->jung = combined; res.preeditChar = Layout_JungToCompatJamo(ctx->jung);
                } else {
                    res.commitChar = Layout_JungToCompatJamo(ctx->jung);
                    ctx->jung = layoutRes.index; res.preeditChar = Layout_JungToCompatJamo(ctx->jung);
                }
            } else {   // JAMO_JONG: 중성만 + 종성 → 중성 확정
                res.commitChar = Layout_JungToCompatJamo(ctx->jung);
                Fsm_Init(ctx);
            }
            break;

        case STATE_CHO_JUNG:
            if (layoutRes.type == JAMO_CHO) {
                int jong = (!directJong) ? Layout_ChoToJong(layoutRes.index) : -1;
                if (jong != -1) {   // 2벌식 도깨비불: 초성 → 종성
                    ctx->state = STATE_CHO_JUNG_JONG; ctx->jong = jong;
                    res.preeditChar = ComposeHangul(ctx->cho, ctx->jung, ctx->jong);
                } else {   // 세벌식/사용자: 초성 = 새 음절 (버그 수정: 이전엔 세벌식도 ChoToJong을 탐)
                    res.commitChar = ComposeHangul(ctx->cho, ctx->jung, -1);
                    Fsm_Init(ctx); ctx->state = STATE_CHO; ctx->cho = layoutRes.index;
                    res.preeditChar = Layout_ChoToCompatJamo(ctx->cho);
                }
            } else if (layoutRes.type == JAMO_JUNG) {
                int combined = fsm_combineJung(ctx->jung, layoutRes.index, hl);
                if (combined != -1) {
                    ctx->jung = combined; res.preeditChar = ComposeHangul(ctx->cho, ctx->jung, -1);
                } else {
                    res.commitChar = ComposeHangul(ctx->cho, ctx->jung, -1);
                    Fsm_Init(ctx); ctx->state = STATE_JUNG; ctx->jung = layoutRes.index;
                    res.preeditChar = Layout_JungToCompatJamo(ctx->jung);
                }
            } else {   // JAMO_JONG: 직접 종성 입력
                ctx->state = STATE_CHO_JUNG_JONG; ctx->jong = layoutRes.index;
                res.preeditChar = ComposeHangul(ctx->cho, ctx->jung, ctx->jong);
            }
            break;

        case STATE_CHO_JUNG_JONG:
            if (layoutRes.type == JAMO_CHO) {
                int combined = (!directJong) ? Layout_CombineJong(ctx->jong, layoutRes.index) : -1;
                if (combined != -1) {   // 2벌식: 종성+초성 → 겹받침
                    ctx->jong = combined; res.preeditChar = ComposeHangul(ctx->cho, ctx->jung, ctx->jong);
                } else {   // 새 음절
                    res.commitChar = ComposeHangul(ctx->cho, ctx->jung, ctx->jong);
                    Fsm_Init(ctx); ctx->state = STATE_CHO; ctx->cho = layoutRes.index;
                    res.preeditChar = Layout_ChoToCompatJamo(ctx->cho);
                }
            } else if (layoutRes.type == JAMO_JUNG) {
                if (!directJong) {   // 2벌식 도깨비불: 종성 분리 이동
                    int jong1, cho2;
                    Layout_SplitJong(ctx->jong, &jong1, &cho2);
                    res.commitChar = ComposeHangul(ctx->cho, ctx->jung, jong1 > 0 ? jong1 : -1);
                    Fsm_Init(ctx); ctx->state = STATE_CHO_JUNG; ctx->cho = cho2; ctx->jung = layoutRes.index;
                    res.preeditChar = ComposeHangul(ctx->cho, ctx->jung, -1);
                } else {   // 세벌식/사용자: 현재 음절 확정 후 새 중성
                    res.commitChar = ComposeHangul(ctx->cho, ctx->jung, ctx->jong);
                    Fsm_Init(ctx); ctx->state = STATE_JUNG; ctx->jung = layoutRes.index;
                    res.preeditChar = Layout_JungToCompatJamo(ctx->jung);
                }
            } else {   // JAMO_JONG: 종성 뒤 또 종성 → 겹받침 결합
                int comb = fsm_combineJongPair(ctx->jong, layoutRes.index, hl);
                if (comb != -1) {
                    ctx->jong = comb; res.preeditChar = ComposeHangul(ctx->cho, ctx->jung, ctx->jong);
                } else {
                    res.commitChar = ComposeHangul(ctx->cho, ctx->jung, ctx->jong);
                    Fsm_Init(ctx);
                }
            }
            break;
    }

    return res;
}

wchar_t Fsm_Flush(FsmContext *ctx) {
    wchar_t c = 0;
    if (ctx->state == STATE_CHO)        c = Layout_ChoToCompatJamo(ctx->cho);
    else if (ctx->state == STATE_JUNG)  c = Layout_JungToCompatJamo(ctx->jung);
    else if (ctx->state != STATE_EMPTY) c = ComposeHangul(ctx->cho, ctx->jung, ctx->jong);
    Fsm_Init(ctx);
    return c;
}

bool Fsm_Backspace(FsmContext *ctx, wchar_t *outPreedit) {
    *outPreedit = 0;
    switch (ctx->state) {
        case STATE_EMPTY:
            return false;                       // 조합 없음 → 앱의 백스페이스에 위임
        case STATE_CHO:
        case STATE_JUNG:
            Fsm_Init(ctx);                      // 홀 자모 하나뿐 → 조합 비움
            break;
        case STATE_CHO_JUNG:
            ctx->jung = -1; ctx->state = STATE_CHO;
            *outPreedit = Layout_ChoToCompatJamo(ctx->cho);   // 중성 제거 → 초성만
            break;
        case STATE_CHO_JUNG_JONG:
            ctx->jong = -1; ctx->state = STATE_CHO_JUNG;
            *outPreedit = ComposeHangul(ctx->cho, ctx->jung, -1);  // 종성 제거 → 초+중
            break;
    }
    return true;
}
