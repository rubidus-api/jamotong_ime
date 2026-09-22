// popup_style_win.c — 팝업 공통 중 Windows 를 묻는 부분 (RFC-0008 W2-03).
#include "popup_style.h"

bool Popup_WorkAreaAt(int x, int y, RECT *out) {
    POINT pt = { x, y };
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi; mi.cbSize = sizeof(mi);
    if (!mon || !GetMonitorInfoW(mon, &mi)) return false;
    *out = mi.rcWork;
    return true;
}

void Popup_ClampToMonitor(int anchorTop, int w, int h, int *x, int *y) {
    RECT work;
    if (Popup_WorkAreaAt(*x, *y, &work)) Popup_ClampRect(&work, anchorTop, w, h, x, y);
}

// GetDpiForWindow 는 Windows 10 1607+ — 없으면 96 (그 시절 호스트는 DPI 를 모른다고 본다).
typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
UINT Popup_WindowDpi(HWND hwnd) {
    static PFN_GetDpiForWindow fn = NULL;
    static bool looked = false;
    if (!looked) {
        HMODULE u = GetModuleHandleW(L"user32.dll");
        fn = u ? (PFN_GetDpiForWindow)(void (*)(void))GetProcAddress(u, "GetDpiForWindow") : NULL;
        looked = true;
    }
    UINT d = (fn && hwnd) ? fn(hwnd) : 0;
    return d ? d : 96;
}

bool Popup_HighContrast(void) {
    HIGHCONTRASTW hc; hc.cbSize = sizeof(hc);
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
}
