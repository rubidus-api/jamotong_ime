#pragma once
#include <stdbool.h>
#include <wchar.h>

// ── 사전 컴파일러 (원본 .jdt → 이진 .jdb, RFC-0016 P5) ────────────────────────────
// 오너 결정 2026-09-23: 자료는 컴파일을 거친다 — 확인·검증·이진화. 오류는 여기서 전부 나오고,
// 통과한 것만 산출물이 된다. 입력기는 산출물만 읽는다(jdict.h).
//
// 원본(.jdt, UTF-8, 탭으로 나눈 두 칸):
//   JamotongData 1          ← 첫 줄 고정
//   Type = sequence         ← 사전 종류
//   Name = romaji kana      ← 선택: Name / Version / License / Source
//   # 주석
//   ka<탭>か                ← 왼쪽 = 친 글자열, 오른쪽 = 낼 글자(`\u{...}` 표기 허용)
#define JDICT_BUILD_CODE  24
#define JDICT_BUILD_MSG   200

typedef struct JDictBuildResult {
    int     line;                       // 문제가 난 줄 (1부터, 0 = 파일 전체)
    wchar_t code[JDICT_BUILD_CODE];     // 예: E-DICT-ROW
    wchar_t message[JDICT_BUILD_MSG];   // 영어 사유
    wchar_t help[JDICT_BUILD_MSG];      // 고치는 법 (없으면 빈 문자열)
    int     count;                      // 성공 시 구운 항목 수
    int     maxKeyLen, maxValLen;
} JDictBuildResult;

// 원본을 구워 outPath 에 쓴다. 실패하면 false 이고 산출물을 남기지 않는다(반쯤 구운 파일 금지).
bool JDict_Build(const wchar_t *srcPath, const wchar_t *outPath, JDictBuildResult *res);
