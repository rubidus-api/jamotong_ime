#pragma once
#include <stdbool.h>
#include <wchar.h>

// ── MSKLC `.klc` → 정적 자판 `.jmt` (RFC-0011a) ───────────────────────────────────
// **도구 전용**. `.klc` 는 Microsoft Keyboard Layout Creator 가 쓰는 자판 원본이다: 스캔코드와
// 가상키마다 기본·Shift·AltGr 면에 어떤 글자가 나오는지 적은 표. 우리 정적 자판은 "US 자판이
// 낼 글자 → 이 자판이 낼 글자" 한 장이므로, 기본 면과 Shift 면만 옮긴다.
//   옮기지 않는 것(세어서 알려 준다): 데드키(`@`), 합자(`%%`), AltGr·Ctrl 면, 글쇠 없는 줄.
typedef struct KlcImportResult {
    int rows;        // 읽은 LAYOUT 줄
    int mapped;      // 옮긴 글쇠 (기본·Shift 각각 셈)
    int deadKeys;    // 데드키라 건너뛴 자리
    int ligatures;   // 합자라 건너뛴 자리
    int skipped;     // 그 밖에 옮길 수 없던 자리 (모르는 스캔코드 등)
    wchar_t name[64];     // 자판 이름 (KBD 줄의 설명, 없으면 파일 이름)
    wchar_t error[200];   // 실패 사유 (성공이면 빈 문자열)
} KlcImportResult;

bool KlcImport_Run(const wchar_t *srcPath, const wchar_t *outPath, KlcImportResult *res);
