#pragma once
#include "config.h"

// .jmt 로드 실패 진단. 파서가 첫 오류의 줄 번호와 사유(영어)를 채운다. line 0 = 파일 열기 실패.
typedef struct {
    int     line;          // 1-based 오류 줄 (0 = 파일 열기 실패/미상)
    wchar_t message[160];  // 영어 사유 (예: "choseong index out of range (0..18)")
} KlayDiag;

// 통합 자판 설정파일(.jmt) 로더. 파일 첫머리의 `Type =` 값으로 종류를 정한다:
//   Type = static   1:1 문자 리맵 (드보락류)          → LAYOUT_TYPE_STATIC_MAP (charMap)
//   Type = hangul   세벌식 계열 한글 (초/중/종성·결합) → LAYOUT_TYPE_HANGUL_CUSTOM (기본값)
//   Type = chord    조합→동작 (ARTSEY류·레이어·마우스) → LAYOUT_TYPE_CHORD
// 성공 시 out(LayoutConfig)을 채우고 true. name/리소스는 live config가 소유(Config_Free가 해제).
// diag(NULL 허용): 실패 시 첫 오류의 줄/사유를 채운다.
bool Klay_Load(const wchar_t *path, LayoutConfig *out, KlayDiag *diag);
