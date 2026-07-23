/* key_policy.c — 순수 키 판단 (RFC-0009 §8.1)
 *
 * 규칙 순서가 중요하다. 위에서부터 걸리는 대로 결정한다.
 */
#include "key_policy.h"
#include "hangul2.h"

static bool IsBoundaryKey(const LabKeySnapshot *k) {
    switch (k->virtual_key) {
    case VK_SPACE: case VK_RETURN: case VK_TAB: case VK_ESCAPE:
    case VK_LEFT:  case VK_RIGHT:  case VK_UP:  case VK_DOWN:
    case VK_HOME:  case VK_END:    case VK_PRIOR: case VK_NEXT:
    case VK_DELETE:
        return true;
    default:
        return false;
    }
}

LabKeyAction Lab_ClassifyKey(const LabKeySnapshot *key,
                             bool keyboard_open,
                             bool composing,
                             bool keyboard_disabled) {
    if (!key) return LAB_KEY_PASS;

    /* 1. 한/영 전환은 disabled여도 받는다 — 그래야 다시 켤 수 있다.
          Shift+Space와 VK_HANGUL 둘 다(RFC-0009 §8.3, D4). */
    bool toggle = (key->virtual_key == VK_HANGUL) ||
                  (key->virtual_key == VK_SPACE && key->shift &&
                   !key->ctrl && !key->alt && !key->win);
    if (toggle) return LAB_KEY_TOGGLE;

    /* 2. keyboard가 disabled면 아무것도 먹지 않는다(암호 입력란 등). */
    if (keyboard_disabled) return LAB_KEY_PASS;

    /* 3. Ctrl/Alt/Win 조합은 앱 단축키다. 조합 중이면 확정만 하고 넘긴다.
          Shift는 된소리에 쓰이므로 여기 넣지 않는다(RFC-0009 §14.1 LAB-KEY-003). */
    if (key->ctrl || key->alt || key->win)
        return composing ? LAB_KEY_FINALIZE_AND_PASS : LAB_KEY_PASS;

    /* 4. 한글 모드가 아니면 전부 통과 — 영문은 앱 원래 입력 그대로(§4.1). */
    if (!keyboard_open) return LAB_KEY_PASS;

    /* 5. 경계키: 조합 중이면 확정하고 원래 키를 앱에 넘긴다.
          replay(SendInput/PostMessage)는 쓰지 않는다(§8.2, D7). */
    if (IsBoundaryKey(key))
        return composing ? LAB_KEY_FINALIZE_AND_PASS : LAB_KEY_PASS;

    /* 6. Backspace는 조합 중일 때만 우리 것. 아니면 앱이 지운다. */
    if (key->virtual_key == VK_BACK)
        return composing ? LAB_KEY_HANDLE_BACKSPACE : LAB_KEY_PASS;

    /* 7. 두벌식에 매핑되는 키만 먹는다. 나머지(숫자·기호·OEM)는 통과. */
    if (Hangul2_MapDubeolsik(key->virtual_key, key->shift).kind != JAMO_NONE)
        return LAB_KEY_HANDLE_HANGUL;

    /* 8. 나머지는 조합 중이면 확정 후 통과, 아니면 그냥 통과. */
    return composing ? LAB_KEY_FINALIZE_AND_PASS : LAB_KEY_PASS;
}
