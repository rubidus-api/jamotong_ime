#pragma once
// 유니코드 코드포인트 직접 입력 팝업 (TODO #5).
//   Ctrl+Alt+U → 팝업에 16진수 입력(실시간 미리보기) → Enter로 해당 문자 삽입, Esc 취소.
//   문서를 읽거나 교체하지 않고(팝업이 피드백 담당) '삽입'만 쓰므로 모든 앱(CUAS 포함)에서 동작.
#include <windows.h>
#include <stdbool.h>

void CodeInput_Show(int x, int y);          // 팝업 열기 (입력 스레드, lazy 생성)
bool CodeInput_IsVisible(void);
// 키 처리. 소비했으면 true. Enter 확정 시 *outCodepoint에 코드포인트(≥0x20)를 담는다(그 외 0).
bool CodeInput_HandleKey(UINT vKey, bool shift, unsigned *outCodepoint);
void CodeInput_Hide(void);
void CodeInput_Uninitialize(void);
