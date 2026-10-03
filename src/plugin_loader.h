#pragma once
#include "config.h"

void PluginLoader_LoadAll(JamotongConfig *config);
// 본문을 나중에 읽기 (RFC-0020 F1) — 입력기 DLL 이 처음에 한 번 켠다.
void PluginLoader_SetDeferBodies(bool on);
