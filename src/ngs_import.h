#pragma once
#include <stdbool.h>
#include <wchar.h>

// ── 날개셋 자판 파일(`.key`/`.ist`) → 우리 자판 원본 `.jmt` (RFC-0011b) ───────────────
// **도구 전용**. 날개셋 한글 입력기는 자판을 두 가지로 나눠 준다:
//   `.key` — 글쇠 배열만 (자판 47글쇠의 94자리에 무엇이 들어가는지)
//   `.ist` — 입력기 유형 전체 (글쇠 배열 + 글쇠 인식·낱자 처리·오토마타 설정)
// 두 파일 모두 첫머리가 다를 뿐 **글쇠 배열 토막은 같은 꼴**이라, 그 토막만 읽어 옮긴다.
//   옮기는 것: 낱자(초성·중성·종성) 글쇠, 그리고 낱자가 하나도 없으면 글자 글쇠(정적 자판).
//   옮기지 않는 것(세어서 알려 준다): 날개셋 수식 글쇠값(조건에 따라 달라지는 자리),
//   한글 자판으로 옮길 때의 글자 글쇠, 우리가 모르는 낱자 코드(옛한글 등), 오토마타·옵션 일체.
typedef struct NgsImportResult {
    wchar_t name[64];          // 자판 이름 (파일이 가진 이름, 없으면 파일 이름)
    int keys;                  // 글쇠 자리 수 (보통 94: '!'~'~')
    int mapped;                // 옮긴 글쇠
    int formulas;              // 날개셋 수식이라 건너뛴 자리
    int chars;                 // 글자 글쇠 (한글 자판으로 옮길 때는 버린다)
    int unknown;               // 우리가 모르는 낱자 코드
    bool hangul;               // 한글 자판(Type = hangul)으로 썼는가 — 아니면 정적 자판
    bool sebeol;               // 종성 글쇠가 있어 세벌식으로 적었는가
    wchar_t unknownList[120];  // 모르는 코드 몇 개 (예: "M26 M44 T125")
    wchar_t error[200];        // 실패 사유 (성공이면 빈 문자열)
} NgsImportResult;

bool NgsImport_Run(const wchar_t *srcPath, const wchar_t *outPath, NgsImportResult *res);
