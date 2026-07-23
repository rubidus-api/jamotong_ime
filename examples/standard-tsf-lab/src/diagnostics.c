/* diagnostics.c — 구조 진단 로그 (RFC-0009 §11)
 *
 * 개인정보 원칙(§11.2): 사용자가 친 글자를 기록하지 않는다. 길이와 코드포인트 종류만 남긴다.
 * 기본은 꺼져 있다. 환경변수 JAMOTONG_LAB_TRACE=1 일 때만 켜진다(§11.3).
 * 파일: %TEMP%\jamotong-lab-trace.log
 */
#include "lab_tip.h"
#include <stdio.h>
#include <stdarg.h>

static int   g_trace_state = -1;      /* -1 미확인, 0 꺼짐, 1 켜짐 */
static FILE *g_trace_file  = NULL;

static bool TraceEnabled(void) {
    if (g_trace_state < 0) {
        WCHAR buf[8];
        DWORD n = GetEnvironmentVariableW(L"JAMOTONG_LAB_TRACE", buf, 8);
        g_trace_state = (n > 0 && buf[0] == L'1') ? 1 : 0;
        if (g_trace_state) {
            WCHAR path[MAX_PATH];
            DWORD t = GetTempPathW(MAX_PATH, path);
            if (t > 0 && t < MAX_PATH - 32) {
                wcscat_s(path, MAX_PATH, L"jamotong-lab-trace.log");
                g_trace_file = _wfopen(path, L"a, ccs=UTF-8");
            }
        }
    }
    return g_trace_state == 1 && g_trace_file != NULL;
}

void Lab_Trace(const char *fmt, ...) {
    if (!TraceEnabled()) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fwprintf(g_trace_file, L"%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    char line[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fwprintf(g_trace_file, L"%hs\n", line);
    fflush(g_trace_file);              /* crash해도 남게 (§11.5) */
}

/* 편집 세션 결과 — 요청 성공과 세션 성공을 나눠 남긴다(§5.3).
   "왜 실패했는지"를 이 셋의 조합으로 재구성할 수 있어야 한다. */
void Lab_TraceSession(const char *what, const LabSessionResult *r) {
    if (!TraceEnabled()) return;
    Lab_Trace("%s request_hr=0x%08lX session_hr=0x%08lX callback_ran=%d",
              what, (unsigned long)r->request_hr, (unsigned long)r->session_hr,
              r->callback_ran ? 1 : 0);
}

/* 호스트 능력 스냅샷 — 어느 인터페이스가 없는지가 실패 원인인 경우가 많다 */
void Lab_TraceContextCaps(ITfContext *ctx) {
    if (!TraceEnabled() || !ctx) return;
    void *p = NULL;
    int ias = SUCCEEDED(ITfContext_QueryInterface(ctx, &IID_ITfInsertAtSelection, &p));
    if (p) { ((IUnknown*)p)->lpVtbl->Release((IUnknown*)p); p = NULL; }
    int cc  = SUCCEEDED(ITfContext_QueryInterface(ctx, &IID_ITfContextComposition, &p));
    if (p) { ((IUnknown*)p)->lpVtbl->Release((IUnknown*)p); p = NULL; }
    TF_STATUS status; ZeroMemory(&status, sizeof status);
    HRESULT hs = ITfContext_GetStatus(ctx, &status);
    Lab_Trace("caps InsertAtSelection=%d ContextComposition=%d status_hr=0x%08lX "
              "dwDynamicFlags=0x%08lX dwStaticFlags=0x%08lX",
              ias, cc, (unsigned long)hs,
              (unsigned long)status.dwDynamicFlags, (unsigned long)status.dwStaticFlags);
}
