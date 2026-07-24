#pragma once
// comp_inline.h — RFC-0010 문서 인라인 표준 composition (비단명 컨텍스트 전용)
//   경로 판정: comp_path.h(JamoPath_Decide) + ITfContext::GetStatus(TS_SS_TRANSITORY).
//   트랜잭션 레시피는 examples/standard-tsf-lab(메모장 실기 PASS)에서 이식:
//   QUERYONLY range → StartComposition(실제 sink, S_OK+NULL=실패) → SetText(flag 0)
//   → 표시 속성 → 캐럿을 조합 끝으로 → 확정 prefix ShiftStart → 필요 시 EndComposition.
// 모든 함수는 입력 스레드 전용. PathForContext는 config를 읽으므로 g_configLock 보유 하에 호출.
#include "jamotong.h"
#include "comp_path.h"

// 서비스 생성 시 sink vtbl 초기화 (JamotongTextService_Create).
void JamoComp_Init(JamotongTextService *svc);

// 현재 컨텍스트의 경로 판정(단일 슬롯 캐시). 킬스위치(config.options.inlineComposition=0)와
// EDIT 계열 검출은 호출자가 먼저 거른다(EDIT 계열은 실기 검증된 EM_REPLACESEL 경로 유지).
JamoPathKind JamoComp_PathForContext(JamotongTextService *svc, ITfContext *pic);

// 순차 FSM 한 키 결과를 표준 composition으로 반영(한 키 = 한 동기 세션).
// 실패 시 조합 rollback을 시도하고 강등을 기록한 뒤 실패 hr을 반환 — 호출자는 기존
// commit 경로로 이 키 결과를 이어간다(실패를 감추지 않는다, RFC-0004 P2-2).
HRESULT JamoComp_Apply(JamotongTextService *svc, ITfContext *pic, FsmResult res);

// 활성 문서 composition 존재 여부.
BOOL JamoComp_IsActive(const JamotongTextService *svc);

// 확정: 조합 텍스트는 문서에 그대로 두고 composition만 종료(플러시·포커스 이동·한자키).
// preedit 재삽입 금지 — 텍스트는 이미 문서 안에 있다.
void JamoComp_Finalize(JamotongTextService *svc);

// 취소: 조합 텍스트를 문서에서 제거하고 종료(Esc).
void JamoComp_Cancel(JamotongTextService *svc);

// 경로 캐시 무효화(포커스 이동 시 — stale 컨텍스트 포인터 재사용 방지).
void JamoComp_ResetPathCache(JamotongTextService *svc);

// Deactivate 정리: 조합 확정 시도 후 로컬 참조 해제.
void JamoComp_Release(JamotongTextService *svc);
