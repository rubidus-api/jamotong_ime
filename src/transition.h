#pragma once
// transition.h — 조합 경계 전환 정책 (순수 로직, WinAPI 무의존, RFC-0008 W1-09)
//   포커스를 떠날 때 남은 음절은 **조합을 시작한 대상**에 정확히 한 번 확정한다(버리지 않는다).
//   알림 시점엔 포커스가 이미 새 창에 있으므로 '지금 포커스'가 아니라 '기억한 대상'에 넣는다.
//   넣지 못하면 대상에 묶어 보류하고, 그 대상으로 돌아와 키를 칠 때 한 번 재시도한다.
// 네이티브 테스트: jamotong-private/test/transition_test.c (T020)

typedef enum TransFlushAction {
    TRANS_NONE = 0,            // 남은 음절 없음
    TRANS_FINALIZE_INLINE = 1, // 인라인 조합 — 텍스트가 이미 문서에 있다, Finalize 만
    TRANS_EDIT_REPLACE = 2,    // 기억한 EDIT 창에 EM_REPLACESEL (B1 판정)
    TRANS_ASYNC_SESSION = 3,   // 기억한 문맥에 비동기 편집 세션 (키 이벤트 밖이라 동기는 거부된다)
    TRANS_PEND = 4             // 대상을 모른다 — 보류
} TransFlushAction;

// inline_active: 인라인 조합이 살아 있는가. fsm_nonempty: FSM 에 음절이 남았는가.
// edit_ok: 기억한 EDIT 창이 살아 있고 이 스레드 것인가. ctx_ok: 기억한 문맥이 있는가.
TransFlushAction Trans_FlushAction(int inline_active, int fsm_nonempty, int edit_ok, int ctx_ok);

// 보류한 음절을 지금 넣어도 되는가 — 그 대상으로 돌아왔을 때만(1).
//   EDIT 대상이면 창으로 대조(CUAS 는 문맥 포인터가 바뀔 수 있다), 아니면 문맥으로.
//   대상이 없는 보류(옛 방식)는 아무 문맥에서나. 지금 문맥이 없으면 넣을 수 없다.
int Trans_PendingMatches(const void *pend_hwnd, const void *pend_ctx,
                         const void *focus_edit_hwnd, const void *cur_ctx);
