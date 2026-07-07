#pragma once
#include <windows.h>
#include "config.h"

// ------------------------------------------------------------------
// Settings UI Thread & Window Manager
// ------------------------------------------------------------------

// 설정창을 새 스레드에서 엽니다. 이미 열려있다면 해당 창을 최상단으로 포커스합니다.
// pCurrentConfig: 현재 엔진의 실제 설정 포인터 (적용 시 여기에 덮어씀)
void SettingsUI_Show(JamotongConfig *pCurrentConfig);

// 내부 스레드 종료 및 윈도우 정리
void SettingsUI_Shutdown(void);
