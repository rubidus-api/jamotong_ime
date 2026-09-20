// ui_server.h — RFC-0015: 데스크톱 UI 헬퍼 (jamotong.exe --ui-server).
#ifndef JAMOTONG_UI_SERVER_H
#define JAMOTONG_UI_SERVER_H

#include <windows.h>

// 메시지 루프를 돌며 파이프로 오는 표시 요청을 그린다. 세션당 하나만 살아남는다
// (이미 돌고 있으면 0 을 돌려주고 즉시 끝난다).
int UiServer_Run(HINSTANCE hInst);

#endif
