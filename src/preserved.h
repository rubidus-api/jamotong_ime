#pragma once
// RFC-0013 C — TSF preserved key 글루. 문맥 무관 명령(한/영·무간섭·설정·코드입력)만
// `ITfKeystrokeMgr::PreserveKey` 로 예약한다. 매핑 규칙 = preserved_map.c (순수·T017).
//
// · 한자(SC_FN_HANJA)는 넣지 않는다 — 조합 중 음절에 작용하는 문맥 의존 키라 key sink 몫.
// · 등록 불가 항목(GUI 조합·맨 모디파이어 트리거)과 등록 실패는 기존 OnTestKeyDown
//   예측-소비 경로가 그대로 받는다(폴백 유지 — 지우는 커밋은 실기 PASS 뒤 별도).
// · 킬스위치 config.options.usePreservedKeys (config.ini `UsePreservedKeys=0`).
#include "jamotong.h"

void Preserved_Register(JamotongTextService *obj);    // TIP activate 에서 (키 sink advise 뒤)
void Preserved_Unregister(JamotongTextService *obj);  // TIP deactivate 에서
// OnPreservedKey 의 rguid → 기능 번호(ShortcutFn). 우리 것이 아니면 -1.
int  Preserved_Lookup(const JamotongTextService *obj, const GUID *rguid);
