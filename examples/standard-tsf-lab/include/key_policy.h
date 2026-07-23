/* key_policy.h — 순수 키 판단 (RFC-0009 §8.1)
 *
 * OnTestKeyDown과 OnKeyDown이 같은 snapshot과 같은 순수 함수를 쓴다.
 * 그래야 "테스트에선 먹는다고 해놓고 실제로는 안 먹는" 어긋남이 생기지 않는다.
 */
#ifndef LAB_KEY_POLICY_H
#define LAB_KEY_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#  include <windows.h>
#else
typedef unsigned int UINT;
typedef uintptr_t    WPARAM;
typedef intptr_t     LPARAM;
#  define VK_BACK    0x08
#  define VK_TAB     0x09
#  define VK_RETURN  0x0D
#  define VK_SPACE   0x20
#  define VK_PRIOR   0x21
#  define VK_NEXT    0x22
#  define VK_END     0x23
#  define VK_HOME    0x24
#  define VK_LEFT    0x25
#  define VK_UP      0x26
#  define VK_RIGHT   0x27
#  define VK_DOWN    0x28
#  define VK_DELETE  0x2E
#  define VK_ESCAPE  0x1B
#  define VK_HANGUL  0x15
#endif

typedef enum LabKeyAction {
    LAB_KEY_PASS = 0,            /* 앱이 원래 키를 받는다. eaten=FALSE */
    LAB_KEY_HANDLE_HANGUL,       /* 우리가 먹고 조합한다 */
    LAB_KEY_HANDLE_BACKSPACE,    /* 우리가 먹고 한 단계 지운다 */
    LAB_KEY_FINALIZE_AND_PASS,   /* 조합을 확정하고, 원래 키는 앱으로 (§8.2) */
    LAB_KEY_TOGGLE               /* 한/영 전환 */
} LabKeyAction;

typedef struct LabKeySnapshot {
    UINT virtual_key;
    UINT scan_code;
    bool extended;
    bool repeat;
    bool shift;
    bool ctrl;
    bool alt;
    bool win;
    uint32_t context_generation;
} LabKeySnapshot;

/* 순수 함수. 어떤 전역도 읽지 않는다 — 인자로 받은 것만 본다. */
LabKeyAction Lab_ClassifyKey(const LabKeySnapshot *key,
                             bool keyboard_open,
                             bool composing,
                             bool keyboard_disabled);

#endif /* LAB_KEY_POLICY_H */
