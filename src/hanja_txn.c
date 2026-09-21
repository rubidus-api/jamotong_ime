// hanja_txn.c — 한자 변환 트랜잭션 정책 (순수 로직). 규칙은 hanja_txn.h.
#include <stddef.h>
#include "hanja_txn.h"

HanjaTxnMode HanjaTxn_Mode(int composing, int inline_active) {
    if (!composing) return HANJA_FROM_DOCUMENT;
    return inline_active ? HANJA_COMMIT_THEN_REPLACE : HANJA_HOLD_ORIGINAL;
}

int HanjaTxn_SelectionStillValid(const wchar_t *expected, const wchar_t *current) {
    if (!expected || !expected[0] || !current) return 0;
    return wcscmp(expected, current) == 0;
}
