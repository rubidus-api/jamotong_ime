// chord_layout.c — 조합 자판 **런타임**: 눌린 글쇠로 조합을 판정하고 동작을 낸다.
//   파일을 읽어 표를 만드는 일은 도구 쪽(chord_parse.c)이다 — 입력기는 구운 자판만 읽는다.
#include "chord_layout.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

void ChordLayout_Free(ChordLayout *cl) { if (cl) HeapFree(GetProcessHeap(), 0, cl); }

#define CHORD_HOLD_MS 200   // tap/hold 판정 임계 (ms)

// 시계 (RFC-0016 §7.1: 시험은 가짜 시계로 판정을 재현한다)
static unsigned long DefaultNow(void) { return (unsigned long)GetTickCount(); }
static unsigned long (*g_now)(void) = DefaultNow;
void ChordKb_SetClock(unsigned long (*now)(void)) { g_now = now ? now : DefaultNow; }

void ChordKb_Init(ChordKbContext *c) {
    memset(c, 0, sizeof(*c));
    c->momentaryLayer = -1;
    c->oneshotLayer = -1;
    c->dragBtn = -1;
    c->macroIdx = -1;
}

static int EffLayer(const ChordKbContext *c) {
    if (c->momentaryLayer >= 0) return c->momentaryLayer;
    if (c->oneshotLayer >= 0) return c->oneshotLayer;
    return c->curLayer;
}
static const ChordEntry *FindEntry(const ChordLayout *cl, unsigned mask, int layer, int isHold) {
    for (int i = 0; cl && i < cl->chordCount; i++)
        if (cl->chords[i].mask == mask && cl->chords[i].layer == layer && cl->chords[i].isHold == isHold)
            return &cl->chords[i];
    return NULL;
}
// 지속형 hold (임시 레이어/모디파이어 = 누르고 있는 동안 유지). 나머지는 discrete(길게-누름) hold.
static bool IsSustained(ChordActionType a) {
    return a == CA_LAYER_ONESHOT || a == CA_MOD_ONESHOT || a == CA_PTR_MOVE || a == CA_PTR_WHEEL;
}

// ── SendInput 실행 (모든 합성 입력에 JAMO_SYNTH_MARK 표식) ─────────────────────────
static void SendMods(int mod, bool down) {
    static const struct { int bit; WORD vk; bool ext; } M[8] = {
        { CMOD_LCTRL, VK_LCONTROL, false }, { CMOD_RCTRL, VK_RCONTROL, true },
        { CMOD_LALT, VK_LMENU, false },     { CMOD_RALT, VK_RMENU, true },
        { CMOD_LSHIFT, VK_LSHIFT, false },  { CMOD_RSHIFT, VK_RSHIFT, false },
        { CMOD_LGUI, VK_LWIN, true },       { CMOD_RGUI, VK_RWIN, true },
    };
    for (int i = 0; i < 8; i++) {
        if (!(mod & M[i].bit)) continue;
        INPUT in; memset(&in, 0, sizeof(in));
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = M[i].vk;
        in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (M[i].ext ? KEYEVENTF_EXTENDEDKEY : 0);
        in.ki.dwExtraInfo = JAMO_SYNTH_MARK;
        SendInput(1, &in, sizeof(INPUT));
    }
}
static void SendVKey(int vk, int mod, bool ext) {
    if (!vk) return;
    if (mod) SendMods(mod, true);
    INPUT in[2]; memset(in, 0, sizeof(in));
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = (WORD)vk;
    in[0].ki.dwFlags = ext ? KEYEVENTF_EXTENDEDKEY : 0;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP | (ext ? KEYEVENTF_EXTENDEDKEY : 0);
    in[0].ki.dwExtraInfo = in[1].ki.dwExtraInfo = JAMO_SYNTH_MARK;
    SendInput(2, in, sizeof(INPUT));
    if (mod) SendMods(mod, false);
}
static void SendText(ChordKbContext *c, const wchar_t *s, int mod) {
    // 수식키를 함께 눌러야 하는 경우(1·2판의 with-mods)는 예전 길 그대로 — 문서 삽입에는
    // 수식키라는 것이 없다. 그 외에는 입력기가 물려 준 문서 편집 경로로 보낸다 (B11 잔여).
    if (!mod && c && c->textSink) { c->textSink(c->textCtx, s); return; }
    if (mod) SendMods(mod, true);
    for (int i = 0; s[i]; i++) {
        INPUT in[2]; memset(in, 0, sizeof(in));
        in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wScan = in[1].ki.wScan = s[i];
        in[0].ki.dwFlags = KEYEVENTF_UNICODE;
        in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        in[0].ki.dwExtraInfo = in[1].ki.dwExtraInfo = JAMO_SYNTH_MARK;
        SendInput(2, in, sizeof(INPUT));
    }
    if (mod) SendMods(mod, false);
}
static void SendMouseMove(int dx, int dy) {
    INPUT in; memset(&in, 0, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dx = dx; in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    in.mi.dwExtraInfo = JAMO_SYNTH_MARK;
    SendInput(1, &in, sizeof(INPUT));
}
static void SendMouseBtn(int btn, int action, int mod) {
    DWORD dn = (btn==1)?MOUSEEVENTF_RIGHTDOWN : (btn==2)?MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_LEFTDOWN;
    DWORD up = (btn==1)?MOUSEEVENTF_RIGHTUP   : (btn==2)?MOUSEEVENTF_MIDDLEUP   : MOUSEEVENTF_LEFTUP;
    if (mod) SendMods(mod, true);
    INPUT in; memset(&in, 0, sizeof(in)); in.type = INPUT_MOUSE; in.mi.dwExtraInfo = JAMO_SYNTH_MARK;
    if (action != 2) { in.mi.dwFlags = dn; SendInput(1, &in, sizeof(INPUT)); }   // down (click/down)
    if (action != 1) { in.mi.dwFlags = up; SendInput(1, &in, sizeof(INPUT)); }   // up (click/up)
    if (mod) SendMods(mod, false);
}
static void SendMouseWheel(int amt) {
    INPUT in; memset(&in, 0, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.mouseData = (DWORD)amt;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.dwExtraInfo = JAMO_SYNTH_MARK;
    SendInput(1, &in, sizeof(INPUT));
}

// ── 3판 연속 포인터 (§6.5) ─────────────────────────────────────────────────────────────────
//   프로필: {가속, 상한}. 이동은 px/s^2·px/s, 휠은 노치/s^2·노치/s.
static const struct { int accel, maxv; } kPtrProf[4] = {
    { 600, 250 },    // slow
    { 2200, 900 },   // normal
    { 4000, 1900 },  // fast
    { 24, 12 },      // scroll (노치)
};
#define PTR_TICK_MS 15

static void PtrSendBtn(int btn, bool down) {
    DWORD dn = (btn==1)?MOUSEEVENTF_RIGHTDOWN : (btn==2)?MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_LEFTDOWN;
    DWORD up = (btn==1)?MOUSEEVENTF_RIGHTUP   : (btn==2)?MOUSEEVENTF_MIDDLEUP   : MOUSEEVENTF_LEFTUP;
    INPUT in; memset(&in, 0, sizeof(in));
    in.type = INPUT_MOUSE; in.mi.dwExtraInfo = JAMO_SYNTH_MARK; in.mi.dwFlags = down ? dn : up;
    SendInput(1, &in, sizeof(INPUT));
}
static void PtrReleaseDrag(ChordKbContext *c) {   // 소유한 드래그만 놓는다 (물리 버튼은 건드리지 않는다)
    if (c->dragBtn < 0) return;
    PtrSendBtn(c->dragBtn, false);
    c->dragBtn = -1;
}
static void PtrStopContinuous(ChordKbContext *c) {
    memset(c->ptrKeyX, 0, sizeof c->ptrKeyX); memset(c->ptrKeyY, 0, sizeof c->ptrKeyY);
    memset(c->whKeyX, 0, sizeof c->whKeyX);   memset(c->whKeyY, 0, sizeof c->whKeyY);
    c->ptrX = c->ptrY = c->whX = c->whY = 0;
    c->ptrRemX = c->ptrRemY = c->whRemX = c->whRemY = 0;
}
// 활성 합과 프로필을 글쇠별 몫에서 다시 계산한다 (키를 떼면 그 몫만 빠진다)
static void PtrRecalc(ChordKbContext *c) {
    int px = 0, py = 0, wx = 0, wy = 0, pp = -1, wp = -1;
    for (int k = 0; k < 256; k++) {
        if (c->ptrKeyX[k] || c->ptrKeyY[k]) {
            px += c->ptrKeyX[k]; py += c->ptrKeyY[k];
            if ((int)c->ptrKeyProf[k] > pp) pp = (int)c->ptrKeyProf[k];
        }
        if (c->whKeyX[k] || c->whKeyY[k]) {
            wx += c->whKeyX[k]; wy += c->whKeyY[k];
            if ((int)c->whKeyProf[k] > wp) wp = (int)c->whKeyProf[k];
        }
    }
    bool wasMoving = (c->ptrX || c->ptrY), wasWheel = (c->whX || c->whY);
    c->ptrX = px; c->ptrY = py; c->whX = wx; c->whY = wy;
    c->ptrProf = pp < 0 ? CPROF_NORMAL : pp;
    c->whProf  = wp < 0 ? CPROF_SCROLL : wp;
    unsigned long now = g_now();
    if ((px || py) && !wasMoving) { c->ptrStart = now; c->ptrLast = now; c->ptrRemX = c->ptrRemY = 0; }
    if ((wx || wy) && !wasWheel)  { c->whStart = now;  c->whLast = now;  c->whRemX = c->whRemY = 0; }
}
// 한 번 이동·스크롤 (Chord = tap 일 때)
static void PtrStep(const ChordEntry *e) {
    if (e->act == CA_PTR_MOVE) { if (e->p1 || e->p2) SendMouseMove(e->p1, e->p2); return; }
    if (e->p2) { INPUT in; memset(&in, 0, sizeof in); in.type = INPUT_MOUSE; in.mi.dwExtraInfo = JAMO_SYNTH_MARK;
                 in.mi.dwFlags = MOUSEEVENTF_HWHEEL; in.mi.mouseData = (DWORD)(e->p2 * WHEEL_DELTA); SendInput(1, &in, sizeof(INPUT)); }
    if (e->p1) SendMouseWheel(e->p1 * WHEEL_DELTA);
}

// ── 3판 매크로 실행 (§6.6): 막지 않고 틱으로 한 단계씩. 취소는 자기가 누른 수정키만 놓는다 ──
static void MacroCancel(ChordKbContext *c) {
    if (c->macroIdx < 0) return;
    if (c->macroMods) { SendMods(c->macroMods, false); c->macroMods = 0; }
    c->macroIdx = -1; c->macroStep = 0;
}
static void MacroRun(ChordKbContext *c, const ChordLayout *cl) {
    while (c->macroIdx >= 0) {
        const ChordMacro *m = &cl->macros[c->macroIdx];
        if (g_now() - c->macroStart > CL_MACRO_MAX_MS) { MacroCancel(c); return; }   // 전체 시간 상한
        if (c->macroStep >= m->count) { MacroCancel(c); return; }                    // 끝 (수정키 정리 포함)
        const ChordMacroStep *st = &cl->steps[m->first + c->macroStep];
        c->macroStep++;
        switch (st->kind) {
            case MS_TEXT: SendText(c, cl->macroText + st->textOff, 0); break;
            // with 로 이미 누르고 있는 수정키는 그대로 두고, 이 단계의 mods() 만 씌운다 (이중 누름 금지)
            case MS_KEY:  SendVKey(st->vk, st->mod, st->p1 != 0); break;
            case MS_PTR: {
                ChordEntry e; memset(&e, 0, sizeof e);
                e.act = (ChordActionType)st->vk; e.p1 = st->p1; e.p2 = st->p2; e.prof = st->prof;
                if (e.act == CA_PTR_BTN) {
                    if (e.p2 != 2) PtrSendBtn(e.p1, true);
                    if (e.p2 != 1) PtrSendBtn(e.p1, false);
                } else PtrStep(&e);
                break; }
            case MS_WITH:    if (st->mod) { c->macroMods |= st->mod; SendMods(st->mod, true); } break;
            case MS_ENDWITH: if (c->macroMods) { SendMods(c->macroMods, false); c->macroMods = 0; } break;
            case MS_WAIT:    c->macroResume = g_now() + (unsigned long)st->p1; return;   // 다음 틱에서 이어간다
            default: break;
        }
    }
}
static void MacroStart(ChordKbContext *c, const ChordLayout *cl, int idx) {
    if (idx < 0 || idx >= cl->macroCount) return;
    MacroCancel(c);                    // 한 번에 하나 (§6.6) — 도는 중이면 먼저 취소한다
    c->macroIdx = idx; c->macroStep = 0; c->macroMods = 0;
    c->macroStart = c->macroResume = g_now();
    MacroRun(c, cl);
}

static void ExecChord(ChordKbContext *c, const ChordLayout *cl, const ChordEntry *e) {
    int mods = e->mod | c->oneshotMod;
    switch (e->act) {
        // 3판: 문자열에는 대기 중 원샷 수정키를 씌우지 않고 취소한다 (§6.2 — "a" 에 Shift 를 씌워 대문자/단축키로 만들지 않는다)
        case CA_TEXT:   SendText(c, e->text, (cl && cl->v3) ? 0 : c->oneshotMod); c->oneshotMod = 0; c->oneshotLayer = -1; break;
        case CA_KEY:    SendVKey(e->vk, mods, e->keyExt); c->oneshotMod = 0; c->oneshotLayer = -1; break;
        case CA_MOUSE_MOVE:  SendMouseMove(e->p1, e->p2); c->oneshotMod = 0; c->oneshotLayer = -1; break;
        case CA_MOUSE_BTN:   SendMouseBtn(e->p1, e->p2, c->oneshotMod); c->oneshotMod = 0; c->oneshotLayer = -1; break;
        case CA_MOUSE_WHEEL: SendMouseWheel(e->p1); c->oneshotMod = 0; c->oneshotLayer = -1; break;
        case CA_MOD_ONESHOT:   c->oneshotMod |= e->mod; break;   // 레이어 원샷은 유지
        case CA_LAYER_ONESHOT: c->oneshotLayer = (e->targetLayer >= 0) ? e->targetLayer : -1; break;
        case CA_LAYER_TOGGLE:  c->curLayer = (c->curLayer == e->targetLayer && e->targetLayer >= 0) ? 0 : (e->targetLayer >= 0 ? e->targetLayer : 0); c->oneshotLayer = -1; break;
        case CA_LAYER_SWITCH:  c->curLayer = (e->targetLayer >= 0) ? e->targetLayer : 0; c->oneshotLayer = -1; break;
        case CA_PTR_MOVE: case CA_PTR_WHEEL:   // tap 으로 쓰면 한 번 (Hold 면 ConfirmSustainedHold 가 연속으로 건다)
            PtrStep(e); c->oneshotMod = 0; c->oneshotLayer = -1; break;
        case CA_PTR_BTN:
            if (e->p2 == 3) {                  // drag-toggle
                if (c->dragBtn == e->p1) PtrReleaseDrag(c);
                else { PtrReleaseDrag(c); PtrSendBtn(e->p1, true); c->dragBtn = e->p1; }
            } else {
                if (e->p2 != 2) PtrSendBtn(e->p1, true);
                if (e->p2 != 1) PtrSendBtn(e->p1, false);
            }
            c->oneshotMod = 0; c->oneshotLayer = -1; break;
        case CA_CANCEL:
            PtrStopContinuous(c); PtrReleaseDrag(c); MacroCancel(c);
            c->oneshotMod = 0; c->oneshotLayer = -1; break;
        case CA_MACRO:
            MacroStart(c, cl, e->p1); c->oneshotMod = 0; c->oneshotLayer = -1; break;
        case CA_SYMBOL:   // 엔진으로 보내는 논리 입력 — 키 이벤트를 내지 않는다 (§6.3)
            if (c->symbolSink) c->symbolSink(c->symbolCtx, e->text);
            c->oneshotMod = 0; c->oneshotLayer = -1; break;
    }
}

void ChordKb_SetSymbolSink(ChordKbContext *c, void (*sink)(void *ctx, const wchar_t *sym), void *ctx) {
    if (!c) return;
    c->symbolSink = sink;
    c->symbolCtx = ctx;
}

void ChordKb_SetTextSink(ChordKbContext *c, void (*sink)(void *ctx, const wchar_t *s), void *ctx) {
    if (!c) return;
    c->textSink = sink;
    c->textCtx = ctx;
}

void ChordKb_ReleaseAll(ChordKbContext *c) {
    if (c->heldMod) SendMods(c->heldMod, false);   // 대상 앱에 Ctrl/Alt 가 눌린 채 남지 않게 (W1-09)
    PtrReleaseDrag(c);                             // 소유한 드래그도 놓는다 (§6.5 — 포커스 상실·전환에서 정리)
    MacroCancel(c);                                // 실행 중 매크로도 취소하고 그 수정키를 놓는다 (§6.6)
    int keep = c->curLayer;
    void (*sink)(void *, const wchar_t *) = c->symbolSink;
    void *sctx = c->symbolCtx;
    ChordKb_Init(c);
    c->curLayer = keep;
    c->symbolSink = sink; c->symbolCtx = sctx;   // 싱크는 자판이 아니라 입력기가 건 것이다
}

// 형성 중 조합을 판정·실행하고 비운다 (모두 해제 때, 또는 3판에서 닫힌 조합 뒤 새 글쇠가 눌렸을 때).
static void CompletePending(ChordKbContext *c, const ChordLayout *cl) {
    int layer = EffLayer(c);
    unsigned m = c->pendMask;
    unsigned long held = g_now() - c->pendTick;
    unsigned long holdMs = cl->v3 ? (unsigned long)cl->holdTermMs : CHORD_HOLD_MS;
    const ChordEntry *tap = FindEntry(cl, m, layer, 0);
    const ChordEntry *hold = FindEntry(cl, m, layer, 1);
    const ChordEntry *use = NULL;
    if (hold && !IsSustained(hold->act) && held >= holdMs) use = hold;  // 길게 → discrete hold
    else if (tap) use = tap;
    else if (hold && !IsSustained(hold->act)) use = hold;   // tap 없으면 discrete hold라도
    if (use) ExecChord(c, cl, use);
    else c->oneshotLayer = -1;   // 미매치도 원샷 레이어는 소비
    c->pendMask = 0; c->pendKeys = 0; c->pendClosed = false;
}
// 3판: mask 를 포함하는(같거나 더 큰) 선언 조합이 layer 에 있는가 — 있으면 hold 를 서두르지 않는다 (§7.1)
static bool AnyChordCovers(const ChordLayout *cl, unsigned mask, int layer) {
    for (int i = 0; i < cl->chordCount; i++)
        if (cl->chords[i].layer == layer && (cl->chords[i].mask & mask) == mask) return true;
    return false;
}

// 지속형 hold 확정: 형성 중 글쇠들을 hold 로 돌리고 레이어/모디파이어를 켠다.
static void ConfirmSustainedHold(ChordKbContext *c, const ChordEntry *he) {
    if (he->act == CA_PTR_MOVE || he->act == CA_PTR_WHEEL) {   // 연속 포인터: 이 조합의 글쇠마다 제 몫을 적어 둔다
        int dx = he->p1 > 0 ? 1 : he->p1 < 0 ? -1 : 0, dy = he->p2 > 0 ? 1 : he->p2 < 0 ? -1 : 0;
        for (int k = 0; k < 256; k++) {
            if (c->role[k] != 1) continue;
            if (he->act == CA_PTR_MOVE) { c->ptrKeyX[k] = (signed char)dx; c->ptrKeyY[k] = (signed char)dy; c->ptrKeyProf[k] = (unsigned char)he->prof; }
            else { c->whKeyX[k] = (signed char)dx; c->whKeyY[k] = (signed char)dy; c->whKeyProf[k] = (unsigned char)he->prof; }
        }
        for (int k = 0; k < 256; k++) if (c->role[k] == 1) c->role[k] = 2;
        c->holdKeys = c->pendKeys;
        c->pendMask = 0; c->pendKeys = 0; c->pendClosed = false;
        PtrRecalc(c);
        return;
    }
    if (he->act == CA_LAYER_ONESHOT) {
        if (he->targetLayer >= 0) c->momentaryLayer = he->targetLayer;
    } else {   // CA_MOD_ONESHOT → 모디파이어를 누른 채 유지
        c->heldMod |= he->mod;
        SendMods(he->mod, true);
    }
    for (int k = 0; k < 256; k++) if (c->role[k] == 1) c->role[k] = 2;   // pend → hold
    c->holdKeys = c->pendKeys;
    c->pendMask = 0; c->pendKeys = 0; c->pendClosed = false;
}

bool ChordKb_KeyDown(ChordKbContext *c, const ChordLayout *cl, UINT vk, wchar_t keyChar) {
    if (!cl || keyChar == 0 || keyChar >= 128) return false;
    int bit = cl->keyBit[(int)keyChar];
    if (bit < 0) return false;
    if (vk < 256 && c->keyDown[vk]) {
        if (GetKeyState((int)vk) & 0x8000) return true;   // 진짜 반복
        // keyup 유실로 박힌 유령 키 자가 치유 (chord.c와 동일 근거) — 새 눌림으로 재처리
        c->keyDown[vk] = false;
        if (c->role[vk] == 1 && c->pendKeys > 0) c->pendKeys--;   // role 3 (소비될 뗌) 은 셀 것이 없다
        else if (c->role[vk] == 2 && c->holdKeys > 0) c->holdKeys--;
        c->role[vk] = 0;
    }

    unsigned long now = g_now();
    // 사용자의 일반 키는 매크로를 취소한 뒤 처리한다 (§6.6 — 교차 실행 금지)
    if (c->macroIdx >= 0) MacroCancel(c);
    // 3판 겹친 세대(rolling): 첫 글쇠가 떨어져 닫힌 조합이 있으면 먼저 확정하고, 남은 글쇠의 뗌은 소비한다 (§7.1).
    if (cl->v3 && c->pendClosed && c->pendMask) {
        for (int k = 0; k < 256; k++) if (c->role[k] == 1) c->role[k] = 3;
        CompletePending(c, cl);
    }
    // 형성 중 조합이 '지속형 hold'(임시 레이어/모디파이어)이고 새 글쇠가 들어오면 → hold 확정(방해 기반).
    // 그 hold 글쇠들은 눌려 있는 동안 레이어/모디파이어를 유지하고, 새 글쇠는 새 조합을 시작한다.
    //   3판: 조합 시간 안(경계 포함)에 더 큰 선언 조합이 가능하면 기다리고, timeout 정책이면 HoldTermMs 전엔 hold 가 아니다.
    if (c->pendMask) {
        const ChordEntry *he = FindEntry(cl, c->pendMask, EffLayer(c), 1);
        bool confirm = he && IsSustained(he->act);
        if (confirm && cl->v3) {
            unsigned long el = now - c->pendTick;
            if (el <= (unsigned long)cl->comboTermMs && AnyChordCovers(cl, c->pendMask | (1u << bit), EffLayer(c))) confirm = false;
            else if (cl->holdPolicy == CHORD_HOLD_TIMEOUT && el < (unsigned long)cl->holdTermMs) confirm = false;
        }
        if (confirm) ConfirmSustainedHold(c, he);
    }

    if (vk < 256) { c->keyDown[vk] = true; c->role[vk] = 1; }
    if (c->pendKeys == 0) c->pendTick = now;
    c->pendKeys++;
    c->pendMask |= (1u << bit);
    return true;
}

bool ChordKb_KeyUp(ChordKbContext *c, const ChordLayout *cl, UINT vk) {
    if (vk >= 256 || !c->keyDown[vk]) return false;
    c->keyDown[vk] = false;
    int r = c->role[vk]; c->role[vk] = 0;

    if (r == 3) return true;   // 3판: 이미 확정된 조합의 남은 글쇠 — 뗌만 소비
    if (r == 2) {   // 지속형 hold 글쇠 해제
        if (c->ptrKeyX[vk] || c->ptrKeyY[vk] || c->whKeyX[vk] || c->whKeyY[vk]) {   // 연속 포인터 몫 반환
            c->ptrKeyX[vk] = c->ptrKeyY[vk] = c->whKeyX[vk] = c->whKeyY[vk] = 0;
            PtrRecalc(c);
        }
        if (c->holdKeys > 0) c->holdKeys--;
        if (c->holdKeys <= 0) {   // 모든 hold 글쇠 떨어짐 → 임시 레이어/모디파이어 복귀
            if (c->heldMod) { SendMods(c->heldMod, false); c->heldMod = 0; }
            c->momentaryLayer = -1; c->holdKeys = 0;
        }
        return true;
    }

    // r == 1: 형성 중 조합 글쇠 해제. 3판: 첫 해제로 조합이 닫힌다(새 글쇠는 다음 조합). 확정은 모두 해제 때.
    if (cl->v3) c->pendClosed = true;
    if (c->pendKeys > 0) c->pendKeys--;
    if (c->pendKeys <= 0) CompletePending(c, cl);   // 조합 완성
    return true;
}

// ── 3판: 키 이벤트 없이 흐르는 시간 (§7.1) ────────────────────────────────────────────────
//   형성 중 조합이 지속형 hold 후보이고, 조합 시간과 hold 시간이 모두 지나면 hold 를 켠다.
//   (조합 시간이 지나야 더 큰 조합 후보가 사라진 것이므로 두 정책 모두 같은 조건을 쓴다.)
static const ChordEntry *PendingSustainedHold(const ChordKbContext *c, const ChordLayout *cl) {
    if (!cl || !cl->v3 || !c->pendMask || c->pendClosed) return NULL;
    const ChordEntry *he = FindEntry(cl, c->pendMask, EffLayer(c), 1);
    return (he && IsSustained(he->act)) ? he : NULL;
}
int ChordKb_NextTickMs(const ChordKbContext *c, const ChordLayout *cl) {
    if (c->macroIdx >= 0) {                                           // 매크로가 기다리는 중 (§6.6)
        long ms = (long)(c->macroResume - g_now());
        return ms <= 0 ? 1 : (ms > 2000 ? 2000 : (int)ms);
    }
    if (c->ptrX || c->ptrY || c->whX || c->whY) return PTR_TICK_MS;   // 연속 포인터가 돌고 있다 (§6.5)
    if (!PendingSustainedHold(c, cl)) return 0;
    unsigned long deadline = c->pendTick + (unsigned long)(cl->holdTermMs > cl->comboTermMs ? cl->holdTermMs : cl->comboTermMs) + 1;
    unsigned long now = g_now();
    if ((long)(deadline - now) <= 0) return 1;
    long ms = (long)(deadline - now);
    return (ms > 60000) ? 60000 : (int)ms;
}
// 연속 이동·스크롤 한 틱. 가속은 시작 이후 경과로, 대각선은 정규화(×0.707), 잔량은 1/1000 단위로 누적.
static bool PtrTick(ChordKbContext *c) {
    bool did = false;
    unsigned long now = g_now();
    if (c->ptrX || c->ptrY) {
        long el = (long)(now - c->ptrStart), dt = (long)(now - c->ptrLast);
        if (dt < 0) dt = 0;
        c->ptrLast = now;
        long v = (long)kPtrProf[c->ptrProf].accel * el / 1000;
        if (v > kPtrProf[c->ptrProf].maxv) v = kPtrProf[c->ptrProf].maxv;
        long step = v * dt;                                   // 1/1000 픽셀
        if (c->ptrX && c->ptrY) step = step * 707 / 1000;      // 대각선 정규화
        c->ptrRemX += (int)(step * (c->ptrX > 0 ? 1 : c->ptrX < 0 ? -1 : 0));
        c->ptrRemY += (int)(step * (c->ptrY > 0 ? 1 : c->ptrY < 0 ? -1 : 0));
        int px = c->ptrRemX / 1000, py = c->ptrRemY / 1000;
        if (px || py) { c->ptrRemX -= px * 1000; c->ptrRemY -= py * 1000; SendMouseMove(px, py); did = true; }
    }
    if (c->whX || c->whY) {
        long el = (long)(now - c->whStart), dt = (long)(now - c->whLast);
        if (dt < 0) dt = 0;
        c->whLast = now;
        long v = (long)kPtrProf[c->whProf].accel * el / 1000;
        if (v > kPtrProf[c->whProf].maxv) v = kPtrProf[c->whProf].maxv;
        long step = v * dt;                                   // 1/1000 노치
        c->whRemX += (int)(step * (c->whX > 0 ? 1 : c->whX < 0 ? -1 : 0));
        c->whRemY += (int)(step * (c->whY > 0 ? 1 : c->whY < 0 ? -1 : 0));
        int nx = c->whRemX / 1000, ny = c->whRemY / 1000;
        if (ny) { c->whRemY -= ny * 1000; SendMouseWheel(ny * WHEEL_DELTA); did = true; }
        if (nx) {
            c->whRemX -= nx * 1000;
            INPUT in; memset(&in, 0, sizeof in); in.type = INPUT_MOUSE; in.mi.dwExtraInfo = JAMO_SYNTH_MARK;
            in.mi.dwFlags = MOUSEEVENTF_HWHEEL; in.mi.mouseData = (DWORD)(nx * WHEEL_DELTA);
            SendInput(1, &in, sizeof(INPUT)); did = true;
        }
    }
    return did;
}

bool ChordKb_Tick(ChordKbContext *c, const ChordLayout *cl) {
    bool moved = PtrTick(c);
    if (c->macroIdx >= 0 && (long)(g_now() - c->macroResume) >= 0) { MacroRun(c, cl); moved = true; }
    const ChordEntry *he = PendingSustainedHold(c, cl);
    if (!he) return moved;
    unsigned long el = g_now() - c->pendTick;
    if (el <= (unsigned long)cl->comboTermMs || el < (unsigned long)cl->holdTermMs) return moved;
    ConfirmSustainedHold(c, he);
    return true;
}
