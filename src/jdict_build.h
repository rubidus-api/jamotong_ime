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
// 연결 비용 파일 (.jdc, RFC-0022): Mozc 의 connection_single_column.txt(첫 줄 N, 그 뒤 N×N 줄의 비용, [rid][lid] 차례)를
//   한 칸 1바이트(비용 / step, 255 까지)로 굽는다.
bool JConn_Build(const wchar_t *srcPath, const wchar_t *outPath, int step, JDictBuildResult *res);
// 품사 종류까지 (.jdc 판 2): idDefPath = Mozc 의 id.def ("id 품사,…" 줄, UTF-8). NULL 이면 판 1 그대로.
bool JConn_BuildEx(const wchar_t *srcPath, const wchar_t *idDefPath, const wchar_t *outPath, int step, JDictBuildResult *res);
