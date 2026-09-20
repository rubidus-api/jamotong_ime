// ui_ipc.h — RFC-0015: UWP 호스트용 데스크톱 UI 헬퍼와 주고받는 메시지.
//
// TIP 은 호스트 프로세스 안에서 돈다. 그 호스트가 UWP 앱(AppContainer)이면 TIP 이 만든 창은
// 화면에 합성되지 않는다(매뉴얼 §14.7). 그래서 "무엇을 어디에 그릴지"만 데스크톱 세션의
// 헬퍼(jamotong.exe --ui-server)에 보내고, 키 처리와 문서 편집은 TIP 이 그대로 한다.
// 헬퍼는 **표시 전용**이다 — 역방향 채널이 없다(Phase 1).
#ifndef JAMOTONG_UI_IPC_H
#define JAMOTONG_UI_IPC_H

#include <windows.h>

#define JAMO_UIIPC_VERSION   1u
// 세션마다 따로 — 다른 사용자/세션과 섞이지 않는다(이름 뒤에 세션 ID 를 붙인다).
#define JAMO_UIIPC_PIPE_FMT  L"\\\\.\\pipe\\jamotong-ui-%lu"
#define JAMO_UIIPC_MUTEX_FMT L"Local\\JamotongUiHelper-%lu"

// 방어적 상한 — 신뢰 경계를 넘어온 값이라 파싱 직후 강제한다.
#define JAMO_UIIPC_MAX_CAND     64
#define JAMO_UIIPC_MAX_CANDLEN  32
#define JAMO_UIIPC_MAX_FACE     32

enum {
    JAMO_UIMSG_SHOW   = 1,   // 후보 목록을 새로 표시
    JAMO_UIMSG_UPDATE = 2,   // 선택/페이지만 갱신
    JAMO_UIMSG_HIDE   = 3    // 감춘다
};

// 고정 헤더. 뒤에 SHOW 일 때만 후보 문자열이 이어진다(각 JAMO_UIIPC_MAX_CANDLEN 길이 고정).
// 고정 폭이라 길이 계산 실수를 줄인다 — 후보 하나당 64바이트면 최대 4KB 로 한 파이프 메시지에 든다.
typedef struct {
    UINT32 version;      // JAMO_UIIPC_VERSION — 모르는 값이면 헬퍼는 조용히 무시한다
    UINT32 msg;          // JAMO_UIMSG_*
    UINT32 count;        // 후보 개수 (SHOW)
    UINT32 perPage;      // 한 페이지에 보일 개수
    UINT32 selection;    // 선택된 인덱스 (전체 기준)
    INT32  anchorX;      // 앵커(화면 좌표) — 캐럿 왼쪽 아래
    INT32  anchorY;
    INT32  caretTop;     // 캐럿 윗변(위로 뒤집을 때 기준)
    UINT32 fontSize;     // 후보 글꼴 크기(px). 0 = 헬퍼 기본값
    WCHAR  fontFace[JAMO_UIIPC_MAX_FACE];
} JamoUiMsgHeader;

#endif   // JAMOTONG_UI_IPC_H
