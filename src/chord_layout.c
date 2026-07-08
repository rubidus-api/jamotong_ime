#include "chord_layout.h"
#include <stdio.h>
#include <string.h>

static void TrimEnds(wchar_t *s) {
    size_t n = wcslen(s);
    while (n > 0 && (s[n-1]==L'\n'||s[n-1]==L'\r'||s[n-1]==L' '||s[n-1]==L'\t')) s[--n]=L'\0';
}

// ── 이름 → 값 매핑 ────────────────────────────────────────────────────────────
// 키 이름 → 가상키. *ext에 확장키 여부(화살표·오른쪽 모디파이어·키패드 Enter/÷ 등)를 채운다.
static int KeyNameToVK(const wchar_t *n, bool *ext) {
    *ext = false;
    // 네비게이션(확장키) — 키패드와 구분됨
    if (!_wcsicmp(n, L"left"))  { *ext = true; return VK_LEFT; }
    if (!_wcsicmp(n, L"right")) { *ext = true; return VK_RIGHT; }
    if (!_wcsicmp(n, L"up"))    { *ext = true; return VK_UP; }
    if (!_wcsicmp(n, L"down"))  { *ext = true; return VK_DOWN; }
    if (!_wcsicmp(n, L"home"))  { *ext = true; return VK_HOME; }
    if (!_wcsicmp(n, L"end"))   { *ext = true; return VK_END; }
    if (!_wcsicmp(n, L"pgup"))  { *ext = true; return VK_PRIOR; }
    if (!_wcsicmp(n, L"pgdn"))  { *ext = true; return VK_NEXT; }
    if (!_wcsicmp(n, L"ins") || !_wcsicmp(n, L"insert")) { *ext = true; return VK_INSERT; }
    if (!_wcsicmp(n, L"del") || !_wcsicmp(n, L"delete")) { *ext = true; return VK_DELETE; }
    if (!_wcsicmp(n, L"apps") || !_wcsicmp(n, L"menu"))  { *ext = true; return VK_APPS; }
    if (!_wcsicmp(n, L"prtsc") || !_wcsicmp(n, L"printscreen")) { *ext = true; return VK_SNAPSHOT; }
    // 토글(락) 키 — 모두 비확장 스캔코드: NumLock 0x45, ScrollLock 0x46, CapsLock 0x3A
    if (!_wcsicmp(n, L"numlock") || !_wcsicmp(n, L"num")) return VK_NUMLOCK;
    if (!_wcsicmp(n, L"scrolllock") || !_wcsicmp(n, L"scroll") || !_wcsicmp(n, L"scrlk")) return VK_SCROLL;
    if (!_wcsicmp(n, L"capslock") || !_wcsicmp(n, L"caps")) return VK_CAPITAL;
    // 볼륨/미디어/브라우저 멀티미디어 키 (확장키)
    if (!_wcsicmp(n, L"volup") || !_wcsicmp(n, L"volumeup"))     { *ext = true; return VK_VOLUME_UP; }
    if (!_wcsicmp(n, L"voldown") || !_wcsicmp(n, L"volumedown")) { *ext = true; return VK_VOLUME_DOWN; }
    if (!_wcsicmp(n, L"volmute") || !_wcsicmp(n, L"mute"))       { *ext = true; return VK_VOLUME_MUTE; }
    if (!_wcsicmp(n, L"mnext") || !_wcsicmp(n, L"medianext"))    { *ext = true; return VK_MEDIA_NEXT_TRACK; }
    if (!_wcsicmp(n, L"mprev") || !_wcsicmp(n, L"mediaprev"))    { *ext = true; return VK_MEDIA_PREV_TRACK; }
    if (!_wcsicmp(n, L"mstop") || !_wcsicmp(n, L"mediastop"))    { *ext = true; return VK_MEDIA_STOP; }
    if (!_wcsicmp(n, L"mplay") || !_wcsicmp(n, L"playpause"))    { *ext = true; return VK_MEDIA_PLAY_PAUSE; }
    if (!_wcsicmp(n, L"browserback"))    { *ext = true; return VK_BROWSER_BACK; }
    if (!_wcsicmp(n, L"browserfwd") || !_wcsicmp(n, L"browserforward")) { *ext = true; return VK_BROWSER_FORWARD; }
    if (!_wcsicmp(n, L"browserrefresh")) { *ext = true; return VK_BROWSER_REFRESH; }
    if (!_wcsicmp(n, L"browserhome"))    { *ext = true; return VK_BROWSER_HOME; }
    if (!_wcsicmp(n, L"mail"))     { *ext = true; return VK_LAUNCH_MAIL; }
    if (!_wcsicmp(n, L"mediasel")) { *ext = true; return VK_LAUNCH_MEDIA_SELECT; }
    if (!_wcsicmp(n, L"calc"))     { *ext = true; return VK_LAUNCH_APP2; }
    if (!_wcsicmp(n, L"sleep")) return VK_SLEEP;
    // 기본(비확장)
    if (!_wcsicmp(n, L"back") || !_wcsicmp(n, L"backspace")) return VK_BACK;
    if (!_wcsicmp(n, L"enter") || !_wcsicmp(n, L"return")) return VK_RETURN;
    if (!_wcsicmp(n, L"tab")) return VK_TAB;
    if (!_wcsicmp(n, L"space")) return VK_SPACE;
    if (!_wcsicmp(n, L"esc") || !_wcsicmp(n, L"escape")) return VK_ESCAPE;
    if (!_wcsicmp(n, L"pause")) return VK_PAUSE;
    // 모디파이어를 일반 키로 (좌/우 구분)
    if (!_wcsicmp(n, L"lshift")) return VK_LSHIFT;
    if (!_wcsicmp(n, L"rshift")) return VK_RSHIFT;
    if (!_wcsicmp(n, L"lctrl"))  return VK_LCONTROL;
    if (!_wcsicmp(n, L"rctrl"))  { *ext = true; return VK_RCONTROL; }
    if (!_wcsicmp(n, L"lalt"))   return VK_LMENU;
    if (!_wcsicmp(n, L"ralt"))   { *ext = true; return VK_RMENU; }
    if (!_wcsicmp(n, L"lwin") || !_wcsicmp(n, L"lgui")) { *ext = true; return VK_LWIN; }
    if (!_wcsicmp(n, L"rwin") || !_wcsicmp(n, L"rgui")) { *ext = true; return VK_RWIN; }
    // 키패드 (numpad — 화살표/네비와 구분)
    if (!_wcsnicmp(n, L"kp", 2)) {
        const wchar_t *k = n + 2;
        if (k[0] >= L'0' && k[0] <= L'9' && k[1] == 0) return VK_NUMPAD0 + (k[0] - L'0');
        if (!_wcsicmp(k, L"add") || !_wcsicmp(k, L"plus"))  return VK_ADD;
        if (!_wcsicmp(k, L"sub") || !_wcsicmp(k, L"minus")) return VK_SUBTRACT;
        if (!_wcsicmp(k, L"mul") || !_wcsicmp(k, L"star"))  return VK_MULTIPLY;
        if (!_wcsicmp(k, L"div") || !_wcsicmp(k, L"slash")) { *ext = true; return VK_DIVIDE; }
        if (!_wcsicmp(k, L"dot") || !_wcsicmp(k, L"dec"))   return VK_DECIMAL;
        if (!_wcsicmp(k, L"enter")) { *ext = true; return VK_RETURN; }   // 키패드 Enter = 확장
    }
    // F1~F24 (보이지 않는 F13~24 포함)
    if ((n[0] == L'f' || n[0] == L'F') && n[1]) { int f = _wtoi(n + 1); if (f >= 1 && f <= 24) return VK_F1 + (f - 1); }
    // 단일 문자
    if (n[1] == L'\0') {
        wchar_t c = n[0];
        if (c >= L'a' && c <= L'z') return L'A' + (c - L'a');
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) return c;
    }
    return 0;
}
static int ModNameToBit(const wchar_t *n) {
    if (!_wcsicmp(n, L"shift") || !_wcsicmp(n, L"lshift")) return CMOD_LSHIFT;
    if (!_wcsicmp(n, L"rshift")) return CMOD_RSHIFT;
    if (!_wcsicmp(n, L"ctrl") || !_wcsicmp(n, L"control") || !_wcsicmp(n, L"lctrl")) return CMOD_LCTRL;
    if (!_wcsicmp(n, L"rctrl")) return CMOD_RCTRL;
    if (!_wcsicmp(n, L"alt") || !_wcsicmp(n, L"lalt")) return CMOD_LALT;
    if (!_wcsicmp(n, L"ralt") || !_wcsicmp(n, L"altgr")) return CMOD_RALT;
    if (!_wcsicmp(n, L"gui") || !_wcsicmp(n, L"win") || !_wcsicmp(n, L"lgui") || !_wcsicmp(n, L"lwin")) return CMOD_LGUI;
    if (!_wcsicmp(n, L"rgui") || !_wcsicmp(n, L"rwin")) return CMOD_RGUI;
    return 0;
}
static int LayerFind(ChordLayout *cl, const wchar_t *name) {
    for (int i = 0; i < cl->layerCount; i++) if (!_wcsicmp(cl->layerNames[i], name)) return i;
    return -1;
}
// 참조 시 없으면 등록 (레이어 전방 참조 허용: base의 조합이 뒤에 정의될 레이어를 가리킬 수 있음)
static int LayerFindOrAdd(ChordLayout *cl, const wchar_t *name) {
    int idx = LayerFind(cl, name);
    if (idx < 0 && cl->layerCount < CL_MAX_LAYERS) {
        wcscpy_s(cl->layerNames[cl->layerCount], 32, name);
        idx = cl->layerCount++;
    }
    return idx;
}
static void ExpandText(wchar_t *dst, size_t dstn, const wchar_t *src) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j+1 < dstn; i++) {
        if (src[i]==L'\\' && src[i+1]) {
            wchar_t c = src[++i];
            dst[j++] = (c==L'n')?L'\n':(c==L't')?L'\t':(c==L's')?L' ':(c==L'\\')?L'\\':c;
        } else dst[j++] = src[i];
    }
    dst[j] = L'\0';
}

// RHS 동작 문자열을 파싱해 e에 채운다. false = 잘못된 이름/대상 (RFC-0004 P1-3:
// 예전엔 unknown key/mod/layer가 무음 no-op 엔트리로 저장돼 "로드 성공인데 조합이 안 먹는"
// 상태가 됐다 — 이제 로드 실패로 표면화).
static bool ParseAction(ChordLayout *cl, ChordEntry *e, const wchar_t *rhs) {
    wchar_t verb[16] = {0}, a1[32] = {0}, a2[16] = {0};
    if (swscanf(rhs, L"%15ls %31ls %15ls", verb, a1, a2) >= 1) {
        if (!_wcsicmp(verb, L"key")) {
            bool ex = false; e->act = CA_KEY; e->vk = KeyNameToVK(a1, &ex); e->keyExt = ex;
            return e->vk != 0;   // 미지의 키 이름 거부
        }
        if (!_wcsicmp(verb, L"mod")) {
            e->act = CA_MOD_ONESHOT; e->mod = ModNameToBit(a1);
            return e->mod != 0;   // 미지의 모디파이어 이름 거부
        }
        if (!_wcsicmp(verb, L"layer")) {
            e->act = CA_LAYER_ONESHOT; e->targetLayer = LayerFindOrAdd(cl, a1);
            return e->targetLayer >= 0;   // 레이어 정원(CL_MAX_LAYERS) 초과 거부
        }
        if (!_wcsicmp(verb, L"tlayer")) {
            e->act = CA_LAYER_TOGGLE; e->targetLayer = LayerFindOrAdd(cl, a1);
            return e->targetLayer >= 0;
        }
        if (!_wcsicmp(verb, L"slayer")) {
            e->act = CA_LAYER_SWITCH; e->targetLayer = LayerFindOrAdd(cl, a1);
            return e->targetLayer >= 0;
        }
        if (!_wcsicmp(verb, L"mouse")) {
            if (!_wcsicmp(a1, L"move")) { e->act = CA_MOUSE_MOVE;
                // 두 번째 좌표는 rhs에서 재파싱
                int dx=0, dy=0; swscanf(rhs, L"mouse move %d %d", &dx, &dy); e->p1=dx; e->p2=dy; return true; }
            if (!_wcsicmp(a1, L"click") || !_wcsicmp(a1, L"down") || !_wcsicmp(a1, L"up")) {
                e->act = CA_MOUSE_BTN;
                e->p1 = (!_wcsicmp(a2, L"right"))?1 : (!_wcsicmp(a2, L"middle"))?2 : 0;
                e->p2 = (!_wcsicmp(a1, L"down"))?1 : (!_wcsicmp(a1, L"up"))?2 : 0;
                return true;
            }
            if (!_wcsicmp(a1, L"wheel")) {
                e->act = CA_MOUSE_WHEEL;
                if (!_wcsicmp(a2, L"up")) e->p1 = WHEEL_DELTA;
                else if (!_wcsicmp(a2, L"down")) e->p1 = -WHEEL_DELTA;
                else e->p1 = _wtoi(a2);
                e->p2 = 0; return true;
            }
            return false;   // mouse 뒤 미지의 하위 동작
        }
    }
    // 기본: 텍스트 (\b\n\t\s 는 특수키/문자로)
    if (!wcscmp(rhs, L"\\b")) { e->act = CA_KEY; e->vk = VK_BACK; return true; }
    if (!wcscmp(rhs, L"\\n")) { e->act = CA_KEY; e->vk = VK_RETURN; return true; }
    if (!wcscmp(rhs, L"\\t")) { e->act = CA_KEY; e->vk = VK_TAB; return true; }
    e->act = CA_TEXT;
    ExpandText(e->text, 24, rhs);
    return e->text[0] != L'\0';
}

ChordLayout *ChordLayout_LoadFromFile(const wchar_t *path) {
    FILE *fp = _wfopen(path, L"r, ccs=UTF-8");
    if (!fp) return NULL;
    ChordLayout *cl = (ChordLayout*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ChordLayout));
    if (!cl) { fclose(fp); return NULL; }
    wcscpy_s(cl->name, 64, L"chord");
    for (int i = 0; i < 128; i++) cl->keyBit[i] = -1;
    wcscpy_s(cl->layerNames[0], 32, L"base");
    cl->layerCount = 1;
    int curLayer = 0;
    bool bad = false;   // 잘못된 글쇠/동작 참조 발견 시 파일 전체 거부 (RFC-0004 P1-3)

    wchar_t line[256];
    while (fgetws(line, 256, fp)) {
        TrimEnds(line);
        wchar_t *p = line;
        while (*p==L' '||*p==L'\t') p++;
        if (*p==L'\0' || *p==L'#') continue;

        wchar_t name[32] = {0}, keys[32] = {0}, rhs[64] = {0};
        int bit = 0;

        if (swscanf(p, L"Name = %63l[^\n]", cl->name) == 1) { TrimEnds(cl->name); }
        else if (swscanf(p, L"Type = %31ls", name) == 1) { /* 통합 로더가 사용, 여기선 무시 */ }
        else if (swscanf(p, L"Key %31ls = %d", keys, &bit) == 2) {
            // 좌변 키 나열 = 배열 지정: 시작 비트부터 연속 배정 (예: Key jkl; = 0 → j0 k1 l2 ;3).
            // 단건(Key j = 0)은 길이 1의 특수형. 범위(0~31) 밖·비ASCII 키는 파일 거부.
            size_t nk = wcslen(keys);
            for (size_t i = 0; i < nk; i++) {
                int b = bit + (int)i;
                if ((unsigned)keys[i] < 128 && b >= 0 && b < 32) cl->keyBit[(int)keys[i]] = b;
                else { bad = true; break; }
            }
        }
        else if (swscanf(p, L"Layer %31ls", name) == 1) {
            int idx = LayerFindOrAdd(cl, name);
            if (idx >= 0) curLayer = idx;
            else bad = true;   // 레이어 정원(CL_MAX_LAYERS) 초과
        }
        else if ((!wcsncmp(p, L"Chord ", 6) || !wcsncmp(p, L"Hold ", 5)) && cl->chordCount < CL_MAX_CHORDS) {
            int isHold = (p[0] == L'H' || p[0] == L'h');
            const wchar_t *fmt = isHold ? L"Hold %31ls = %63l[^\n]" : L"Chord %31ls = %63l[^\n]";
            if (swscanf(p, fmt, keys, rhs) == 2) {
                unsigned mask = 0; bool ok = true;
                for (int i = 0; keys[i]; i++) {
                    int kb = (unsigned)keys[i] < 128 ? cl->keyBit[(int)keys[i]] : -1;
                    if (kb < 0) { ok = false; break; }
                    mask |= (1u << kb);
                }
                if (ok && mask) {
                    ChordEntry *e = &cl->chords[cl->chordCount];
                    memset(e, 0, sizeof(*e));
                    e->mask = mask; e->layer = curLayer; e->targetLayer = -1; e->isHold = isHold;
                    TrimEnds(rhs);
                    if (ParseAction(cl, e, rhs)) cl->chordCount++;
                    else bad = true;   // 미지의 key/mod/하위동작 이름 → 무음 no-op 대신 실패
                } else bad = true;     // Key로 선언 안 된 글쇠를 조합이 참조
            }
        }
    }
    fclose(fp);
    if (bad) { HeapFree(GetProcessHeap(), 0, cl); return NULL; }   // 부분 로드 대신 명시적 실패
    return cl;
}

void ChordLayout_Free(ChordLayout *cl) { if (cl) HeapFree(GetProcessHeap(), 0, cl); }

#define CHORD_HOLD_MS 200   // tap/hold 판정 임계 (ms)

void ChordKb_Init(ChordKbContext *c) {
    memset(c, 0, sizeof(*c));
    c->momentaryLayer = -1;
    c->oneshotLayer = -1;
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
static bool IsSustained(ChordActionType a) { return a == CA_LAYER_ONESHOT || a == CA_MOD_ONESHOT; }

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
static void SendText(const wchar_t *s, int mod) {
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

static void ExecChord(ChordKbContext *c, const ChordEntry *e) {
    int mods = e->mod | c->oneshotMod;
    switch (e->act) {
        case CA_TEXT:   SendText(e->text, c->oneshotMod); c->oneshotMod = 0; c->oneshotLayer = -1; break;
        case CA_KEY:    SendVKey(e->vk, mods, e->keyExt); c->oneshotMod = 0; c->oneshotLayer = -1; break;
        case CA_MOUSE_MOVE:  SendMouseMove(e->p1, e->p2); c->oneshotMod = 0; c->oneshotLayer = -1; break;
        case CA_MOUSE_BTN:   SendMouseBtn(e->p1, e->p2, c->oneshotMod); c->oneshotMod = 0; c->oneshotLayer = -1; break;
        case CA_MOUSE_WHEEL: SendMouseWheel(e->p1); c->oneshotMod = 0; c->oneshotLayer = -1; break;
        case CA_MOD_ONESHOT:   c->oneshotMod |= e->mod; break;   // 레이어 원샷은 유지
        case CA_LAYER_ONESHOT: c->oneshotLayer = (e->targetLayer >= 0) ? e->targetLayer : -1; break;
        case CA_LAYER_TOGGLE:  c->curLayer = (c->curLayer == e->targetLayer && e->targetLayer >= 0) ? 0 : (e->targetLayer >= 0 ? e->targetLayer : 0); c->oneshotLayer = -1; break;
        case CA_LAYER_SWITCH:  c->curLayer = (e->targetLayer >= 0) ? e->targetLayer : 0; c->oneshotLayer = -1; break;
    }
}

bool ChordKb_KeyDown(ChordKbContext *c, const ChordLayout *cl, UINT vk, wchar_t keyChar) {
    if (!cl || keyChar == 0 || keyChar >= 128) return false;
    int bit = cl->keyBit[(int)keyChar];
    if (bit < 0) return false;
    if (vk < 256 && c->keyDown[vk]) return true;   // 반복

    // 형성 중 조합이 '지속형 hold'(임시 레이어/모디파이어)이고 새 글쇠가 들어오면 → hold 확정(방해 기반).
    // 그 hold 글쇠들은 눌려 있는 동안 레이어/모디파이어를 유지하고, 새 글쇠는 새 조합을 시작한다.
    if (c->pendMask) {
        const ChordEntry *he = FindEntry(cl, c->pendMask, EffLayer(c), 1);
        if (he && IsSustained(he->act)) {
            if (he->act == CA_LAYER_ONESHOT) {
                if (he->targetLayer >= 0) c->momentaryLayer = he->targetLayer;
            } else {   // CA_MOD_ONESHOT → 모디파이어를 누른 채 유지
                c->heldMod |= he->mod;
                SendMods(he->mod, true);
            }
            for (int k = 0; k < 256; k++) if (c->role[k] == 1) c->role[k] = 2;   // pend → hold
            c->holdKeys = c->pendKeys;
            c->pendMask = 0; c->pendKeys = 0;
        }
    }

    if (vk < 256) { c->keyDown[vk] = true; c->role[vk] = 1; }
    if (c->pendKeys == 0) c->pendTick = GetTickCount();
    c->pendKeys++;
    c->pendMask |= (1u << bit);
    return true;
}

bool ChordKb_KeyUp(ChordKbContext *c, const ChordLayout *cl, UINT vk) {
    if (vk >= 256 || !c->keyDown[vk]) return false;
    c->keyDown[vk] = false;
    int r = c->role[vk]; c->role[vk] = 0;

    if (r == 2) {   // 지속형 hold 글쇠 해제
        if (c->holdKeys > 0) c->holdKeys--;
        if (c->holdKeys <= 0) {   // 모든 hold 글쇠 떨어짐 → 임시 레이어/모디파이어 복귀
            if (c->heldMod) { SendMods(c->heldMod, false); c->heldMod = 0; }
            c->momentaryLayer = -1; c->holdKeys = 0;
        }
        return true;
    }

    // r == 1: 형성 중 조합 글쇠 해제
    if (c->pendKeys > 0) c->pendKeys--;
    if (c->pendKeys <= 0) {   // 조합 완성
        int layer = EffLayer(c);
        unsigned m = c->pendMask;
        unsigned long held = (unsigned long)GetTickCount() - c->pendTick;
        const ChordEntry *tap = FindEntry(cl, m, layer, 0);
        const ChordEntry *hold = FindEntry(cl, m, layer, 1);
        const ChordEntry *use = NULL;
        if (hold && !IsSustained(hold->act) && held >= CHORD_HOLD_MS) use = hold;  // 길게 → discrete hold
        else if (tap) use = tap;
        else if (hold && !IsSustained(hold->act)) use = hold;   // tap 없으면 discrete hold라도
        if (use) ExecChord(c, use);
        else c->oneshotLayer = -1;   // 미매치도 원샷 레이어는 소비
        c->pendMask = 0; c->pendKeys = 0;
    }
    return true;
}
