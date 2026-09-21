#pragma once
// edit_verdict.h — EDIT 계열 경로의 삽입 성공 판정 (순수 로직, WinAPI 무의존)
//   EM_REPLACESEL 은 결과를 돌려주지 않는다. 그래서 보내기 전후의 선택 범위를 견준다:
//   성공한 삽입/교체는 선택을 항상 s+n 캐럿으로 접으므로, 전후 선택이 '완전히 같으면'
//   아무것도 들어가지 않은 것이다(읽기 전용·길이 제한에 걸린 EDIT 는 메시지를 무시한다).
//   판정을 틀리면 이중 삽입이 되므로 확실한 경우만 실패로 본다 — 읽지 못했으면 UNKNOWN(=성공 취급).
// 네이티브 테스트: jamotong-private/test/edit_verdict_test.c (T019)
#include <stdbool.h>

typedef struct EditSelSnap {
    int ok;          // 선택을 읽었는가 (EM_EXGETSEL/EM_GETSEL 응답)
    long start;      // 선택 시작
    long end;        // 선택 끝 (캐럿이면 start == end)
} EditSelSnap;

typedef enum EditVerdict {
    EDIT_VERDICT_INSERTED = 0,       // 선택이 움직였다 (부분 반영도 여기 — 다시 넣으면 중복 위험)
    EDIT_VERDICT_NOT_INSERTED = 1,   // 전후 선택이 같다 = 확실히 안 들어갔다
    EDIT_VERDICT_UNKNOWN = 2         // 읽지 못했다 = 판정 불가
} EditVerdict;

// insLen: 넣으려던 UTF-16 길이 (0 이하면 넣을 것이 없어 INSERTED).
EditVerdict EditVerdict_Decide(EditSelSnap before, EditSelSnap after, long insLen);

// 호출자 규칙: NOT_INSERTED 만 실패다.
bool EditVerdict_Failed(EditVerdict v);
