#include "preserved_map.h"

// config.h 의 SMOD_* 와 같은 값 (순수 모듈이라 복제하지 않고 여기서 정의를 공유하려면
// config.h 가 windows.h 를 끌므로, 값만 맞춘다 — config_test 가 어긋나면 잡는다)
#define PM_SMOD_SHIFT 0x01u
#define PM_SMOD_CTRL  0x02u
#define PM_SMOD_ALT   0x04u
#define PM_SMOD_GUI   0x08u

bool PreservedMap_IsBareModifier(unsigned vKey) {
    if (vKey >= 16 && vKey <= 18) return true;    // VK_SHIFT/VK_CONTROL/VK_MENU
    if (vKey == 91 || vKey == 92) return true;    // VK_LWIN/VK_RWIN
    if (vKey >= 160 && vKey <= 165) return true;  // VK_LSHIFT..VK_RMENU
    return false;
}

bool PreservedMap_Build(unsigned vKey, unsigned smods, unsigned *uVKey, unsigned *uModifiers) {
    if (!uVKey || !uModifiers) return false;
    if (smods & PM_SMOD_GUI) return false;            // TSF 에 Win 모디파이어 없음 → sink 폴백
    if (PreservedMap_IsBareModifier(vKey)) return false;  // "오른쪽 Alt만" 류 → sink 폴백
    unsigned m = 0;
    if (smods & PM_SMOD_SHIFT) m |= JAMO_TFMOD_SHIFT;
    if (smods & PM_SMOD_CTRL)  m |= JAMO_TFMOD_CONTROL;
    if (smods & PM_SMOD_ALT)   m |= JAMO_TFMOD_ALT;
    *uVKey = vKey;
    *uModifiers = m;
    return true;
}
