// layout_icon.h — 자판 아이콘 그리기 (2026-10-04). 표지의 짜임은 layout_icon_style.h.
#pragma once
#include <windows.h>
#include "layout_icon_style.h"
// abbrev 의 아이콘. size 0 = SM_CXSMICON (최소 32). 부른 쪽이 DestroyIcon 한다(언어바는 셸이 소유).
HICON LayoutIcon_Create(const wchar_t *abbrev, int size);
