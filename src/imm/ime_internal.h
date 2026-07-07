// ime_internal.h — IMM32 IME 내부 공유 선언.
#pragma once
#include "immdev_min.h"
#include "../fsm.h"
#include "../config.h"

// 입력 컨텍스트별 private 데이터(INPUTCONTEXT.hPrivate, 크기=IMEINFO.dwPrivateDataSize).
//   조합 오토마타 상태를 컨텍스트별로 보관.
typedef struct {
    FsmContext fsm;
} ImePrivate;

extern HINSTANCE       g_hImeInst;
extern JamotongConfig  g_imeConfig;   // 프로세스 전역 설정
extern CRITICAL_SECTION g_imeLock;

void Ime_EnsureConfig(void);          // ime_main.c
void Ime_PrivInit(ImePrivate *priv);  // ime_process.c
void Ime_HanjaInit(void);             // ime_process.c — 후보창/한자사전 1회 초기화

// 조합 처리 (ime_process.c) — 아래는 IMM 필수 export 이므로 시스템이 직접 호출.
BOOL WINAPI ImeProcessKey(HIMC hIMC, UINT vKey, LPARAM lKeyData, CONST LPBYTE lpbKeyState);
UINT WINAPI ImeToAsciiEx(UINT uVKey, UINT uScanCode, CONST LPBYTE lpbKeyState,
                         LPTRANSMSGLIST lpTransBuf, UINT fuState, HIMC hIMC);
BOOL WINAPI NotifyIME(HIMC hIMC, DWORD dwAction, DWORD dwIndex, DWORD dwValue);
