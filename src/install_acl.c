// install_acl.c — 설치 폴더 안전성 판정 (설명은 install_acl.h)
#include "install_acl.h"
#include <wctype.h>

// 위 폴더: 지우기·이름 바꾸기(DELETE, FILE_DELETE_CHILD)와 권한·주인 바꾸기, 그리고 그것을 다 담는 것들.
//   파일·폴더를 '더하는' 권한은 경로의 기존 칸을 바꾸지 못하므로 넣지 않는다 — 드라이브 루트가 그렇다.
#define ANCESTOR_BAD (0x00000040u /* FILE_DELETE_CHILD */ | 0x00010000u /* DELETE */ | \
                      0x00040000u /* WRITE_DAC */ | 0x00080000u /* WRITE_OWNER */ | \
                      0x02000000u /* MAXIMUM_ALLOWED */ | 0x10000000u /* GENERIC_ALL */)
// 드라이브 루트: 위와 같되 DELETE 는 뺀다 — 루트는 지우거나 이름을 바꿀 수 없다. 많은 데이터 드라이브가
//   인증된 사용자에게 루트 자신의 '수정'(DELETE 포함)을 준다(2026-09-28 VM 의 D:\).
#define ROOT_BAD     (ANCESTOR_BAD & ~0x00010000u)
// 설치 폴더 자신: 위의 것에 더해 무엇이든 쓰는 권한 — 파일 하나만 놓을 수 있어도 곁에 DLL 을 둔다.
#define TARGET_BAD   (ANCESTOR_BAD | 0x00000002u /* FILE_ADD_FILE */ | 0x00000004u /* FILE_ADD_SUBDIRECTORY */ | \
                      0x00000010u /* FILE_WRITE_EA */ | 0x00000100u /* FILE_WRITE_ATTRIBUTES */ | \
                      0x40000000u /* GENERIC_WRITE */)

static bool SidEq(const wchar_t *a, const wchar_t *b) {
    for (; *a && *b; a++, b++)
        if (towupper((wint_t)*a) != towupper((wint_t)*b)) return false;
    return *a == *b;
}

bool InstallAcl_IsTrusted(const wchar_t *sid) {
    static const wchar_t *const trusted[] = {
        L"S-1-5-18",        // SYSTEM
        L"S-1-5-32-544",    // BUILTIN\Administrators
        L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464",   // NT SERVICE\TrustedInstaller
    };
    if (!sid) return false;
    for (size_t i = 0; i < sizeof trusted / sizeof trusted[0]; i++)
        if (SidEq(sid, trusted[i])) return true;
    return false;
}

long InstallAcl_FirstUnsafe(const InstallAce *aces, size_t count, InstallAclRole role) {
    if (count == INSTALL_ACL_NULL_DACL) return 0;
    const unsigned bad = role == INSTALL_ACL_TARGET ? TARGET_BAD : role == INSTALL_ACL_ROOT ? ROOT_BAD : ANCESTOR_BAD;
    for (size_t i = 0; i < count; i++) {
        const InstallAce *a = &aces[i];
        if (!a->allow) continue;                               // 거부는 판정에 쓰지 않는다(보수적으로)
        if (a->flags & INSTALL_ACE_INHERIT_ONLY) continue;     // 자식에게만 가는 권한
        if (a->sid && SidEq(a->sid, L"S-1-3-0")) continue;     // CREATOR OWNER — 자리 표시
        if (InstallAcl_IsTrusted(a->sid)) continue;
        if (a->mask & bad) return (long)i;
    }
    return -1;
}

InstallAclVerdict InstallAcl_Check(const wchar_t *owner, const InstallAce *aces, size_t count, InstallAclRole role) {
    if (!InstallAcl_IsTrusted(owner)) return INSTALL_ACL_UNSAFE_OWNER;
    return InstallAcl_FirstUnsafe(aces, count, role) >= 0 ? INSTALL_ACL_UNSAFE_ACE : INSTALL_ACL_SAFE;
}
