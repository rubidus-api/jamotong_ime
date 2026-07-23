/* diagnostics.c — privacy-preserving structural trace (RFC-0009 §11)
 *
 * The trace variant is always enabled. The normal lab remains opt-in with
 * JAMOTONG_LAB_TRACE=1. Records never contain entered text, key values, pointer
 * values, document contents, paths, or command lines.
 */
#include "lab_tip.h"
#include <stdio.h>

static INIT_ONCE g_trace_once = INIT_ONCE_STATIC_INIT;
static SRWLOCK   g_trace_lock = SRWLOCK_INIT;
static FILE     *g_trace_file = NULL;
static int       g_trace_enabled = 0;
static volatile LONG g_trace_sequence = 0;

static BOOL CALLBACK InitTrace(PINIT_ONCE once, PVOID parameter, PVOID *context) {
    (void)once;
    (void)parameter;
    (void)context;

#ifdef LAB_ALWAYS_TRACE
    g_trace_enabled = 1;
#else
    WCHAR enabled[8];
    DWORD n = GetEnvironmentVariableW(L"JAMOTONG_LAB_TRACE", enabled,
                                      (DWORD)(sizeof enabled / sizeof enabled[0]));
    g_trace_enabled = (n > 0 && enabled[0] == L'1') ? 1 : 0;
#endif
    if (!g_trace_enabled) return TRUE;

    WCHAR path[MAX_PATH];
    DWORD used = GetTempPathW(MAX_PATH, path);
    if (used == 0 || used >= MAX_PATH) {
        g_trace_enabled = 0;
        return TRUE;
    }

#ifdef LAB_ALWAYS_TRACE
#ifdef LAB_TRACE_BUILD
    int written = swprintf(path + used, MAX_PATH - used,
                           L"jamotong-tsf-trace-%lu.jsonl",
                           (unsigned long)GetCurrentProcessId());
#else
    int written = swprintf(path + used, MAX_PATH - used,
                           LAB_TRACE_FILE_FORMAT,
                           (unsigned long)GetCurrentProcessId());
#endif
#else
    int written = swprintf(path + used, MAX_PATH - used,
                           L"jamotong-standard-lab-trace-%lu.jsonl",
                           (unsigned long)GetCurrentProcessId());
#endif
    if (written < 0 || (DWORD)written >= MAX_PATH - used) {
        g_trace_enabled = 0;
        return TRUE;
    }

    g_trace_file = _wfopen(path, L"ab");
    if (!g_trace_file) g_trace_enabled = 0;
    return TRUE;
}

static bool TraceEnabled(void) {
    InitOnceExecuteOnce(&g_trace_once, InitTrace, NULL, NULL);
    return g_trace_enabled != 0 && g_trace_file != NULL;
}

static const char *PhaseName(LabTracePhase phase) {
    switch (phase) {
    case LAB_TRACE_REQUEST:           return "request";
    case LAB_TRACE_EDIT_SESSION:      return "edit_session";
    case LAB_TRACE_GET_RANGE:         return "get_range";
    case LAB_TRACE_INSERT_QUERY:      return "insert_query";
    case LAB_TRACE_INSERT_TEXT:       return "insert_text";
    case LAB_TRACE_START_COMPOSITION: return "start_composition";
    case LAB_TRACE_SET_TEXT:          return "set_text";
    case LAB_TRACE_DISPLAY_ATTRIBUTE: return "display_attribute";
    case LAB_TRACE_SET_SELECTION:     return "set_selection";
    case LAB_TRACE_COMMIT_PREFIX:     return "commit_prefix";
    case LAB_TRACE_END_COMPOSITION:   return "end_composition";
    default:                          return "none";
    }
}

static void WriteCommon(const char *event, const LabContextState *st,
                        HRESULT hr, LONG value) {
    LONG seq = InterlockedIncrement(&g_trace_sequence);
    DWORD pid = GetCurrentProcessId();
    DWORD tid = GetCurrentThreadId();
    ULONGLONG tick = GetTickCount64();
    uint32_t ctx = st ? st->generation : 0;
    uint32_t txn = st ? st->active_transaction : 0;
    uint32_t epoch = st ? st->termination_epoch : 0;
    int composition = st && st->composition ? 1 : 0;
    LabTracePhase phase = st ? st->trace_phase : LAB_TRACE_NONE;

    /* event is always a source-code constant, never user-controlled data. */
    fprintf(g_trace_file,
            "{\"schema\":1,\"seq\":%ld,\"pid\":%lu,\"tid\":%lu,"
            "\"tick_ms\":%llu,\"variant\":\"%s\",\"event\":\"%s\","
            "\"ctx\":%u,\"txn\":%u,"
            "\"phase\":\"%s\",\"hr\":\"0x%08lX\",\"value\":%ld,"
            "\"composition\":%d,\"termination_epoch\":%u}\n",
            (long)seq, (unsigned long)pid, (unsigned long)tid,
            (unsigned long long)tick, LAB_TRACE_VARIANT, event, ctx, txn, PhaseName(phase),
            (unsigned long)hr, (long)value, composition, epoch);
}

void Lab_TraceEvent(const char *event, const LabContextState *st, HRESULT hr, LONG value) {
    if (!TraceEnabled()) return;
    AcquireSRWLockExclusive(&g_trace_lock);
    WriteCommon(event, st, hr, value);
    fflush(g_trace_file);
    ReleaseSRWLockExclusive(&g_trace_lock);
}

void Lab_TraceSession(const char *what, const LabContextState *st,
                      const LabSessionResult *r, HRESULT inner_hr) {
    if (!TraceEnabled()) return;
    AcquireSRWLockExclusive(&g_trace_lock);
    LONG seq = InterlockedIncrement(&g_trace_sequence);
    fprintf(g_trace_file,
            "{\"schema\":1,\"seq\":%ld,\"pid\":%lu,\"tid\":%lu,"
            "\"tick_ms\":%llu,\"variant\":\"%s\","
            "\"event\":\"edit_session.result\","
            "\"command\":\"%s\",\"ctx\":%u,\"txn\":%u,\"phase\":\"%s\","
            "\"request_hr\":\"0x%08lX\",\"session_hr\":\"0x%08lX\","
            "\"inner_hr\":\"0x%08lX\",\"callback_ran\":%d,"
            "\"composition\":%d,\"termination_epoch\":%u}\n",
            (long)seq, (unsigned long)GetCurrentProcessId(),
            (unsigned long)GetCurrentThreadId(),
            (unsigned long long)GetTickCount64(), LAB_TRACE_VARIANT, what,
            st ? st->generation : 0, st ? st->active_transaction : 0,
            PhaseName(st ? st->trace_phase : LAB_TRACE_NONE),
            (unsigned long)r->request_hr, (unsigned long)r->session_hr,
            (unsigned long)inner_hr, r->callback_ran ? 1 : 0,
            st && st->composition ? 1 : 0, st ? st->termination_epoch : 0);
    fflush(g_trace_file);
    ReleaseSRWLockExclusive(&g_trace_lock);
}

void Lab_TraceContextCaps(const LabContextState *st) {
    if (!TraceEnabled() || !st || !st->context) return;

    void *object = NULL;
    int insert_at_selection =
        SUCCEEDED(ITfContext_QueryInterface(st->context, &IID_ITfInsertAtSelection, &object));
    if (object) {
        ((IUnknown*)object)->lpVtbl->Release((IUnknown*)object);
        object = NULL;
    }
    int context_composition =
        SUCCEEDED(ITfContext_QueryInterface(st->context, &IID_ITfContextComposition, &object));
    if (object) {
        ((IUnknown*)object)->lpVtbl->Release((IUnknown*)object);
        object = NULL;
    }

    TF_STATUS status;
    ZeroMemory(&status, sizeof status);
    HRESULT status_hr = ITfContext_GetStatus(st->context, &status);

    AcquireSRWLockExclusive(&g_trace_lock);
    LONG seq = InterlockedIncrement(&g_trace_sequence);
    fprintf(g_trace_file,
            "{\"schema\":1,\"seq\":%ld,\"pid\":%lu,\"tid\":%lu,"
            "\"tick_ms\":%llu,\"variant\":\"%s\","
            "\"event\":\"context.caps\",\"ctx\":%u,"
            "\"txn\":%u,\"phase\":\"%s\",\"insert_at_selection\":%d,"
            "\"context_composition\":%d,\"status_hr\":\"0x%08lX\","
            "\"dynamic_flags\":%lu,\"static_flags\":%lu}\n",
            (long)seq, (unsigned long)GetCurrentProcessId(),
            (unsigned long)GetCurrentThreadId(),
            (unsigned long long)GetTickCount64(), LAB_TRACE_VARIANT, st->generation,
            st->active_transaction, PhaseName(st->trace_phase),
            insert_at_selection, context_composition, (unsigned long)status_hr,
            (unsigned long)status.dwDynamicFlags, (unsigned long)status.dwStaticFlags);
    fflush(g_trace_file);
    ReleaseSRWLockExclusive(&g_trace_lock);
}
