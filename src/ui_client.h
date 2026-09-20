// ui_client.h — RFC-0015: TIP 쪽 UI 헬퍼 클라이언트 (표시 요청만, 역방향 없음).
#ifndef JAMOTONG_UI_CLIENT_H
#define JAMOTONG_UI_CLIENT_H

#include <windows.h>
#include <stdbool.h>

// 헬퍼에 닿는가 (연결만 확인 — 없으면 호출자가 폴백한다).
bool UiClient_Available(void);

// 후보 목록을 그리라고 보낸다. lines[i] 는 화면에 그대로 그릴 완성된 줄
// (번호·글자·훈음·U+ 까지 TIP 이 만들어 보낸다 — 헬퍼는 사전을 모른다).
bool UiClient_Show(const wchar_t *const *lines, int count, int perPage, int selection,
                   int anchorX, int anchorY, int caretTop,
                   const wchar_t *fontFace, int fontSize);

// 선택/페이지만 바뀌었을 때.
bool UiClient_Update(int selection, int perPage);

// 감춘다.
void UiClient_Hide(void);

// 연결 상태를 버린다(테스트·재활성화용).
void UiClient_Reset(void);

#endif
