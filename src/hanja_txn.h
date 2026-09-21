#pragma once
// hanja_txn.h — 한자 변환 트랜잭션 정책 (순수 로직, WinAPI 무의존, RFC-0008 W1-02)
//   확정 전용 경로(EDIT·CUAS·터미널)에서 조합 중 한자키: 원문을 먼저 문서에 넣고 나중에 바꾸면
//   CUAS 의 비동기 전달 탓에 원문+한자가 함께 남거나 앞 글자만 바뀔 수 있다. 그래서 원문은 후보가
//   정해질 때까지 조합으로 **보류**한다(선택=한자만 삽입, 취소=조합 계속). 인라인 조합은 음절이 이미
//   문서 안의 조합이라 확정 후 range 교체를 유지한다.
// 네이티브 테스트: jamotong-private/test/hanja_txn_test.c (T022)
#include <wchar.h>

typedef enum HanjaTxnMode {
    HANJA_FROM_DOCUMENT = 0,        // 조합 없음 — 블록 선택·커서 앞 단어 (문서 텍스트 변환)
    HANJA_HOLD_ORIGINAL = 1,        // 조합 중·확정 전용 경로 — 원문 보류
    HANJA_COMMIT_THEN_REPLACE = 2   // 조합 중·인라인 조합 — 확정 후 교체
} HanjaTxnMode;

HanjaTxnMode HanjaTxn_Mode(int composing, int inline_active);

// 블록 선택 변환: 교체 직전 선택(current)이 한자키 때 읽은 것(expected)과 같을 때만 1.
int HanjaTxn_SelectionStillValid(const wchar_t *expected, const wchar_t *current);
