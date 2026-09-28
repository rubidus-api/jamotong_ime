#pragma once
// install_acl.h — 설치 폴더가 안전한가 (순수 로직, WinAPI 무의존)
//   입력기 DLL 은 관리자 앱을 포함한 모든 프로세스에 실린다. 설치 폴더를 사용자가 고를 수 있으므로
//   (RFC-0019 §7), 관리자 아닌 누가 그 폴더나 그 위 폴더를 바꿔치기할 수 있으면 등록을 거절한다.
//   이 파일은 한 폴더의 주인과 DACL 을 받아 판정만 한다 — 읽기와 잠그기는 setup_cmd.c 가 한다.
// 네이티브 테스트: jamotong-private/test/install_acl_test.c (T069)
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

#define INSTALL_ACE_INHERIT_ONLY 0x08u          // INHERIT_ONLY_ACE — 그 폴더 자신에게는 권한이 없다
#define INSTALL_ACL_NULL_DACL    ((size_t)-1)   // count 자리에: DACL 이 NULL = 모두에게 모든 권한

typedef struct InstallAce {
    const wchar_t *sid;     // 문자열 SID ("S-1-5-32-544")
    unsigned       mask;    // 접근 마스크
    unsigned       flags;   // ACE 플래그 (INSTALL_ACE_INHERIT_ONLY 만 본다)
    bool           allow;   // 허용 ACE 인가 (거부 ACE 는 판정에 쓰지 않는다)
} InstallAce;

typedef enum InstallAclRole {
    INSTALL_ACL_ANCESTOR = 0,   // 위 폴더: 경로의 한 칸을 바꿀 수 있는 권한만 문제다
    INSTALL_ACL_TARGET = 1,     // 설치 폴더 자신: 무엇이든 쓸 수 있으면 문제다
    INSTALL_ACL_ROOT = 2        // 드라이브 루트: 이름을 바꿀 수 없으니 DELETE 는 문제가 아니다
} InstallAclRole;

typedef enum InstallAclVerdict {
    INSTALL_ACL_SAFE = 0,
    INSTALL_ACL_UNSAFE_OWNER = 1,   // 주인이 관리자가 아니다 (주인은 늘 권한을 고칠 수 있다)
    INSTALL_ACL_UNSAFE_ACE = 2      // 관리자 아닌 누가 위험한 권한을 가진다
} InstallAclVerdict;

// SYSTEM, Administrators, TrustedInstaller 인가.
bool InstallAcl_IsTrusted(const wchar_t *sid);

// owner: 주인 SID (NULL = 모름 → 위험). aces/count: DACL (count == INSTALL_ACL_NULL_DACL 이면 NULL DACL).
InstallAclVerdict InstallAcl_Check(const wchar_t *owner, const InstallAce *aces, size_t count, InstallAclRole role);

// 위험하다고 본 첫 ACE 의 자리 (로그용). 안전하거나 주인 문제면 -1.
long InstallAcl_FirstUnsafe(const InstallAce *aces, size_t count, InstallAclRole role);
