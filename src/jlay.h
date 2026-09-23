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
#define JLAY_FORMAT_VERSION 1

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

// 산출물이 원본보다 낡았는가 (원본의 크기·수정시각이 머리부와 다르다). 산출물이 없으면 true.
//   실제로 다시 굽는 일은 도구가 한다 — 입력기는 굽지 않는다.
bool JLay_IsStale(const wchar_t *jmbPath, const wchar_t *jmtPath);
