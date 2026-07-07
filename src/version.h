#pragma once
// ── 버전 규약 (Semantic Versioning, lowent 모듈 버전 규약 준용) ────────────────────
//   MAJOR: 하위호환이 깨지는 변경(설정 파일 형식·자판 파일 형식·동작 계약의 비호환 변경)
//   MINOR: 하위호환 신규 기능(새 입력 기능·UI·데이터 확충)
//   PATCH: 하위호환 버그 수정만
//   0.x 단계: 공개 안정화 이전 — MINOR가 기능 묶음, PATCH가 수정 묶음.
//   갱신 절차: 아래 세 숫자만 수정 → 문자열/리소스 버전은 자동 파생. CHANGELOG.md에 항목 추가.
#define JAMOTONG_VERSION_MAJOR 0
#define JAMOTONG_VERSION_MINOR 11
#define JAMOTONG_VERSION_PATCH 0

// 문자열 파생 (수정 금지). L"" 와 일반 문자열의 인접 결합은 C11 §6.4.5(와이드로 승격) 표준.
#define JAMO_STR2(x) #x
#define JAMO_STR(x)  JAMO_STR2(x)
#define JAMOTONG_VERSION \
    L"" JAMO_STR(JAMOTONG_VERSION_MAJOR) "." JAMO_STR(JAMOTONG_VERSION_MINOR) "." JAMO_STR(JAMOTONG_VERSION_PATCH)

#define JAMOTONG_AUTHOR      L"rubidus-api"
#define JAMOTONG_HOMEPAGE    L"https://github.com/rubidus-api/jamotong_ime"
