#pragma once
// 조합 미리보기 플로팅 오버레이 (RFC-0002).
//   커밋 전용 엔진(문서엔 확정 음절만 삽입)에서 조합 중 음절을 IME 소유의 반투명 '칩' 창으로
//   캐럿 위치에 표시한다. 문서를 건드리지 않으므로 CUAS 제약과 무관. 고전 IMM32 기본 조합창 패턴.
#include <windows.h>

// 캐럿 화면 rect(rc)와 조합 문자열(text), 글꼴 face/크기로 오버레이 표시/갱신.
//   fixedSize: 0=Auto(rc 높이 = 줄 높이 근사; PuTTY처럼 캐럿이 작은 앱에선 고정 권장), 8~96=고정 px.
//   입력 스레드에서 호출(창 lazy 생성).
void PreeditOverlay_Show(const RECT *rcCaret, const wchar_t *text, const wchar_t *fontFace, int fixedSize);
void PreeditOverlay_Hide(void);
void PreeditOverlay_Uninitialize(void);   // 창 파괴 + 글꼴 해제 (클래스 해제는 DllMain)
