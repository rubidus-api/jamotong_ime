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
