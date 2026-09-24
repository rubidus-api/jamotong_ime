#pragma once
#include "klay.h"
#include <stdbool.h>
#include <wchar.h>

// ── 자판 정의 언어 v4 — 어휘기 (RFC-0018 P1) ────────────────────────────────────
// 겉모습은 로우엔트에서 가져왔다:
//   주석  `rem` 줄 끝까지 · `note 태그 … 태그` 여러 줄
//   닫개  ` .` 는 폼을 닫는다. 개행은 닫지 않는다 (`do` 가 머리 폼을 닫는 것은 파서 몫)
//   구두점  점과 괄호뿐. `=` `#` `&&` 같은 기호는 이 언어에 없다
//   리터럴  42 · 1_000 · 0x2A · 0b1010 · 'a' · "글월" · 접두사 u/U · 이스케이프 닫힌 열넷
// 팔진은 없다(앞의 0 은 십진). 부동소수도 없다 — 숫자 뒤의 점은 언제나 닫개다.
typedef enum LowTokKind {
    LOW_EOF = 0, LOW_NAME, LOW_INT, LOW_CHAR, LOW_STR, LOW_DOT, LOW_LP, LOW_RP
} LowTokKind;

#define LOW_NAME_MAX 32
#define LOW_STR_MAX  512

typedef struct LowTok {
    LowTokKind kind;
    int        line, col;                 // 1-based
    wchar_t    name[LOW_NAME_MAX];        // LOW_NAME
    long long  num;                       // LOW_INT · LOW_CHAR (문자는 코드값)
    wchar_t   *str;                       // LOW_STR (소유권은 LowTokens)
    int        strLen;                    // str 의 wchar_t 개수
    wchar_t    prefix;                    // 0 · 'u' · 'U'  (문자·문자열에만)
} LowTok;

typedef struct LowTokens { LowTok *v; int n, cap; } LowTokens;

// 소스 한 덩이를 토큰으로. 오류가 있으면 거짓이고 diag 에 사유가 쌓인다(최대 KLAY_DIAG_MAX).
bool LowLex_Run(const wchar_t *src, LowTokens *out, KlayDiag *diag);
void LowTokens_Free(LowTokens *t);
