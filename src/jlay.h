#pragma once
#include "config.h"

// ── 구운 자판 파일 (.jmb, RFC-0016 P5b-2) ─────────────────────────────────────────
// 오너 결정 2026-09-23: 자판 자료도 컴파일을 거친다 — 확인·검증·이진화. 입력기는 구운 자판만
// 읽는다. 텍스트(.jmt)를 읽고 검사하는 일은 도구(관리 앱·CLI)가 한다(jlay_build.h).
//
// 머리부 64바이트, 리틀엔디안:
//    0 magic "JMTLAY\0\0"  8 formatVersion  12 kind(LayoutType)  16 bodyBytes
//   20 crc32(body)         24 fileSize      28 flags             32 srcSize
//   36/40 srcMtime(low/high)                44 builtBy           48.. reserved
// 본문은 이름·약칭 뒤에 종류별 고정폭 필드(자세한 것은 jlay_build.c 의 쓰기 순서와 짝).
//
// 산출물은 원본의 크기·수정시각을 담는다 — 관리 앱과 `--check` 가 "낡았다"를 이걸로 안다.
// 판 2 (v0.40.0): 순차 자판 본문에 앞단 조합이 붙었다 (RFC-0016 §6.3).
// 판 3 (v0.42.0): 순차 자판 본문에 후보 사전 이름과 변환 글쇠가 붙었다 (§6.4).
//   본문이 바뀌면 반드시 이 번호를 올린다 — 옛 산출물을 새 규칙으로 읽으면 조용히 어긋난다.
// 판 4: 조합 항목에 `holdOneshot` 이 붙었다 (Hold 에 건 원샷 ↔ momentary 구분, RFC-0016 §6.2).
// 본문이 바뀌면 판을 반드시 올린다 — 안 올리면 옛 구운 자판을 새 코드가 엉뚱하게 읽는다(0.40.0 교훈,
// 그리고 2026-09-24 실기에서 같은 일이 한 번 더 났다: 새 필드를 굽지 않아 원샷이 momentary 로 돌았다).
#define JLAY_FORMAT_VERSION 4

typedef enum JLayError {
    JLAY_OK = 0,
    JLAY_E_OPEN,      // 파일을 열지 못했다
    JLAY_E_MAGIC,     // 구운 자판 파일이 아니다
    JLAY_E_VERSION,   // 더 새 판 — 이 자모통이 못 읽는다
    JLAY_E_LAYOUT,    // 크기·오프셋이 맞지 않는다 (잘림·조작)
    JLAY_E_CRC,       // 내용이 구울 때와 다르다
    JLAY_E_KIND,      // 모르는 자판 종류
    JLAY_E_DICT,      // 순차 자판의 사전을 쓸 수 없다 (없거나 점검 실패)
    JLAY_E_MEMORY
} JLayError;

// 구운 자판을 읽어 LayoutConfig 를 채운다. 리소스(name·자판 구조체·사전)는 live config 소유이며
// Config_FreeLayoutResources 가 해제한다. 실패하면 false 이고 *err 에 이유.
//   내용 crc 와 순차 자판의 사전 전수 점검까지 여기서 한다 — 성한 자판만 목록에 오른다.
bool JLay_Load(const wchar_t *path, LayoutConfig *out, JLayError *err);
const wchar_t *JLay_ErrorText(JLayError e);

// 산출물이 낡았는가: 원본의 크기·수정시각이 머리부와 다르거나, **형식 판이 지금 쓰는 판과 다르다**.
//   산출물이 없어도 true. (형식이 바뀐 판올림에서 도구가 스스로 다시 굽게 하는 자리다.)
//   실제로 다시 굽는 일은 도구가 한다 — 입력기는 굽지 않는다.
bool JLay_IsStale(const wchar_t *jmbPath, const wchar_t *jmtPath);
