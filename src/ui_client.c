// ui_client.c — RFC-0015: TIP 쪽. 데스크톱 UI 헬퍼에 "이렇게 그려라"를 보낸다.
//
// 규율 두 가지:
//  1) 입력을 막지 않는다. 헬퍼가 죽었거나 느려도 쓰기 한 번 실패하면 그 자리에서 포기하고
//     호출자가 폴백(순환 변환)하도록 false 를 돌려준다.
//  2) 역방향 채널이 없다. 키 처리·문서 편집은 전부 TIP 이 한다 — 헬퍼는 표시만 한다.
#include "ui_client.h"
#include "ui_ipc.h"
#include "edit_session.h"   // JamoDiag
#include <stdio.h>
#include <string.h>

static HANDLE    g_pipe = INVALID_HANDLE_VALUE;
static ULONGLONG g_retryAfter = 0;   // 실패하면 잠깐 쉬었다 다시 — 헬퍼는 도중에 재시작될 수 있다
#define JAMO_UICLI_COOLDOWN_MS 3000

static void ClosePipe(void) {
    if (g_pipe != INVALID_HANDLE_VALUE) { CloseHandle(g_pipe); g_pipe = INVALID_HANDLE_VALUE; }
}

// 연결(필요할 때만). 실패는 조용히 — 헬퍼는 '있으면 좋은 것'이다.
static bool EnsureConnected(void) {
    if (g_pipe != INVALID_HANDLE_VALUE) return true;
    if (GetTickCount64() < g_retryAfter) return false;   // 쿨다운 중 — 매 키마다 두드리지 않는다

    DWORD sid = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sid)) sid = 0;
    wchar_t name[128];
    _snwprintf(name, 128, JAMO_UIIPC_PIPE_FMT, (unsigned long)sid);
    name[127] = L'\0';

    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        JamoDiag("UICLI connect fail err=%lu", GetLastError());
        g_retryAfter = GetTickCount64() + JAMO_UICLI_COOLDOWN_MS;   // 헬퍼가 아직 없다
        return false;
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(h, &mode, NULL, NULL);
    g_pipe = h;
    JamoDiag("UICLI connected");
    return true;
}

// 헬퍼가 재시작되면 들고 있던 파이프는 죽어 있다(ERROR_NO_DATA/BROKEN_PIPE). 그때는 닫고
// **한 번 더** 연결해 본다 — 이걸 안 하면 헬퍼를 다시 띄워도 영영 안 붙는다.
static bool WriteOnce(const void *buf, DWORD len) {
    DWORD wrote = 0;
    if (WriteFile(g_pipe, buf, len, &wrote, NULL) && wrote == len) return true;
    JamoDiag("UICLI write fail err=%lu", GetLastError());
    ClosePipe();
    return false;
}

static bool WriteAll(const void *buf, DWORD len) {
    if (!EnsureConnected()) return false;
    if (WriteOnce(buf, len)) return true;
    if (!EnsureConnected()) return false;   // 죽은 파이프였다 → 새로 붙어 다시 한 번
    if (WriteOnce(buf, len)) return true;
    g_retryAfter = GetTickCount64() + JAMO_UICLI_COOLDOWN_MS;
    return false;
}

bool UiClient_Available(void) {
    return EnsureConnected();
}

bool UiClient_Show(const wchar_t *const *lines, int count, int perPage, int selection,
                   int anchorX, int anchorY, int caretTop,
                   const wchar_t *fontFace, int fontSize) {
    if (count <= 0 || !lines) return false;
    if (count > JAMO_UIIPC_MAX_CAND) count = JAMO_UIIPC_MAX_CAND;

    // 헤더 + 고정폭 문자열들을 한 메시지로 (파이프가 message mode 라 경계는 보장된다)
    size_t bodyChars = (size_t)count * JAMO_UIIPC_MAX_CANDLEN;
    size_t total = sizeof(JamoUiMsgHeader) + bodyChars * sizeof(wchar_t);
    unsigned char *buf = (unsigned char*)calloc(1, total);
    if (!buf) return false;

    JamoUiMsgHeader *h = (JamoUiMsgHeader*)buf;
    h->version = JAMO_UIIPC_VERSION;
    h->msg = JAMO_UIMSG_SHOW;
    h->count = (UINT32)count;
    h->perPage = (UINT32)(perPage > 0 ? perPage : 9);
    h->selection = (UINT32)(selection >= 0 ? selection : 0);
    h->anchorX = anchorX; h->anchorY = anchorY; h->caretTop = caretTop;
    h->fontSize = (UINT32)(fontSize > 0 ? fontSize : 0);
    if (fontFace && fontFace[0]) {
        wcsncpy(h->fontFace, fontFace, JAMO_UIIPC_MAX_FACE - 1);
        h->fontFace[JAMO_UIIPC_MAX_FACE - 1] = L'\0';
    }
    wchar_t *body = (wchar_t*)(buf + sizeof(JamoUiMsgHeader));
    for (int i = 0; i < count; i++) {
        wchar_t *slot = body + (size_t)i * JAMO_UIIPC_MAX_CANDLEN;
        if (lines[i]) {
            wcsncpy(slot, lines[i], JAMO_UIIPC_MAX_CANDLEN - 1);
            slot[JAMO_UIIPC_MAX_CANDLEN - 1] = L'\0';
        }
    }
    bool ok = WriteAll(buf, (DWORD)total);
    free(buf);
    JamoDiag("UICLI show count=%d ok=%d", count, (int)ok);
    return ok;
}

bool UiClient_Update(int selection, int perPage) {
    JamoUiMsgHeader h;
    memset(&h, 0, sizeof(h));
    h.version = JAMO_UIIPC_VERSION;
    h.msg = JAMO_UIMSG_UPDATE;
    h.selection = (UINT32)(selection >= 0 ? selection : 0);
    h.perPage = (UINT32)(perPage > 0 ? perPage : 9);
    return WriteAll(&h, sizeof(h));
}

void UiClient_Hide(void) {
    if (g_pipe == INVALID_HANDLE_VALUE) return;   // 연결한 적 없으면 할 일 없음
    JamoUiMsgHeader h;
    memset(&h, 0, sizeof(h));
    h.version = JAMO_UIIPC_VERSION;
    h.msg = JAMO_UIMSG_HIDE;
    WriteAll(&h, sizeof(h));
}

void UiClient_Reset(void) {
    ClosePipe();
    g_retryAfter = 0;
}
