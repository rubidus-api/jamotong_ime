/* key_policy_test.c — LAB-KEY-001~003 (RFC-0009 §14.1) */
#include "key_policy.h"
#include <stdio.h>
#include <string.h>

static int g_pass=0, g_fail=0;
static const char *g_case="";
static const char *NAME[] = {"PASS","HANGUL","BACKSPACE","FINALIZE_AND_PASS","TOGGLE"};

static LabKeySnapshot K(UINT vk) { LabKeySnapshot k; memset(&k,0,sizeof k); k.virtual_key=vk; return k; }

static void want(LabKeySnapshot k, bool open, bool composing, bool disabled,
                 LabKeyAction expect, const char *what) {
    LabKeyAction got = Lab_ClassifyKey(&k, open, composing, disabled);
    if (got==expect) g_pass++;
    else { g_fail++; printf("  FAIL [%s] %s — got %s want %s\n", g_case, what, NAME[got], NAME[expect]); }
}

/* LAB-KEY-001: English/disabled/empty context에서 일반 키 PASS */
static void key001(void) {
    g_case="LAB-KEY-001";
    want(K('R'), false,false,false, LAB_KEY_PASS, "영문 모드에서 R");
    want(K('K'), false,false,false, LAB_KEY_PASS, "영문 모드에서 K");
    want(K(VK_SPACE), false,false,false, LAB_KEY_PASS, "영문 모드 Space");
    want(K(VK_BACK),  false,false,false, LAB_KEY_PASS, "영문 모드 Backspace");
    want(K('R'), true,false,true,  LAB_KEY_PASS, "disabled에서 R");
    want(K('R'), true,true, true,  LAB_KEY_PASS, "disabled면 조합 중이어도 PASS");
    /* 한글 모드지만 매핑 없는 키 */
    want(K('1'), true,false,false, LAB_KEY_PASS, "숫자 1은 통과");
    want(K(0xBA), true,false,false, LAB_KEY_PASS, "OEM 키는 통과");
}

/* LAB-KEY-002: composing boundary는 FINALIZE_AND_PASS */
static void key002(void) {
    g_case="LAB-KEY-002";
    UINT b[] = {VK_SPACE,VK_RETURN,VK_TAB,VK_ESCAPE,VK_LEFT,VK_RIGHT,VK_UP,VK_DOWN,
                VK_HOME,VK_END,VK_PRIOR,VK_NEXT,VK_DELETE};
    for (unsigned i=0;i<sizeof(b)/sizeof(b[0]);i++) {
        char m[48]; snprintf(m,sizeof m,"조합 중 경계키 0x%02X", b[i]);
        want(K(b[i]), true,true,false, LAB_KEY_FINALIZE_AND_PASS, m);
        snprintf(m,sizeof m,"비조합 경계키 0x%02X", b[i]);
        want(K(b[i]), true,false,false, LAB_KEY_PASS, m);
    }
    want(K(VK_BACK), true,true, false, LAB_KEY_HANDLE_BACKSPACE, "조합 중 Backspace는 우리 것");
    want(K(VK_BACK), true,false,false, LAB_KEY_PASS, "비조합 Backspace는 앱 것");
    /* 매핑 안 되는 키도 조합 중이면 확정 후 통과 */
    want(K('1'), true,true,false, LAB_KEY_FINALIZE_AND_PASS, "조합 중 숫자");
}

/* LAB-KEY-003: modifier shortcut 분류 */
static void key003(void) {
    g_case="LAB-KEY-003";
    LabKeySnapshot k;
    k=K('C'); k.ctrl=true;
    want(k, true,false,false, LAB_KEY_PASS, "Ctrl+C 비조합");
    want(k, true,true, false, LAB_KEY_FINALIZE_AND_PASS, "Ctrl+C 조합 중");
    k=K('S'); k.ctrl=true;
    want(k, true,true, false, LAB_KEY_FINALIZE_AND_PASS, "Ctrl+S 조합 중");
    k=K(VK_TAB); k.alt=true;
    want(k, true,true, false, LAB_KEY_FINALIZE_AND_PASS, "Alt+Tab 조합 중");
    k=K('D'); k.win=true;
    want(k, true,false,false, LAB_KEY_PASS, "Win+D");
    /* ★ Shift는 modifier지만 된소리에 쓰이므로 단축키가 아니다 */
    k=K('R'); k.shift=true;
    want(k, true,false,false, LAB_KEY_HANDLE_HANGUL, "Shift+R은 ㄲ (단축키 아님)");
    k=K('O'); k.shift=true;
    want(k, true,false,false, LAB_KEY_HANDLE_HANGUL, "Shift+O는 ㅒ");
    /* 한/영 전환 */
    want(K(VK_HANGUL), true,false,false, LAB_KEY_TOGGLE, "한/영 키");
    want(K(VK_HANGUL), false,false,true, LAB_KEY_TOGGLE, "disabled여도 한/영 키는 받는다");
    k=K(VK_SPACE); k.shift=true;
    want(k, true,false,false, LAB_KEY_TOGGLE, "Shift+Space 전환");
    k=K(VK_SPACE); k.shift=true; k.ctrl=true;
    want(k, true,false,false, LAB_KEY_PASS, "Ctrl+Shift+Space는 전환 아님");
}

int main(void){
    key001(); key002(); key003();
    printf("\nkey_policy: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail?1:0;
}
