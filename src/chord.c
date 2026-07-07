#include "chord.h"
#include "fsm.h"      // ComposeHangul
#include "layout.h"   // Layout_*ToCompatJamo
#include <string.h>

void Chord_Init(ChordContext *c) {
    c->cho = c->jung = c->jong = -1;
    c->activeKeys = 0;
    memset(c->keyDown, 0, sizeof(c->keyDown));
}

static wchar_t ComposeChord(const ChordContext *c) {
    if (c->cho >= 0 && c->jung >= 0) return ComposeHangul(c->cho, c->jung, c->jong);
    if (c->cho >= 0) return Layout_ChoToCompatJamo(c->cho);
    if (c->jung >= 0) return Layout_JungToCompatJamo(c->jung);
    if (c->jong >= 0) return Layout_JongToCompatJamo(c->jong);
    return 0;
}

static void AddJamo(ChordContext *c, const HangulLayout *hl, LayoutResult lr) {
    if (lr.type == JAMO_CHO) {
        if (c->cho < 0) c->cho = lr.index;
        else { int x = HangulLayout_Combine(hl, JAMO_CHO, c->cho, lr.index); c->cho = (x != -1) ? x : lr.index; }
    } else if (lr.type == JAMO_JUNG) {
        if (c->jung < 0) c->jung = lr.index;
        else { int x = HangulLayout_Combine(hl, JAMO_JUNG, c->jung, lr.index); c->jung = (x != -1) ? x : lr.index; }
    } else if (lr.type == JAMO_JONG) {
        if (c->jong < 0) c->jong = lr.index;
        else { int x = HangulLayout_Combine(hl, JAMO_JONG, c->jong, lr.index); c->jong = (x != -1) ? x : lr.index; }
    }
}

ChordResult Chord_KeyDown(ChordContext *c, const HangulLayout *hl, UINT vk, wchar_t keyChar) {
    ChordResult r = {0, 0, false};
    if (!hl || keyChar == 0 || keyChar >= 128) return r;
    LayoutResult lr = hl->keymap[(int)keyChar];
    if (lr.type == JAMO_NONE) return r;   // 자모 아님 → 통과

    r.eaten = true;
    if (vk < 256 && c->keyDown[vk]) {     // 키 반복 → 변화 없이 현재 조합 반환
        r.composing = ComposeChord(c);
        return r;
    }
    if (vk < 256) { c->keyDown[vk] = true; c->activeKeys++; }
    AddJamo(c, hl, lr);
    r.composing = ComposeChord(c);
    return r;
}

ChordResult Chord_KeyUp(ChordContext *c, UINT vk) {
    ChordResult r = {0, 0, false};
    if (vk < 256 && c->keyDown[vk]) {
        c->keyDown[vk] = false;
        if (c->activeKeys > 0) c->activeKeys--;
        r.eaten = true;
        if (c->activeKeys == 0) {          // 모두 떨어짐 → 음절 확정
            r.commit = ComposeChord(c);
            Chord_Init(c);
        }
    }
    return r;
}
