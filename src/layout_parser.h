#pragma once
#include <windows.h>
#include <stdbool.h>
#include "config.h"

// .jamo 텍스트 레이아웃 설정 파일을 읽어 LayoutConfig 배열에 추가합니다.
// 파일 형식 예:
// [Info]
// Name=en_dvorak
// [Map]
// q='
// Q="
// w=,
// W=<
bool LayoutParser_LoadJamo(JamotongConfig *config, const wchar_t *filepath);
