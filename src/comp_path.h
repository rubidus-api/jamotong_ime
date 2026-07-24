#pragma once
// comp_path.h — RFC-0010 인라인 composition 경로 결정 (순수 로직, WinAPI 무의존)
//   비단명(non-transitory) 컨텍스트 → 표준 ITfComposition (STANDARD)
//   단명(TS_SS_TRANSITORY)·판정 불가·필수 인터페이스 결여·행동 강등 → 기존 commit 전용 (COMMIT)
// 네이티브 테스트: jamotong-private/test/comp_path_test.c (T011)

// Windows SDK TS_SS_TRANSITORY와 같은 값이어야 한다 (독립 oracle: textstor.h = 0x4).
#define JAMO_TS_SS_TRANSITORY 0x4UL

// 연속 강등 문턱: 우리 composition이 갱신 생존 없이 이만큼 연속 외부 종료되면 COMMIT으로.
#define JAMO_PATH_DEMOTE_LIMIT 2

typedef enum JamoPathKind {
    JAMO_PATH_COMMIT = 0,     // 기존 commit 전용 + 오버레이 (RFC-0002)
    JAMO_PATH_STANDARD = 1    // 문서 인라인 표준 composition (RFC-0010)
} JamoPathKind;

// status_hr: ITfContext::GetStatus의 HRESULT (0 이상 = 성공).
// static_flags: TF_STATUS.dwStaticFlags.
// has_insert / has_ctx_comp: ITfInsertAtSelection / ITfContextComposition QI 성공 여부.
// demerits: 이 컨텍스트에서 관측된 연속 강등 카운트(음수는 0으로 취급).
JamoPathKind JamoPath_Decide(long status_hr, unsigned long static_flags,
                             int has_insert, int has_ctx_comp, int demerits);
