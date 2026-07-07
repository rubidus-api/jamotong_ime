#include "jamotong_plugin.h"
#include <windows.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    wchar_t composing[64];
    int compLen;
    bool showCandidates;
    HWND hwndCandidate;
} TestPluginContext;

LRESULT CALLBACK CandidateWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW+1));
        
        TestPluginContext* ctx = (TestPluginContext*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (ctx) {
            TextOutW(hdc, 2, 2, L"1. 漢字", 4);
            TextOutW(hdc, 2, 22, L"2. 幹事", 4);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

HRESULT WINAPI JamoPlugin_Initialize(int nVersion, const JAMOTONG_HOST_CALLBACKS* pCallbacks, void** ppvContext) {
    (void)pCallbacks;
    if (nVersion != JAMOTONG_PLUGIN_API_VERSION) return E_INVALIDARG;
    
    TestPluginContext* ctx = (TestPluginContext*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TestPluginContext));
    
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = CandidateWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"JamotongCandidateWnd";
    RegisterClassW(&wc);
    
    *ppvContext = ctx;
    return S_OK;
}

JAMOTONG_PLUGIN_RESULT WINAPI JamoPlugin_ProcessKey(void* pvContext, WPARAM wParam, LPARAM lParam, const BYTE* pbKeyState, const RECT* prcCursor) {
    (void)pbKeyState;
    TestPluginContext* ctx = (TestPluginContext*)pvContext;
    JAMOTONG_PLUGIN_RESULT res = {0};
    res.hr = S_OK;
    
    bool isDown = (lParam & (1 << 31)) == 0;
    if (!isDown) {
        res.bEaten = FALSE;
        return res;
    }
    
    if (ctx->showCandidates) {
        if (wParam == VK_SPACE || wParam == '1') {
            wcscpy(res.wszCommitted, L"漢字");
            ctx->compLen = 0;
            ctx->composing[0] = 0;
            ctx->showCandidates = false;
            if (ctx->hwndCandidate) {
                DestroyWindow(ctx->hwndCandidate);
                ctx->hwndCandidate = NULL;
            }
            res.bEaten = TRUE;
            return res;
        } else if (wParam == '2') {
            wcscpy(res.wszCommitted, L"幹事");
            ctx->compLen = 0;
            ctx->composing[0] = 0;
            ctx->showCandidates = false;
            if (ctx->hwndCandidate) {
                DestroyWindow(ctx->hwndCandidate);
                ctx->hwndCandidate = NULL;
            }
            res.bEaten = TRUE;
            return res;
        }
        res.bEaten = TRUE;
        return res;
    }
    
    if (wParam >= 'A' && wParam <= 'Z') {
        if (ctx->compLen < 60) {
            ctx->composing[ctx->compLen++] = (wchar_t)wParam + 32; // lowercase
            ctx->composing[ctx->compLen] = 0;
            wcscpy(res.wszComposing, ctx->composing);
            res.bEaten = TRUE;
            return res;
        }
    } else if (wParam == VK_SPACE && ctx->compLen > 0) {
        ctx->showCandidates = true;
        wcscpy(res.wszComposing, ctx->composing);
        res.bEaten = TRUE;
        
        if (!ctx->hwndCandidate) {
            int x = prcCursor ? prcCursor->left : 0;
            int y = prcCursor ? prcCursor->bottom : 0;
            if (x == 0 && y == 0) { x = 100; y = 100; }
            ctx->hwndCandidate = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                L"JamotongCandidateWnd", L"", WS_POPUP | WS_VISIBLE | WS_BORDER,
                x, y, 100, 50, NULL, NULL, GetModuleHandle(NULL), NULL);
            SetWindowLongPtr(ctx->hwndCandidate, GWLP_USERDATA, (LONG_PTR)ctx);
        }
        return res;
    }
    
    res.bEaten = FALSE;
    return res;
}

JAMOTONG_PLUGIN_RESULT WINAPI JamoPlugin_Flush(void* pvContext) {
    TestPluginContext* ctx = (TestPluginContext*)pvContext;
    JAMOTONG_PLUGIN_RESULT res = {0};
    res.hr = S_OK;
    if (ctx->compLen > 0) {
        wcscpy(res.wszCommitted, ctx->composing);
        ctx->compLen = 0;
        ctx->composing[0] = 0;
    }
    if (ctx->hwndCandidate) {
        DestroyWindow(ctx->hwndCandidate);
        ctx->hwndCandidate = NULL;
    }
    return res;
}

HRESULT WINAPI JamoPlugin_Command(void* pvContext, UINT uCmd, void* pData) {
    (void)pvContext; (void)uCmd; (void)pData;
    return S_OK;
}

HRESULT WINAPI JamoPlugin_Uninitialize(void* pvContext) {
    TestPluginContext* ctx = (TestPluginContext*)pvContext;
    if (ctx->hwndCandidate) DestroyWindow(ctx->hwndCandidate);
    HeapFree(GetProcessHeap(), 0, ctx);
    return S_OK;
}
