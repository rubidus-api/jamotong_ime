// jamo_class.c — 창 클래스 등록/해제 (RFC-0008 W2-05). DLL 과 관리자 exe 가 함께 쓴다.
#include "jamo_class.h"

extern HINSTANCE g_hInst;   // DLL = dllmain.c, exe = tray_app.c

// 우리가 등록하는 창 클래스 (Jamo_EnsureClass 로만 등록한다)
static const wchar_t *const kClasses[] = {
    L"JamotongCandidateUI", L"JamotongSettingsClass", L"JamotongCaptureClass",
    L"JamotongPreeditOverlay", L"JamotongCodeInput", NULL };

bool Jamo_EnsureClass(const WNDCLASSW *wc) {
    WNDCLASSW have;
    if (GetClassInfoW(wc->hInstance, wc->lpszClassName, &have)) return true;
    return RegisterClassW(wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void Jamo_UnregisterClasses(void) {
    // 동적 언로드 전에 해제한다(MSDN: DLL이 등록한 클래스는 언로드 시 자동 해제되지 않음). 안 하면
    // WndProc 이 언로드된 코드를 가리키는 낡은 클래스가 남아, 다른 주소로 재로드된 뒤 CreateWindow 가
    // 댕글링 WndProc 을 부른다. 창이 남아 있으면 UnregisterClass 가 실패하므로 해가 없다.
    for (int i = 0; kClasses[i]; i++) UnregisterClassW(kClasses[i], g_hInst);
}

