// edit_verdict.c — EDIT 계열 경로의 삽입 성공 판정 (순수 로직). 규칙은 edit_verdict.h.
#include "edit_verdict.h"

EditVerdict EditVerdict_Decide(EditSelSnap before, EditSelSnap after, long insLen) {
    if (insLen <= 0) return EDIT_VERDICT_INSERTED;
    if (!before.ok || !after.ok) return EDIT_VERDICT_UNKNOWN;
    if (before.start == after.start && before.end == after.end) return EDIT_VERDICT_NOT_INSERTED;
    return EDIT_VERDICT_INSERTED;
}

bool EditVerdict_Failed(EditVerdict v) {
    return v == EDIT_VERDICT_NOT_INSERTED;
}
