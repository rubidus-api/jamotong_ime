/* hangul2.c — 두벌식 현대 한글 최소 FSM (RFC-0009 §6)
 *
 * 순수 함수. 부작용 없음. Windows 헤더 없이 컴파일된다.
 */
#include "hangul2.h"
#include <string.h>

/* ── 결합표 (RFC-0009 §6.2). switch가 아니라 표 — 테스트가 전수 순회한다. ── */
const PairMap kCompoundVowels[] = {
    { V_O,  V_A,  V_WA  },
    { V_O,  V_AE, V_WAE },
    { V_O,  V_I,  V_OE  },
    { V_U,  V_EO, V_WO  },
    { V_U,  V_E,  V_WE  },
    { V_U,  V_I,  V_WI  },
    { V_EU, V_I,  V_UI  },
};
const unsigned kCompoundVowelCount = sizeof(kCompoundVowels)/sizeof(kCompoundVowels[0]);

const PairMap kCompoundFinals[] = {
    { T_G, T_S, T_GS },
    { T_N, T_J, T_NJ },
    { T_N, T_H, T_NH },
    { T_R, T_G, T_RG },
    { T_R, T_M, T_RM },
    { T_R, T_B, T_RB },
    { T_R, T_S, T_RS },
    { T_R, T_T, T_RT },
    { T_R, T_P, T_RP },
    { T_R, T_H, T_RH },
    { T_B, T_S, T_BS },
};
const unsigned kCompoundFinalCount = sizeof(kCompoundFinals)/sizeof(kCompoundFinals[0]);

/* 초성 ↔ 종성. 대응이 없으면 -1. */
static const int8_t kInitialToFinal[L_COUNT] = {
    [L_G]=T_G, [L_GG]=T_GG, [L_N]=T_N, [L_D]=T_D, [L_DD]=-1, [L_R]=T_R,
    [L_M]=T_M, [L_B]=T_B, [L_BB]=-1, [L_S]=T_S, [L_SS]=T_SS, [L_NG]=T_NG,
    [L_J]=T_J, [L_JJ]=-1, [L_C]=T_C, [L_K]=T_K, [L_T]=T_T, [L_P]=T_P, [L_H]=T_H,
};
static const int8_t kFinalToInitial[T_COUNT] = {
    [T_NONE]=-1, [T_G]=L_G, [T_GG]=L_GG, [T_GS]=-1, [T_N]=L_N, [T_NJ]=-1, [T_NH]=-1,
    [T_D]=L_D, [T_R]=L_R, [T_RG]=-1, [T_RM]=-1, [T_RB]=-1, [T_RS]=-1, [T_RT]=-1,
    [T_RP]=-1, [T_RH]=-1, [T_M]=L_M, [T_B]=L_B, [T_BS]=-1, [T_S]=L_S, [T_SS]=L_SS,
    [T_NG]=L_NG, [T_J]=L_J, [T_C]=L_C, [T_K]=L_K, [T_T]=L_T, [T_P]=L_P, [T_H]=L_H,
};

int Hangul2_InitialToFinal(int initial) {
    if (initial < 0 || initial >= L_COUNT) return -1;
    return kInitialToFinal[initial];
}
int Hangul2_FinalToInitial(int final) {
    if (final < 0 || final >= T_COUNT) return -1;
    return kFinalToInitial[final];
}

/* 낱자 하나만 있을 때 preedit에 보여줄 호환 자모(U+3131~U+3163) */
static const WCHAR kCompatInitial[L_COUNT] = {
    0x3131,0x3132,0x3134,0x3137,0x3138,0x3139,0x3141,0x3142,0x3143,0x3145,
    0x3146,0x3147,0x3148,0x3149,0x314A,0x314B,0x314C,0x314D,0x314E
};
static const WCHAR kCompatVowel[V_COUNT] = {
    0x314F,0x3150,0x3151,0x3152,0x3153,0x3154,0x3155,0x3156,0x3157,0x3158,
    0x3159,0x315A,0x315B,0x315C,0x315D,0x315E,0x315F,0x3160,0x3161,0x3162,0x3163
};

WCHAR Hangul2_Compose(int l, int v, int t) {
    return (WCHAR)(0xAC00 + ((l * 21 + v) * 28 + t));
}

void Hangul2_Reset(HangulState *st) {
    memset(st, 0, sizeof(*st));
    st->initial = -1; st->vowel = -1; st->final = T_NONE;
}
bool Hangul2_IsEmpty(const HangulState *st) {
    return st->initial < 0 && st->vowel < 0 && st->final == T_NONE;
}

uint8_t Hangul2_Preedit(const HangulState *st, WCHAR *out, uint8_t cap) {
    if (cap < 1) return 0;
    if (st->initial >= 0 && st->vowel >= 0) {
        out[0] = Hangul2_Compose(st->initial, st->vowel, st->final);
        return 1;
    }
    if (st->initial >= 0) { out[0] = kCompatInitial[st->initial]; return 1; }
    if (st->vowel   >= 0) { out[0] = kCompatVowel[st->vowel];     return 1; }
    return 0;
}

/* 두벌식 매핑 — virtual key + Shift 직접 (RFC-0009 §6.1) */
JamoKey Hangul2_MapDubeolsik(UINT vk, bool shift) {
    switch (vk) {
    /* 자음 */
    case 'R': return (JamoKey){ JAMO_INITIAL, shift ? L_GG : L_G };
    case 'S': return (JamoKey){ JAMO_INITIAL, L_N };
    case 'E': return (JamoKey){ JAMO_INITIAL, shift ? L_DD : L_D };
    case 'F': return (JamoKey){ JAMO_INITIAL, L_R };
    case 'A': return (JamoKey){ JAMO_INITIAL, L_M };
    case 'Q': return (JamoKey){ JAMO_INITIAL, shift ? L_BB : L_B };
    case 'T': return (JamoKey){ JAMO_INITIAL, shift ? L_SS : L_S };
    case 'D': return (JamoKey){ JAMO_INITIAL, L_NG };
    case 'W': return (JamoKey){ JAMO_INITIAL, shift ? L_JJ : L_J };
    case 'C': return (JamoKey){ JAMO_INITIAL, L_C };
    case 'Z': return (JamoKey){ JAMO_INITIAL, L_K };
    case 'X': return (JamoKey){ JAMO_INITIAL, L_T };
    case 'V': return (JamoKey){ JAMO_INITIAL, L_P };
    case 'G': return (JamoKey){ JAMO_INITIAL, L_H };
    /* 모음 */
    case 'K': return (JamoKey){ JAMO_VOWEL, V_A };
    case 'O': return (JamoKey){ JAMO_VOWEL, shift ? V_YAE : V_AE };
    case 'I': return (JamoKey){ JAMO_VOWEL, V_YA };
    case 'J': return (JamoKey){ JAMO_VOWEL, V_EO };
    case 'P': return (JamoKey){ JAMO_VOWEL, shift ? V_YE : V_E };
    case 'U': return (JamoKey){ JAMO_VOWEL, V_YEO };
    case 'H': return (JamoKey){ JAMO_VOWEL, V_O };
    case 'Y': return (JamoKey){ JAMO_VOWEL, V_YO };
    case 'N': return (JamoKey){ JAMO_VOWEL, V_U };
    case 'B': return (JamoKey){ JAMO_VOWEL, V_YU };
    case 'M': return (JamoKey){ JAMO_VOWEL, V_EU };
    case 'L': return (JamoKey){ JAMO_VOWEL, V_I };
    default:  return (JamoKey){ JAMO_NONE, 0 };
    }
}

static int FindCompoundVowel(int a, int b) {
    for (unsigned i = 0; i < kCompoundVowelCount; i++)
        if (kCompoundVowels[i].first == a && kCompoundVowels[i].second == b)
            return kCompoundVowels[i].combined;
    return -1;
}
static int FindCompoundFinal(int a, int b) {
    for (unsigned i = 0; i < kCompoundFinalCount; i++)
        if (kCompoundFinals[i].first == a && kCompoundFinals[i].second == b)
            return kCompoundFinals[i].combined;
    return -1;
}
/* 겹받침을 앞/뒤로 쪼갠다. 겹받침이 아니면 false. */
static bool SplitFinal(int t, int *head, int *tail) {
    for (unsigned i = 0; i < kCompoundFinalCount; i++)
        if (kCompoundFinals[i].combined == t) {
            *head = kCompoundFinals[i].first; *tail = kCompoundFinals[i].second;
            return true;
        }
    return false;
}

static void PushHistory(HangulState *st, JamoKind kind, int index) {
    if (st->history_len < LAB_HISTORY_MAX)
        st->history[st->history_len++] = (HangulKeyStep){ kind, index };
}
/* 확정 후 남은 preedit에 필요한 history만 재구성 (RFC-0009 §6.3) */
static void RebuildHistory(HangulState *st) {
    st->history_len = 0;
    if (st->initial >= 0) PushHistory(st, JAMO_INITIAL, st->initial);
    if (st->vowel   >= 0) PushHistory(st, JAMO_VOWEL,   st->vowel);
}

static void EmitPreedit(HangulStep *out) {
    out->preedit_len = Hangul2_Preedit(&out->next, out->preedit, 4);
}
static void CommitSyllable(HangulStep *out, int l, int v, int t) {
    out->committed[out->committed_len++] = Hangul2_Compose(l, v, t);
}

bool Hangul2_Step(const HangulState *cur, UINT vk, bool shift, HangulStep *out) {
    JamoKey k = Hangul2_MapDubeolsik(vk, shift);
    if (k.kind == JAMO_NONE) return false;

    memset(out, 0, sizeof(*out));
    out->next = *cur;
    out->handled = true;

    if (k.kind == JAMO_INITIAL) {
        /* ── 자음이 왔다 ── */
        if (out->next.initial < 0 && out->next.vowel < 0) {
            out->next.initial = k.index;
            RebuildHistory(&out->next);
            EmitPreedit(out);
            return true;
        }
        if (out->next.vowel < 0) {
            /* 초성만 있는데 또 자음 → 앞 초성을 낱자로 확정하고 새 초성 */
            out->committed[out->committed_len++] = kCompatInitial[out->next.initial];
            Hangul2_Reset(&out->next);
            out->next.initial = k.index;
            RebuildHistory(&out->next);
            EmitPreedit(out);
            return true;
        }
        if (out->next.initial < 0) {
            /* 모음만 있는 상태(초성 없이 모음이 먼저 들어온 경우)에 자음이 왔다.
               종성은 초성이 있어야 성립하므로, 모음을 낱자로 확정하고 새 초성을 잡는다.
               이걸 빼먹으면 initial=-1인 채로 종성이 붙고, 확정 때
               0xAC00 + ((-1*21+v)*28+t) 가 되어 음절 범위 밖 코드포인트가 나온다. */
            out->committed[out->committed_len++] = kCompatVowel[out->next.vowel];
            Hangul2_Reset(&out->next);
            out->next.initial = k.index;
            RebuildHistory(&out->next);
            EmitPreedit(out);
            return true;
        }
        /* 초성+중성(+종성) 상태 → 종성 자리를 노린다 */
        int nf = Hangul2_InitialToFinal(k.index);
        if (out->next.final == T_NONE) {
            if (nf > 0) {
                out->next.final = nf;
                PushHistory(&out->next, JAMO_INITIAL, k.index);
                EmitPreedit(out);
                return true;
            }
            /* 종성이 될 수 없는 자음(ㄸㅃㅉ) → 현재 음절 확정 + 새 초성 */
            CommitSyllable(out, out->next.initial, out->next.vowel, T_NONE);
            Hangul2_Reset(&out->next);
            out->next.initial = k.index;
            RebuildHistory(&out->next);
            EmitPreedit(out);
            return true;
        }
        /* 이미 종성이 있다 → 겹받침 시도 */
        int comb = (nf > 0) ? FindCompoundFinal(out->next.final, nf) : -1;
        if (comb > 0) {
            out->next.final = comb;
            PushHistory(&out->next, JAMO_INITIAL, k.index);
            EmitPreedit(out);
            return true;
        }
        /* 안 되면 현재 음절 확정 + 새 초성 */
        CommitSyllable(out, out->next.initial, out->next.vowel, out->next.final);
        Hangul2_Reset(&out->next);
        out->next.initial = k.index;
        RebuildHistory(&out->next);
        EmitPreedit(out);
        return true;
    }

    /* ── 모음이 왔다 ── */
    if (out->next.vowel < 0 && out->next.initial < 0) {
        out->next.vowel = k.index;
        RebuildHistory(&out->next);
        EmitPreedit(out);
        return true;
    }
    if (out->next.vowel < 0) {
        out->next.vowel = k.index;
        PushHistory(&out->next, JAMO_VOWEL, k.index);
        EmitPreedit(out);
        return true;
    }
    if (out->next.final == T_NONE) {
        /* 겹모음 시도 */
        int comb = FindCompoundVowel(out->next.vowel, k.index);
        if (comb >= 0) {
            out->next.vowel = comb;
            PushHistory(&out->next, JAMO_VOWEL, k.index);
            EmitPreedit(out);
            return true;
        }
        /* 안 되면 현재 음절 확정 + 모음 단독 시작 */
        if (out->next.initial < 0)
            out->committed[out->committed_len++] = kCompatVowel[out->next.vowel];
        else
            CommitSyllable(out, out->next.initial, out->next.vowel, T_NONE);
        Hangul2_Reset(&out->next);
        out->next.vowel = k.index;
        RebuildHistory(&out->next);
        EmitPreedit(out);
        return true;
    }
    /* ── 종성 뒤 모음 = 도깨비불 (RFC-0009 §6.2) ── */
    int head, tail;
    if (SplitFinal(out->next.final, &head, &tail)) {
        /* 겹받침이면 앞 자모만 남기고 뒤를 다음 초성으로 */
        CommitSyllable(out, out->next.initial, out->next.vowel, head);
        int ni = Hangul2_FinalToInitial(tail);
        Hangul2_Reset(&out->next);
        out->next.initial = ni;
        out->next.vowel = k.index;
    } else {
        /* 단일 종성이면 통째로 다음 초성으로 */
        CommitSyllable(out, out->next.initial, out->next.vowel, T_NONE);
        int ni = Hangul2_FinalToInitial(out->next.final);
        Hangul2_Reset(&out->next);
        out->next.initial = ni;
        out->next.vowel = k.index;
    }
    RebuildHistory(&out->next);
    EmitPreedit(out);
    return true;
}

bool Hangul2_Backspace(const HangulState *cur, HangulStep *out) {
    if (Hangul2_IsEmpty(cur)) return false;      /* 앱이 원래 Backspace를 받는다 */

    memset(out, 0, sizeof(*out));
    out->next = *cur;
    out->handled = true;

    /* history를 한 단계 되감는다 — 유니코드 역산이 아니라 실제 입력 단계 (RFC-0009 §6.3) */
    if (out->next.history_len > 0) {
        out->next.history_len--;
        HangulState rebuilt;
        Hangul2_Reset(&rebuilt);
        for (uint8_t i = 0; i < out->next.history_len; i++) {
            HangulKeyStep h = out->next.history[i];
            if (h.kind == JAMO_INITIAL) {
                if (rebuilt.initial < 0)      rebuilt.initial = h.index;
                else if (rebuilt.vowel >= 0) {
                    int nf = Hangul2_InitialToFinal(h.index);
                    if (nf > 0) {                 /* ㄸㅃㅉ는 종성이 될 수 없다 → -1 */
                        if (rebuilt.final == T_NONE) rebuilt.final = nf;
                        else {
                            int c = FindCompoundFinal(rebuilt.final, nf);
                            if (c > 0) rebuilt.final = c;
                        }
                    }
                }
            } else {
                if (rebuilt.vowel < 0) rebuilt.vowel = h.index;
                else {
                    int c = FindCompoundVowel(rebuilt.vowel, h.index);
                    if (c >= 0) rebuilt.vowel = c;
                }
            }
        }
        memcpy(rebuilt.history, out->next.history, sizeof(rebuilt.history));
        rebuilt.history_len = out->next.history_len;
        out->next = rebuilt;
        EmitPreedit(out);
        return true;
    }
    Hangul2_Reset(&out->next);
    EmitPreedit(out);
    return true;
}
