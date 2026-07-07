#pragma once
#include "config.h"

// 통합 자판 설정파일(.jmt) 로더. 파일 첫머리의 `Type =` 값으로 종류를 정한다:
//   Type = static   1:1 문자 리맵 (드보락류)          → LAYOUT_TYPE_STATIC_MAP (charMap)
//   Type = hangul   세벌식 계열 한글 (초/중/종성·결합) → LAYOUT_TYPE_HANGUL_CUSTOM (기본값)
//   Type = chord    조합→동작 (ARTSEY류·레이어·마우스) → LAYOUT_TYPE_CHORD
// 성공 시 out(LayoutConfig)을 채우고 true. name/리소스는 live config가 소유(Config_Free가 해제).
bool Klay_Load(const wchar_t *path, LayoutConfig *out);
