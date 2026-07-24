// comp_path.c — RFC-0010 경로 결정 (순수 로직; 근거는 R1~R4 실기 + R2 재실행, 매뉴얼 §12.7.8)
#include "comp_path.h"

JamoPathKind JamoPath_Decide(long status_hr, unsigned long static_flags,
                             int has_insert, int has_ctx_comp, int demerits) {
    if (status_hr < 0) return JAMO_PATH_COMMIT;                    // 판정 불가 → 보수적
    if (static_flags & JAMO_TS_SS_TRANSITORY) return JAMO_PATH_COMMIT;   // 단명 CUAS 문서
    if (!has_insert || !has_ctx_comp) return JAMO_PATH_COMMIT;     // 필수 인터페이스 결여
    if (demerits >= JAMO_PATH_DEMOTE_LIMIT) return JAMO_PATH_COMMIT;     // 행동 강등
    return JAMO_PATH_STANDARD;
}
