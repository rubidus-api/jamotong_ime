#pragma once
#include "klay.h"

// ── 자판 컴파일러 (원본 .jmt → 이진 .jmb, RFC-0016 P5b-2) ────────────────────────
// 도구(관리 앱·CLI)에만 들어간다. 입력기 DLL 은 구운 결과만 읽는다(jlay.h).
// 오류는 여기서 전부 난다(파일:줄:칸 + 코드 + 고치는 법) — 통과 못 하면 산출물을 남기지 않는다.

// 원본 하나를 굽는다. diag(NULL 허용)에 진단을 모은다.
bool JLay_Build(const wchar_t *srcPath, const wchar_t *outPath, KlayDiag *diag);
// 폴더 안의 `*.jmt` 가운데 산출물이 없거나 낡은 것을 굽는다 (설치 스크립트·관리 앱이 쓴다).
//   *built/*failed 에 개수(NULL 허용). 폴더가 없으면 false.
bool JLay_BuildDir(const wchar_t *dir, int *built, int *failed);
