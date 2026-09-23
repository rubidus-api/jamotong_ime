#pragma once
#include <stdbool.h>
#include <wchar.h>

// ── 사전 파일 (RFC-0016 P5, 오너 결정 2026-09-23) ──────────────────────────────────
// 자판 파일(.jmt)은 규칙만 갖고, 변환 자료는 따로 구운 사전 파일(.jdb)이 갖는다.
// 사전은 **읽기 전용 이진**이다: 열 때 통째로 메모리 매핑하고, 정렬된 키를 이진 탐색으로 찾는다.
//   - 열기 비용이 0 에 가깝다 (파싱이 없다). 입력기 DLL 은 텍스트를 쓰는 모든 프로세스에 로드되므로
//     같은 사전이 물리 메모리 한 벌로 공유되는 것이 중요하다.
//   - 10만 항목이 비교 17번. 접두 최장 일치 하나만 하면 되므로 SQL 엔진은 필요 없다.
//   - 남이 준 파일일 수 있다 → 열 때 머리부와 색인의 범위를 전부 검사하고, 그 뒤로는 범위 밖을
//     읽지 않는다. 내용(키/값 바이트)은 훑지 않는다 — 깨져 있으면 결과가 틀릴 뿐 죽지 않는다.
//
// 파일 구조 (리틀엔디안, 머리부 64바이트):
//   magic "JMTDICT\0" | formatVersion | kind | count | offIndex | offKeys | offVals |
//   keyBytes | valBytes | maxKeyLen | maxValLen | flags | crc32 | fileSize | reserved
//   색인   count x 12바이트 {u32 keyOff, u32 valOff, u16 keyLen, u16 valLen} — 키 오름차순
//   키블록 UTF-8 바이트, 값블록 UTF-16LE (런타임이 변환하지 않는다)
//   이름/라이선스 문자열은 값블록 뒤 꼬리에 UTF-16LE 로 붙는다.
#define JDICT_FORMAT_VERSION 1
#define JDICT_KIND_SEQUENCE  1
#define JDICT_MAX_KEY        32    // 친 글자열 (ASCII)
#define JDICT_MAX_VALUE      64    // 낼 글자 (UTF-16 부호단위)

typedef enum JDictError {
    JDICT_OK = 0,
    JDICT_E_OPEN,       // 파일을 열지 못했다 (없거나 권한)
    JDICT_E_MAGIC,      // 사전 파일이 아니다
    JDICT_E_VERSION,    // 더 새 판 — 이 자모통이 못 읽는다
    JDICT_E_LAYOUT,     // 머리부/색인이 파일 크기와 맞지 않는다 (잘림·조작)
    JDICT_E_CRC,        // 내용이 굽던 때와 다르다 (전수 점검에서만)
    JDICT_E_ORDER,      // 키가 오름차순이 아니다 (전수 점검에서만)
    JDICT_E_KEY,        // 이 종류가 쓸 수 없는 키 글자 (전수 점검에서만)
    JDICT_E_MEMORY
} JDictError;

// 값은 파일에 UTF-16LE 로 들어 있다. Windows 의 wchar_t 와 같은 폭이지만, 폭이 다른 곳(네이티브
// 시험)에서도 같은 코드가 돌도록 16비트 단위로 명시한다. 값을 쓸 때는 JDict_CopyValue 로 옮긴다.
typedef unsigned short jdchar;

typedef struct JDict JDict;

// 사전을 열어 매핑한다. 실패하면 NULL 이고 *err 에 이유. path 는 이미 해석된 전체 경로.
JDict *JDict_Open(const wchar_t *path, JDictError *err);
void   JDict_Close(JDict *d);

int            JDict_Count(const JDict *d);
int            JDict_Kind(const JDict *d);
int            JDict_MaxKeyLen(const JDict *d);
const wchar_t *JDict_Name(const JDict *d);
const wchar_t *JDict_License(const JDict *d);
const wchar_t *JDict_ErrorText(JDictError e);

// 전수 점검 (오너 지시 2026-09-23: 자판을 고를 때 사전 데이터까지 보고 성한 것만 쓴다).
//   머리부 뒤 전체의 crc32, 키 오름차순, 키 글자 범위를 확인한다. 파일 전체를 읽으므로 자판을
//   목록에 올리거나 켤 때 한 번만 부른다 — 치는 동안에는 부르지 않는다.
bool JDict_Verify(const JDict *d, JDictError *err);

// 정확히 같은 키가 있는가. val/valLen 은 NULL 을 넘겨도 된다. val 은 매핑 안을 가리키며
// (NUL 종단이 아니다) 사전이 닫힐 때까지 유효하다.
bool JDict_Exact(const JDict *d, const wchar_t *key, const jdchar **val, int *valLen);
// 찾은 값을 wchar_t 버퍼로 옮긴다 (NUL 종단). 자리가 모자라면 -1, 아니면 옮긴 부호단위 수.
int  JDict_CopyValue(const jdchar *val, int valLen, wchar_t *out, int cap);
// key 로 시작하면서 key 보다 긴 항목이 있는가 (엔진의 "더 기다릴까" 판정).
bool JDict_HasLonger(const JDict *d, const wchar_t *key);
// buf 의 앞부분과 맞는 가장 긴 항목. 찾으면 *keyLen 에 그 길이, val/valLen 에 값.
bool JDict_LongestPrefix(const JDict *d, const wchar_t *buf, int *keyLen,
                         const jdchar **val, int *valLen);
