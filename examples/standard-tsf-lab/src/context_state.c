/* context_state.c — 컨텍스트별 상태 소유 (RFC-0009 §5.1)
 *
 * 조합과 FSM을 ITfContext별로 소유한다. 탭/창/문서/포커스를 옮겨도 상태가 섞이지 않는다.
 * generation은 stale async callback을 걸러내는 데 쓴다(§9.2).
 */
#include "lab_tip.h"
#include <stdlib.h>

static HRESULT AdviseTextEditSink(LabTextService *svc, LabContextState *st) {
    ITfSource *source = NULL;
    HRESULT hr = ITfContext_QueryInterface(st->context, &IID_ITfSource, (void**)&source);
    if (SUCCEEDED(hr)) {
        DWORD cookie = LAB_INVALID_COOKIE;
        hr = ITfSource_AdviseSink(source, &IID_ITfTextEditSink,
                                  (IUnknown*)&svc->text_edit_sink, &cookie);
        if (SUCCEEDED(hr)) st->text_edit_sink_cookie = cookie;
        ITfSource_Release(source);
    }
    Lab_TraceEvent("text_edit.advise", st, hr, SUCCEEDED(hr) ? 1 : 0);
    return hr;
}

static void UnadviseTextEditSink(LabContextState *st) {
    if (!st->context || st->text_edit_sink_cookie == LAB_INVALID_COOKIE) return;
    ITfSource *source = NULL;
    HRESULT hr = ITfContext_QueryInterface(st->context, &IID_ITfSource, (void**)&source);
    if (SUCCEEDED(hr)) {
        hr = ITfSource_UnadviseSink(source, st->text_edit_sink_cookie);
        ITfSource_Release(source);
    }
    Lab_TraceEvent("text_edit.unadvise", st, hr, 0);
    st->text_edit_sink_cookie = LAB_INVALID_COOKIE;
}

static void ReleaseContextState(LabContextState *st) {
    UnadviseTextEditSink(st);
    Lab_TraceEvent("context.release", st, S_OK, 0);
    if (st->composition) ITfComposition_Release(st->composition);
    if (st->context) ITfContext_Release(st->context);
    free(st);
}

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
    st->text_edit_sink_cookie = LAB_INVALID_COOKIE;
    ITfContext_AddRef(ctx);
    Hangul2_Reset(&st->hangul);
    st->generation = ++svc->next_generation;
    st->next = svc->contexts;
    svc->contexts = st;
    Lab_TraceEvent("context.create", st, S_OK, 0);
    AdviseTextEditSink(svc, st);
    return st;
}

bool Lab_RemoveContext(LabTextService *svc, ITfContext *ctx) {
    LabContextState **link = &svc->contexts;
    while (*link) {
        if ((*link)->context == ctx) {
            LabContextState *dead = *link;
            *link = dead->next;
            ReleaseContextState(dead);
            return true;
        }
        link = &(*link)->next;
    }
    return false;
}

void Lab_ReleaseContexts(LabTextService *svc) {
    LabContextState *p = svc->contexts;
    svc->contexts = NULL;
    while (p) {
        LabContextState *n = p->next;
        ReleaseContextState(p);
        p = n;
    }
}
