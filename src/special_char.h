#pragma once
#include <windows.h>
#include <stdbool.h>

// 자음(호환 자모 ㄱ~ㅎ) + 한자키 → 특수문자 표. HanjaDict_Find와 같은 형태로 후보 UI에 연결.
// consonant: Layout_ChoToCompatJamo 가 준 호환 자모 코드포인트(예: ㅁ=0x3141).
// 성공 시 *pppCandidates 에 정적 단일문자 문자열 배열, *pCount 에 개수. 없으면 false.
bool SpecialChar_Find(wchar_t consonant, wchar_t ***pppCandidates, int *pCount);
