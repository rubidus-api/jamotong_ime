/* context_state.c — 컨텍스트별 상태 소유 (RFC-0009 §5.1)
 *
 * 조합과 FSM을 ITfContext별로 소유한다. 탭/창/문서/포커스를 옮겨도 상태가 섞이지 않는다.
 * generation은 stale async callback을 걸러내는 데 쓴다(§9.2).
 */
#include "lab_tip.h"
#include <stdlib.h>

LabContextState *Lab_FindContext(LabTextService *svc, ITfContext *ctx) {
    for (LabContextState *p = svc->contexts; p; p = p->next)
        if (p->context == ctx) return p;
    return NULL;
}

LabContextState *Lab_EnsureContext(LabTextService *svc, ITfContext *ctx) {
    if (!ctx) return NULL;
    LabContextState *st = Lab_FindContext(svc, ctx);
    if (st) return st;
    st = (LabContextState*)calloc(1, sizeof(*st));
    if (!st) return NULL;
    st->context = ctx;
    ITfContext_AddRef(ctx);
    Hangul2_Reset(&st->hangul);
    st->generation = ++svc->next_generation;
    st->next = svc->contexts;
    svc->contexts = st;
    return st;
}

void Lab_ReleaseContexts(LabTextService *svc) {
    LabContextState *p = svc->contexts;
    svc->contexts = NULL;
    while (p) {
        LabContextState *n = p->next;
        if (p->composition) ITfComposition_Release(p->composition);
        if (p->context)     ITfContext_Release(p->context);
        free(p);
        p = n;
    }
}
