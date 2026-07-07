// immdev_min.h — IMM32 IME 개발용 최소 선언.
//   MinGW의 imm.h(클라이언트)에는 IME측 구조체/함수가 없다(원래 WDK의 immdev.h 소관).
//   여기서 Microsoft 문서화된 ABI 그대로 직접 선언한다. (라이선스: Wine/ReactOS 소스 복사 금지 —
//   구조체 레이아웃은 공표된 ABI 사실이므로 MS 문서 기준으로 재작성.)
//   imm.h 가 제공하는 것: HIMC, HIMCC, CANDIDATEFORM, COMPOSITIONFORM, CANDIDATELIST, REGISTERWORD,
//   STYLEBUFW, IMEMENUITEMINFOW, 그리고 상수(IME_PROP_*, GCS_*, SCS_*, NI_*, CPS_*, IME_CMODE_* 등).
#pragma once
#include <windows.h>
#include <imm.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── IMEINFO (ImeInquire 반환) ────────────────────────────────────────────────────
typedef struct tagIMEINFO {
    DWORD dwPrivateDataSize;
    DWORD fdwProperty;
    DWORD fdwConversionCaps;
    DWORD fdwSentenceCaps;
    DWORD fdwUICaps;
    DWORD fdwSCSCaps;
    DWORD fdwSelectCaps;
} IMEINFO, *PIMEINFO, *NPIMEINFO, *LPIMEINFO;

// ── COMPOSITIONSTRING (HIMCC hCompStr; ImmLockIMCC로 잠금) ───────────────────────
typedef struct tagCOMPOSITIONSTRING {
    DWORD dwSize;
    DWORD dwCompReadAttrLen;
    DWORD dwCompReadAttrOffset;
    DWORD dwCompReadClauseLen;
    DWORD dwCompReadClauseOffset;
    DWORD dwCompReadStrLen;
    DWORD dwCompReadStrOffset;
    DWORD dwCompAttrLen;
    DWORD dwCompAttrOffset;
    DWORD dwCompClauseLen;
    DWORD dwCompClauseOffset;
    DWORD dwCompStrLen;
    DWORD dwCompStrOffset;
    DWORD dwCursorPos;
    DWORD dwDeltaStart;
    DWORD dwResultReadClauseLen;
    DWORD dwResultReadClauseOffset;
    DWORD dwResultReadStrLen;
    DWORD dwResultReadStrOffset;
    DWORD dwResultClauseLen;
    DWORD dwResultClauseOffset;
    DWORD dwResultStrLen;
    DWORD dwResultStrOffset;
    DWORD dwPrivateSize;
    DWORD dwPrivateOffset;
} COMPOSITIONSTRING, *PCOMPOSITIONSTRING, *NPCOMPOSITIONSTRING, *LPCOMPOSITIONSTRING;

// ── CANDIDATEINFO (HIMCC hCandInfo) ─────────────────────────────────────────────
typedef struct tagCANDIDATEINFO {
    DWORD dwSize;
    DWORD dwCount;
    DWORD dwOffset[32];
    DWORD dwPrivateSize;
    DWORD dwPrivateOffset;
} CANDIDATEINFO, *PCANDIDATEINFO, *NPCANDIDATEINFO, *LPCANDIDATEINFO;

// ── GUIDELINE (HIMCC hGuideLine) ────────────────────────────────────────────────
typedef struct tagGUIDELINE {
    DWORD dwSize;
    DWORD dwLevel;
    DWORD dwIndex;
    DWORD dwStrLen;
    DWORD dwStrOffset;
    DWORD dwPrivateSize;
    DWORD dwPrivateOffset;
} GUIDELINE, *PGUIDELINE, *NPGUIDELINE, *LPGUIDELINE;

// ── INPUTCONTEXT (HIMC; ImmLockIMC로 잠금) ──────────────────────────────────────
typedef struct tagINPUTCONTEXT {
    HWND            hWnd;
    BOOL            fOpen;
    POINT           ptStatusWndPos;
    POINT           ptSoftKbdPos;
    DWORD           fdwConversion;
    DWORD           fdwSentence;
    union {
        LOGFONTA    A;
        LOGFONTW    W;
    } lfFont;
    COMPOSITIONFORM cfCompForm;
    CANDIDATEFORM   cfCandForm[4];
    HIMCC           hCompStr;
    HIMCC           hCandInfo;
    HIMCC           hGuideLine;
    HIMCC           hPrivate;
    DWORD           dwNumMsgBuf;
    HIMCC           hMsgBuf;
    DWORD           fdwInit;
    DWORD           dwReserve[3];
} INPUTCONTEXT, *PINPUTCONTEXT, *NPINPUTCONTEXT, *LPINPUTCONTEXT;

// ── TRANSMSG / TRANSMSGLIST (ImeToAsciiEx가 채우는 메시지 버퍼) ──────────────────
typedef struct tagTRANSMSG {
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
} TRANSMSG, *PTRANSMSG, *NPTRANSMSG, *LPTRANSMSG;

typedef struct tagTRANSMSGLIST {
    UINT     uMsgCount;
    TRANSMSG TransMsg[1];
} TRANSMSGLIST, *PTRANSMSGLIST, *NPTRANSMSGLIST, *LPTRANSMSGLIST;

// ── IME측 함수 (imm32.dll이 export하나 imm.h엔 프로토타입 없음; libimm32.a에 존재) ──
LPINPUTCONTEXT WINAPI ImmLockIMC(HIMC);
BOOL           WINAPI ImmUnlockIMC(HIMC);
LPVOID         WINAPI ImmLockIMCC(HIMCC);
BOOL           WINAPI ImmUnlockIMCC(HIMCC);
DWORD          WINAPI ImmGetIMCCSize(HIMCC);
HIMCC          WINAPI ImmReSizeIMCC(HIMCC, DWORD);
HIMCC          WINAPI ImmCreateIMCC(DWORD);
HIMCC          WINAPI ImmDestroyIMCC(HIMCC);
BOOL           WINAPI ImmGenerateMessage(HIMC);

// ── 일부 상수(imm.h에 없을 수 있어 보강) ─────────────────────────────────────────
#ifndef IME_ESC_QUERY_SUPPORT
#define IME_ESC_QUERY_SUPPORT 0x0003
#endif
#ifndef GCS_COMPREADSTR
#define GCS_COMPREADSTR 0x0001
#endif
#ifndef IME_PROP_KBD_CHAR_FIRST
#define IME_PROP_KBD_CHAR_FIRST 0x00000002
#endif
#ifndef IME_PROP_AT_CARET
#define IME_PROP_AT_CARET 0x00010000
#endif
#ifndef IME_PROP_UNICODE
#define IME_PROP_UNICODE 0x00080000
#endif
#ifndef SCS_CAP_COMPSTR
#define SCS_CAP_COMPSTR 0x00000001
#endif
#ifndef CS_IME
#define CS_IME 0x00010000
#endif
#ifndef IMN_SETCONVERSIONMODE
#define IMN_SETCONVERSIONMODE 0x0006
#endif

#ifdef __cplusplus
}
#endif
