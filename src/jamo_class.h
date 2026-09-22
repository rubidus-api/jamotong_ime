#pragma once
// jamo_class.h — 이 DLL 의 창 클래스 등록/해제 (RFC-0008 W2-05).
// 필요할 때마다 "없으면 등록"하고, 해제는 DllCanUnloadNow 가 S_OK 일 때 한다(DllMain 에서 User32 금지).
#include <windows.h>
#include <stdbool.h>
bool Jamo_EnsureClass(const WNDCLASSW *wc);
void Jamo_UnregisterClasses(void);
