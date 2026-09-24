#pragma once
#include "lowlay_check.h"
#include "hangul_layout.h"
#include "config.h"

// ── 자판 정의 언어 v4 → 엔진이 쓰는 표 (RFC-0018 P3) ────────────────────────────
// 검사까지 끝난 폼 트리를 **실제 자판**으로 짓는다.
//   map jamo          → 두 벌 (자음은 초성 자리에 넣고, 받침 배치는 오토마타가 한다)
//   map cho/mid/jong  → 세 벌 (자리를 못 박는다)
//   combine           → 낱자 결합표 (겹받침·겹모음·된소리)
// 낱자는 자모 글자로 적혀 있다(`"ㄱ"`). 번호로 적은 파일도 받는다.
bool LowBuild_IsV4(const wchar_t *src);                     // 첫 `layout` 폼이 있으면 v4
// 파일이 v4 면 그 원문을 돌려준다(부르는 쪽이 free). 아니면 NULL —
// v4 에는 Extends·Include 가 없으므로 `--expand` 는 원문 그대로가 곧 펼친 꼴이다.
wchar_t *LowBuild_ReadIfV4(const wchar_t *path);
// 조합 자판(chord/hold/keys 가 있는 파일)을 엔진 표로 — lowlay_chord.c
struct ChordLayout;
bool LowBuild_Chord(const LowTree *t, const LowCheckResult *c, struct ChordLayout *cl, KlayDiag *diag);
bool LowBuild_Hangul(const LowTree *t, const LowCheckResult *c, HangulLayout *out, KlayDiag *diag);
// 파일 하나를 읽어 LayoutConfig 로. v4 가 아니면 거짓(그러면 호출자가 옛 경로로 간다).
// isV4 로 "이 파일이 v4 였는가"를 알린다 — v4 인데 실패하면 호출자가 옛 경로로 되돌아가면 안 된다.
bool LowBuild_LoadFile(const wchar_t *path, LayoutConfig *out, KlayDiag *diag, bool *isV4);
