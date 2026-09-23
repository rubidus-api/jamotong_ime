#pragma once
#include <stdbool.h>
#include <wchar.h>

// ── 남의 사전 자료 → 우리 사전 원본(.jdt) (RFC-0016 §6.4) ─────────────────────────
// **도구 전용**. 자료는 사용자가 고르고 받는다 — 이 변환기는 형식만 바꾼다. 자모통은 어떤 자료도
// 함께 배포하지 않으므로, 쓰는 사람이 그 자료의 라이선스를 지킨다(바꾼 파일 머리부에 적어 둔다).
//
// 받아들이는 꼴 (탭으로 나눈 줄, UTF-8):
//   읽기 <탭> 좌id <탭> 우id <탭> 비용 <탭> 표기     ← mozc dictionary_oss 등, 비용이 낮을수록 앞 후보
//   읽기 <탭> 표기                                   ← 두 칸짜리, 적힌 차례가 후보 차례
// `#` 로 시작하는 줄과 빈 줄은 건너뛴다.
typedef struct DictImportResult {
    int rows;      // 읽은 자료 줄
    int kept;      // 옮긴 항목
    int skipped;   // 쓸 수 없어 버린 줄 (너무 길거나 빈 값)
    wchar_t error[200];   // 실패 사유 (성공이면 빈 문자열)
} DictImportResult;

// 사전에 새길 이름과 라이선스 (둘 다 NULL 허용). 구운 `.jdb` 가 이 두 줄을 싣고 다니므로, 남에게
// 넘어간 사전도 제 출처를 말할 수 있다. 줄바꿈·탭은 자리를 망가뜨리므로 사이띄개로 바꾼다.
typedef struct DictImportMeta {
    const wchar_t *name;      // NULL = "imported"
    const wchar_t *license;   // NULL = License 줄을 쓰지 않는다
} DictImportMeta;

// srcPath 를 읽어 outPath 에 `.jdt` 를 쓴다. limit > 0 이면 **비용이 낮은 것부터** 그만큼만 남긴다
// (0 = 전부, 사전 한도까지). meta 는 NULL 허용. 실패하면 false 이고 res->error 에 사유.
bool DictImport_Run(const wchar_t *srcPath, const wchar_t *outPath, int limit,
                    const DictImportMeta *meta, DictImportResult *res);
