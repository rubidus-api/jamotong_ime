// transition.c — 조합 경계 전환 정책 (순수 로직). 규칙은 transition.h.
#include <stddef.h>
#include "transition.h"

TransFlushAction Trans_FlushAction(int inline_active, int fsm_nonempty, int edit_ok, int ctx_ok) {
    if (inline_active) return TRANS_FINALIZE_INLINE;
    if (!fsm_nonempty) return TRANS_NONE;
    if (edit_ok) return TRANS_EDIT_REPLACE;
    if (ctx_ok) return TRANS_ASYNC_SESSION;
    return TRANS_PEND;
}

int Trans_PendingMatches(const void *pend_hwnd, const void *pend_ctx,
                         const void *focus_edit_hwnd, const void *cur_ctx) {
    if (!cur_ctx) return 0;
    if (pend_hwnd) return focus_edit_hwnd == pend_hwnd;
    if (pend_ctx) return cur_ctx == pend_ctx;
    return 1;
}

TransResendRoute Trans_ResendRoute(int target_known, int target_alive, int target_is_edit, int focus_is_target) {
    if (!target_known) return TRANS_RESEND_SENDINPUT;   // 예전 동작 (대상 정보 없음)
    if (!target_alive) return TRANS_RESEND_DROP;
    if (target_is_edit) return TRANS_RESEND_POST;
    return focus_is_target ? TRANS_RESEND_SENDINPUT : TRANS_RESEND_DROP;
}
