// layout_icon_style.h — 트레이·언어 전환창 아이콘의 짜임 (2026-10-04 사용자 요청: "자판별로 어떤 자판인지 잘 알 수 있게 서로
//   구분되는 아이콘 … 오른쪽 아래에 2글자로 언어 이름(ko en cn jp) … 나머지에 자판마다 글자 또는 그림 … 색깔도 모양도 서로
//   다르게 … 다크/라이트 모드 상관없이 잘 보이게 밝은 회색으로 배경").
//   아이콘 = 밝은 회색 둥근 바탕 + 왼쪽 위의 표지(모양·색·글자, 자판마다 다르다) + 오른쪽 아래의 언어 두 글자.
//   이 파일은 "어떤 자판에 어떤 표지"만 정한다(그리기는 langbar.c). 순수 함수라 네이티브 시험이 본다.
#pragma once
#include <stdbool.h>
#include <wchar.h>

enum {
    LI_CIRCLE, LI_SQUARE, LI_DIAMOND, LI_HEXAGON, LI_PENTAGON, LI_TRIANGLE, LI_OCTAGON, LI_RING,
    LI_PARALLELOGRAM, LI_TRIDOWN, LI_STAR, LI_CAPSULE, LI_SHIELD, LI_ROUNDSQ, LI_SHAPES
};

typedef struct LayoutIconStyle {
    int            shape;      // LI_*
    unsigned       rgb;        // 표지의 색 0xRRGGBB
    wchar_t        glyph[4];   // 표지 안의 글자 (한 글자, 또는 숫자 둘)
    const wchar_t *font;       // 그 글자를 그릴 글꼴 (없으면 GDI 가 비슷한 것으로)
    wchar_t        lang[3];    // 오른쪽 아래의 언어: ko en cn jp … (abbrev 앞 두 글자, ZH→cn JA→jp)
} LayoutIconStyle;

#define LI_BACKGROUND_RGB 0xDADADAu   // 밝은 회색 — 어두운 작업 표시줄에서도 밝은 작업 표시줄에서도 보인다(테두리와 함께)
#define LI_BORDER_RGB     0x8A8A8Au
#define LI_LANG_RGB       0x1E1E1Eu

// abbrev(자판 파일의 layout abbrev, "KO2S" 같은 네 글자)로 표지를 정한다. 실린 자판은 표에서, 모르는 것은 abbrev 에서
//   모양·색을 고르고 뒤 두 글자를 글자로 쓴다. "--"(무간섭 모드)는 회색 표지. 늘 true.
bool LayoutIcon_Style(const wchar_t *abbrev, LayoutIconStyle *out);
// 표에 있는 자판의 수와 그 abbrev (시험용)
int            LayoutIcon_KnownCount(void);
const wchar_t *LayoutIcon_KnownAbbrev(int i);
