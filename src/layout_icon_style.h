// layout_icon_style.h — 트레이·언어 전환창 아이콘의 짜임.
//   2026-10-03 오너 지시: "위에 언어 코드, 아래에 자판 이름 2글자". 2026-10-04 에 도형 표지로 바꿨다가, 작은 아이콘에서 알아보기
//   어렵다는 오너 요청(2026-11-12 "기존 2x2 글자로 … 가독성")으로 다시 2x2 글자로 돌아간다.
//   2026-11-13 오너 요청: 배경은 하나(밝은 회색)로 통일하고, 언어 글자색은 언어마다, 자판 글자색은 자판마다 다르게 한다(밝은 배경 - 어두운 글씨).
//   아이콘 = 위 줄: 언어 두 글자(언어 색) / 아래 줄: 자판 두 글자(자판 색), 바깥에 1점 여백.
//   이 파일은 "어떤 자판에 어떤 글자·색"만 정한다(그리기는 layout_icon.c). 순수 함수라 네이티브 시험이 본다.
#pragma once
#include <stdbool.h>
#include <wchar.h>

typedef struct LayoutIconStyle {
    wchar_t  top[3];      // 위 줄: 언어 (KO EN CN JP …; abbrev 앞 두 글자, ZH→CN JA→JP). 대문자 ASCII
    wchar_t  bottom[3];   // 아래 줄: 자판 (abbrev 뒤 두 글자). 대문자 ASCII
    unsigned langRgb;     // 위 줄 글자색 0xRRGGBB — 밝은 배경에서 읽히도록 이미 어둡게 맞춰 두었다
    unsigned rgb;         // 아래 줄(자판) 글자색 0xRRGGBB — 같은 방식으로 어둡게 맞춰 두었다
} LayoutIconStyle;

#define LI_BACKGROUND_RGB 0xDADADAu   // 바탕(전체 하나): 밝은 회색 — 어두운·밝은 작업 표시줄 모두에서 보인다
#define LI_MIN_CONTRAST_LUM 0.1235    // 글자색의 (감마 2 근사) 휘도 상한: 바탕과의 대비 ≥ 4.5

// abbrev(자판 파일의 layout abbrev, "KO2S" 같은 네 글자)로 글자와 색을 정한다. 실린 자판은 표의 색, 모르는 것은 abbrev 에서
//   색을 고른다(같은 abbrev 는 늘 같은 아이콘). "--"(무간섭 모드)는 "--"/"--" 회색 띠. 늘 true.
bool LayoutIcon_Style(const wchar_t *abbrev, LayoutIconStyle *out);
// 표에 있는 자판의 수와 그 abbrev (시험용)
int            LayoutIcon_KnownCount(void);
const wchar_t *LayoutIcon_KnownAbbrev(int i);
