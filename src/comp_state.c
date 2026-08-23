#include "comp_state.h"

bool CompState_IsHangulType(int layoutType) {
    return layoutType == 1 /*KOREAN_FSM*/ || layoutType == 4 /*HANGUL_CUSTOM*/;
}

void CompState_Values(int layoutType, bool passthrough, bool fullWidth, long *openClose, long *convMode) {
    bool hangul = !passthrough && CompState_IsHangulType(layoutType);
    long conv = hangul ? JAMO_CMODE_NATIVE : JAMO_CMODE_ALPHANUMERIC;
    if (!passthrough && fullWidth) conv |= JAMO_CMODE_FULLSHAPE;
    if (openClose) *openClose = hangul ? 1 : 0;
    if (convMode)  *convMode  = conv;
}

int CompState_PickIndex(const int *types, const bool *enabled, int n, int cur, bool wantHangul) {
    if (!types || !enabled || n <= 0) return -1;
    if (cur < 0 || cur >= n) cur = 0;
    if (enabled[cur] && CompState_IsHangulType(types[cur]) == wantHangul) return cur;
    for (int i = 1; i <= n; i++) {
        int idx = (cur + i) % n;
        if (enabled[idx] && CompState_IsHangulType(types[idx]) == wantHangul) return idx;
    }
    return -1;
}
