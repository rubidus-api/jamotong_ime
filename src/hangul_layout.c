// hangul_layout.c — 한글 자판 **런타임**: 결합 규칙 조회와 해제.
//   파일을 읽는 일은 도구 쪽(hangul_parse.c)이다.
#include "hangul_layout.h"
#include <stdio.h>
#include <string.h>

void HangulLayout_Free(HangulLayout *hl) {
    if (hl) HeapFree(GetProcessHeap(), 0, hl);
}

int HangulLayout_Combine(const HangulLayout *hl, JamoType type, int a, int b) {
    if (!hl) return -1;
    for (int i = 0; i < hl->combineCount; i++) {
        const HangulCombine *c = &hl->combines[i];
        if (c->type != type) continue;
        if (c->a == a && c->b == b) return c->result;
        if (hl->moachigi && c->a == b && c->b == a) return c->result;   // 모아치기: 순서 무관
    }
    return -1;
}

// 가드 프로그램 하나를 재어 본다. 스택이 얕아(8) 재귀도 배열도 필요 없다.
static long long GuardRun(const unsigned char *code, int n, int cho, int jung, int jong) {
    long long st[8];
    int sp = 0;
    for (int i = 0; i < n; ) {
        unsigned char op = code[i++];
        if (op == HLG_HAS || op == HLG_IDX || op == HLG_NUM) {
            if (i >= n || sp >= 8) return 0;
            unsigned char a = code[i++];
            long long v = 0;
            if (op == HLG_NUM) v = a;
            else {
                int have = a == 0 ? (cho >= 0) : a == 1 ? (jung >= 0) : a == 2 ? (jong > 0)
                                                        : (cho < 0 && jung < 0 && jong <= 0);   // 3 = 빈 조합
                if (op == HLG_HAS) v = have;
                else v = a == 0 ? cho : a == 1 ? jung : a == 2 ? jong : (have ? 1 : 0);
            }
            st[sp++] = v;
            continue;
        }
        if (op == HLG_NOT) { if (sp < 1) return 0; st[sp-1] = !st[sp-1]; continue; }
        if (sp < 2) return 0;
        long long b = st[--sp], a2 = st[--sp];
        long long r;
        switch (op) {
            case HLG_AND: r = (a2 && b); break;
            case HLG_OR:  r = (a2 || b); break;
            case HLG_EQ + 0: r = (a2 == b); break;
            case HLG_EQ + 1: r = (a2 != b); break;
            case HLG_EQ + 2: r = (a2 <  b); break;
            case HLG_EQ + 3: r = (a2 <= b); break;
            case HLG_EQ + 4: r = (a2 >  b); break;
            case HLG_EQ + 5: r = (a2 >= b); break;
            default: return 0;
        }
        st[sp++] = r;
    }
    return sp == 1 ? st[0] : 0;
}

LayoutResult HangulLayout_Key(const HangulLayout *hl, wchar_t key, int cho, int jung, int jong) {
    LayoutResult none = { JAMO_NONE, 0 };
    if (!hl || (unsigned)key >= 128) return none;
    for (int i = 0; i < hl->guardedCount; i++) {      // 좁은 줄이 먼저다 (적은 차례대로)
        const HangulGuarded *g = &hl->guarded[i];
        if (g->key != (unsigned char)key) continue;
        if (GuardRun(g->code, g->len, cho, jung, jong)) return g->r;
    }
    return hl->keymap[(int)key];
}
