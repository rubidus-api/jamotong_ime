#pragma once
#include <windows.h>
#include <stdbool.h>

// 한자 사전 엔트리
typedef struct {
    wchar_t *hangul;       // 한글 (예: "학교")
    wchar_t **candidates;  // 한자 후보 배열 (예: ["學校"])
    int candidateCount;    // 후보 개수
} HanjaEntry;

// 한자 사전 초기화 및 로드 (정렬 수행)
bool HanjaDict_Load(const wchar_t *filepath);

// 한자 사전 해제
void HanjaDict_Free(void);

// 한글 단어로 한자 찾기 (이진 탐색)
// 찾으면 true 반환 및 ppCandidates와 pCount 설정
bool HanjaDict_Find(const wchar_t *hangul, wchar_t ***pppCandidates, int *pCount);

// ── 훈음(뜻·음) 표 (hanja_hunum.txt, "字:훈 음" 형식) ─────────────────────────────
bool HunumDict_Load(const wchar_t *filepath);
void HunumDict_Free(void);
const wchar_t *HunumDict_Find(wchar_t hanja);   // "집 가" 또는 NULL(미수록)
