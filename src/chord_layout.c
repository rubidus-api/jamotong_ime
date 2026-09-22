#include "chord_layout.h"
#include <stddef.h>
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
    if ((n[0] == L'f' || n[0] == L'F') && n[1]) {   // RFC-0016 P1: F 뒤는 숫자만 (전엔 f1junk 가 F1)
        int f = 0; const wchar_t *q = n + 1;
        for (; *q >= L'0' && *q <= L'9' && f < 100; q++) f = f * 10 + (*q - L'0');
        if (!*q && f >= 1 && f <= 24) return VK_F1 + (f - 1);
    }
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
// 넘치면 false (RFC-0016 P1 — 전엔 조용히 잘렸다)
static bool ExpandText(wchar_t *dst, size_t dstn, const wchar_t *src) {
    size_t j = 0, i = 0;
    for (; src[i] && j+1 < dstn; i++) {
        if (src[i]==L'\\' && src[i+1]) {
            wchar_t c = src[++i];
            dst[j++] = (c==L'n')?L'\n':(c==L't')?L'\t':(c==L's')?L' ':(c==L'\\')?L'\\':c;
        } else dst[j++] = src[i];
    }
    dst[j] = L'\0';
    return src[i] == L'\0';
}

// RHS 동작 문자열을 파싱해 e에 채운다. false = 잘못된 이름/대상 (RFC-0004 P1-3:
// 예전엔 unknown key/mod/layer가 무음 no-op 엔트리로 저장돼 "로드 성공인데 조합이 안 먹는"
// 상태가 됐다 — 이제 로드 실패로 표면화).
// 명령 동작의 토큰: 공백으로 나누고 '#' 로 시작하는 토큰부터는 줄 끝 주석 (RFC-0016 P1).
//   반환 = 토큰 수, 토큰이 너무 길면 -1.
#define ACT_MAXTOK 6
static int ActTokens(const wchar_t *s, wchar_t tok[ACT_MAXTOK][32]) {
    int n = 0;
    while (*s) {
        while (*s == L' ' || *s == L'\t') s++;
        if (!*s || *s == L'#') break;
        if (n >= ACT_MAXTOK) return n + 1;   // 너무 많다 — 호출자가 개수로 거부
        size_t k = 0;
        while (*s && *s != L' ' && *s != L'\t') { if (k >= 31) return -1; tok[n][k++] = *s++; }
        tok[n][k] = L'\0'; n++;
    }
    return n;
}
// 부호 있는 10진 정수 전체 + 범위 (전엔 swscanf %d 가 "5x" 를 5 로 받았다)
static bool StrictInt(const wchar_t *t, long lo, long hi, int *out) {
    wchar_t *end = NULL;
    if (!t[0]) return false;
    long v = wcstol(t, &end, 10);
    if (!end || *end || v < lo || v > hi) return false;
    *out = (int)v; return true;
}
#define PA_OK        0
#define PA_BAD       1   // 모르는 동작·이름, 여분 토큰, 잘못된 숫자
#define PA_TEXT_LONG 2   // 문자열이 ChordEntry.text 에 들어가지 않는다

static int ParseAction(ChordLayout *cl, ChordEntry *e, const wchar_t *rhs) {
    wchar_t t[ACT_MAXTOK][32];
    int n = ActTokens(rhs, t);
    if (n >= 1) {
        const wchar_t *verb = t[0];
        if (!_wcsicmp(verb, L"key")) {
            bool ex = false;
            if (n != 2) return PA_BAD;   // 'key A junk' 거부 (RFC-0016 P1)
            e->act = CA_KEY; e->vk = KeyNameToVK(t[1], &ex); e->keyExt = ex;
            return e->vk ? PA_OK : PA_BAD;   // 미지의 키 이름 거부
        }
        if (!_wcsicmp(verb, L"mod")) {
            if (n != 2) return PA_BAD;
            e->act = CA_MOD_ONESHOT; e->mod = ModNameToBit(t[1]);
            return e->mod ? PA_OK : PA_BAD;   // 미지의 모디파이어 이름 거부
        }
        if (!_wcsicmp(verb, L"layer") || !_wcsicmp(verb, L"tlayer") || !_wcsicmp(verb, L"slayer")) {
            if (n != 2) return PA_BAD;
            e->act = !_wcsicmp(verb, L"layer") ? CA_LAYER_ONESHOT : !_wcsicmp(verb, L"tlayer") ? CA_LAYER_TOGGLE : CA_LAYER_SWITCH;
            e->targetLayer = LayerFindOrAdd(cl, t[1]);
            return e->targetLayer >= 0 ? PA_OK : PA_BAD;   // 레이어 정원(CL_MAX_LAYERS) 초과 거부
        }
        if (!_wcsicmp(verb, L"mouse")) {
            if (n < 2) return PA_BAD;
            if (!_wcsicmp(t[1], L"move")) {   // W2-02: 좌표 둘 다, RFC-0016 P1: 정수 전체·범위·여분 없음
                int dx, dy;
                if (n != 4 || !StrictInt(t[2], -10000, 10000, &dx) || !StrictInt(t[3], -10000, 10000, &dy)) return PA_BAD;
                e->act = CA_MOUSE_MOVE; e->p1 = dx; e->p2 = dy; return PA_OK;
            }
            if (!_wcsicmp(t[1], L"click") || !_wcsicmp(t[1], L"down") || !_wcsicmp(t[1], L"up")) {
                if (n > 3) return PA_BAD;
                const wchar_t *b = (n == 3) ? t[2] : L"left";
                if (_wcsicmp(b, L"left") && _wcsicmp(b, L"right") && _wcsicmp(b, L"middle")) return PA_BAD;   // W2-02
                e->act = CA_MOUSE_BTN;
                e->p1 = (!_wcsicmp(b, L"right"))?1 : (!_wcsicmp(b, L"middle"))?2 : 0;
                e->p2 = (!_wcsicmp(t[1], L"down"))?1 : (!_wcsicmp(t[1], L"up"))?2 : 0;
                return PA_OK;
            }
            if (!_wcsicmp(t[1], L"wheel")) {
                if (n != 3) return PA_BAD;
                e->act = CA_MOUSE_WHEEL; e->p2 = 0;
                if (!_wcsicmp(t[2], L"up")) e->p1 = WHEEL_DELTA;
                else if (!_wcsicmp(t[2], L"down")) e->p1 = -WHEEL_DELTA;
                else if (!StrictInt(t[2], -12000, 12000, &e->p1)) return PA_BAD;
                return PA_OK;
            }
            return PA_BAD;   // mouse 뒤 미지의 하위 동작
        }
    }
    // 기본: 텍스트 (\b\n\t\s 는 특수키/문자로). 텍스트에선 '#' 도 글자다.
    if (!wcscmp(rhs, L"\\b")) { e->act = CA_KEY; e->vk = VK_BACK; return PA_OK; }
    if (!wcscmp(rhs, L"\\n")) { e->act = CA_KEY; e->vk = VK_RETURN; return PA_OK; }
    if (!wcscmp(rhs, L"\\t")) { e->act = CA_KEY; e->vk = VK_TAB; return PA_OK; }
    e->act = CA_TEXT;
    // 줄 끝 주석: 공백 뒤의 '#' 부터 (README 예제의 형태). 맨 앞의 '#', 'C#' 처럼 붙은 '#', '\#' 은 글자다.
    //   전엔 주석까지 문자열로 읽어 23자에서 잘랐다 (RFC-0016 P1).
    wchar_t body[256];
    lstrcpynW(body, rhs, 256);
    for (int i = 1; body[i]; i++)
        if (body[i] == L'#' && (body[i-1] == L' ' || body[i-1] == L'\t')) {
            body[i] = L'\0';
            for (int k = i - 1; k >= 0 && (body[k] == L' ' || body[k] == L'\t'); k--) body[k] = L'\0';
            break;
        }
    if (!ExpandText(e->text, sizeof(e->text) / sizeof(e->text[0]), body)) return PA_TEXT_LONG;
    return e->text[0] != L'\0' ? PA_OK : PA_BAD;
}

// 오류를 모두 기록 (RFC-0011 P1) — col 은 1-based.
#define FAIL(col, code, msg, help) do { KlayDiag_Add(diag, KLAY_SEV_ERROR, lineno, (col), (code), (msg), (help)); \
    bad = true; } while (0)
static const wchar_t *const kChordDirectives[] = { L"Key", L"Layer", L"Chord", L"Hold", NULL };

ChordLayout *ChordLayout_LoadFromFile(const wchar_t *path, KlayDiag *diag) {
    KlayLines L;
    bool built = KlayLines_Build(&L, path, diag);
    ChordLayout *cl = built ? ChordLayout_LoadFromLines(&L, diag) : NULL;
    KlayLines_Free(&L);
    return cl;
}

// `Key <keys> [base|shift] = <bit>` (P6). 머리가 틀리면 진단을 내고 bad 를 세운 뒤 false(다른 분기로 안 감).
static bool ChordKeyHead(const wchar_t *p, wchar_t *keys, size_t cch, int *bit, KlayDiag *diag, int lineno, int col, bool *bad) {
    const wchar_t *sp = NULL;
    if (!Klay_ParseKeyHead(p, keys, cch, &sp, diag, lineno, col)) { *bad = true; return false; }
    if (swscanf(sp, L" %d", bit) != 1) {
        KlayDiag_Add(diag, KLAY_SEV_ERROR, lineno, col, L"E-JMT-KEY-SYNTAX", L"Key: expected a bit number after '='", L"e.g. 'Key j = 0'");
        *bad = true;
        return false;
    }
    return true;
}

ChordLayout *ChordLayout_LoadFromLines(const KlayLines *L, KlayDiag *diag) {
    ChordLayout *cl = (ChordLayout*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ChordLayout));
    if (!cl) return NULL;
    wcscpy_s(cl->name, 64, L"chord");
    for (int i = 0; i < 128; i++) cl->keyBit[i] = -1;
    wcscpy_s(cl->layerNames[0], 32, L"base");
    cl->layerCount = 1;
    int curLayer = 0;
    bool bad = false;   // 잘못된 글쇠/동작 참조 발견 시 파일 전체 거부 (RFC-0004 P1-3)
    // 각 조합을 정의한 파일 (상속 덮어쓰기 판정, RFC-0016 §4). 로드마다 따로 — 설정창·입력 스레드가 겹쳐도 안전.
    unsigned char *srcFile = (unsigned char *)calloc(CL_MAX_CHORDS, 1);
    if (!srcFile) { HeapFree(GetProcessHeap(), 0, cl); return NULL; }
    int lineno = 0;

    wchar_t line[256];
    for (int li = 0; li < L->n; li++) {
        lstrcpynW(line, L->v[li].text, 256);
        lineno = L->v[li].line;
        if (diag) diag->curFile = L->files[L->v[li].file];
        TrimEnds(line);
        wchar_t *p = line;
        while (*p==L' '||*p==L'\t') p++;
        if (*p==L'\0' || *p==L'#') continue;
        const int col0 = (int)(p - line) + 1;

        wchar_t name[32] = {0}, keys[32] = {0}, rhs[256] = {0};   // rhs = 줄 한도까지 (전엔 63자에서 잘렸다)
        int bit = 0;

        if (swscanf(p, L"Name = %63l[^\n]", cl->name) == 1) { TrimEnds(cl->name); }
        else if (swscanf(p, L"Type = %31ls", name) == 1) { /* 통합 로더가 사용, 여기선 무시 */ }
        else if (!wcsncmp(p, L"Key ", 4)) {
            if (!ChordKeyHead(p + 4, keys, 32, &bit, diag, lineno, col0 + 4, &bad)) continue;
            // 좌변 키 나열 = 배열 지정: 시작 비트부터 연속 배정 (예: Key jkl; = 0 → j0 k1 l2 ;3).
            // 단건(Key j = 0)은 길이 1의 특수형. 범위(0~31) 밖·비ASCII 키는 파일 거부.
            size_t nk = wcslen(keys);
            for (size_t i = 0; i < nk; i++) {
                int b = bit + (int)i;
                if ((unsigned)keys[i] < 128 && b >= 0 && b < 32) cl->keyBit[(int)keys[i]] = b;
                else { FAIL(col0 + 4 + (int)i, L"E-JMT-RANGE", L"Key: bit out of range (0..31) or non-ASCII key", L"bits 0..31; a key list takes consecutive bits from the start bit"); break; }
            }
        }
        else if (swscanf(p, L"Layer %31ls", name) == 1) {
            int idx = LayerFindOrAdd(cl, name);
            if (idx >= 0) curLayer = idx;
            else FAIL(col0, L"E-JMT-LIMIT", L"too many layers (max 16)", NULL);
        }
        else if (!wcsncmp(p, L"Chord ", 6) || !wcsncmp(p, L"Hold ", 5)) {
            if (cl->chordCount >= CL_MAX_CHORDS) { FAIL(col0, L"E-JMT-LIMIT", L"too many chords (max 2048)", NULL); continue; }
            int isHold = (p[0] == L'H' || p[0] == L'h');
            const wchar_t *fmt = isHold ? L"Hold %31ls = %255l[^\n]" : L"Chord %31ls = %255l[^\n]";
            if (swscanf(p, fmt, keys, rhs) == 2) {
                unsigned mask = 0; bool ok = true;
                for (int i = 0; keys[i]; i++) {
                    int kb = (unsigned)keys[i] < 128 ? cl->keyBit[(int)keys[i]] : -1;
                    if (kb < 0) { ok = false; break; }
                    mask |= (1u << kb);
                }
                if (ok && mask) {
                    ChordEntry ne; memset(&ne, 0, sizeof(ne));
                    ne.mask = mask; ne.layer = curLayer; ne.targetLayer = -1; ne.isHold = isHold;
                    TrimEnds(rhs);
                    int pr = ParseAction(cl, &ne, rhs);
                    if (pr == PA_OK) {
                        // 같은 (layer, mask, tap/hold) — RFC-0016 §4: 상속(다른 파일)은 나중 정의 우선,
                        // 같은 파일 안의 중복은 v1/v2 에선 첫 정의 유지 + 경고 (v3 에선 오류 예정).
                        int dup = -1;
                        for (int j = 0; j < cl->chordCount; j++)
                            if (cl->chords[j].mask == mask && cl->chords[j].layer == curLayer && cl->chords[j].isHold == isHold) { dup = j; break; }
                        if (dup < 0) { srcFile[cl->chordCount] = (unsigned char)L->v[li].file; cl->chords[cl->chordCount++] = ne; }
                        else if (srcFile[dup] != (unsigned char)L->v[li].file) { cl->chords[dup] = ne; srcFile[dup] = (unsigned char)L->v[li].file; }
                        else KlayDiag_Add(diag, KLAY_SEV_WARNING, lineno, col0, L"W-JMT-DUP-CHORD",
                                          L"same chord defined twice in this file - the first definition is used",
                                          L"remove one of the two lines");
                    }
                    else if (pr == PA_TEXT_LONG)
                        FAIL(col0, L"E-JMT-TEXT-LONG", L"chord text is longer than 23 characters",
                             L"shorten the text (at most 23 characters after \\n, \\t and \\s)");
                    else FAIL(col0, L"E-JMT-ACTION", L"unknown action, or extra/invalid words after it",
                              L"use text, 'key <name>', 'mod <name>', 'layer <name>' or a mouse action; comments start with #");
                } else FAIL(col0 + (isHold ? 5 : 6), L"E-JMT-UNDECLARED", L"chord references a key not declared with 'Key'",
                            L"declare every chord key first, e.g. 'Key j = 0'");
            } else FAIL(col0, L"E-JMT-KEY-SYNTAX", L"malformed Chord/Hold line (missing '=')", NULL);
        }
        else if (KlayHeader_IsKnownKey(p)) { /* 머리부 — 통합 로더가 읽는다 */ }
        else if (Klay_UnknownLine(diag, p, lineno, col0, kChordDirectives)) bad = true;   // v1 경고 / v2 오류 (P2)
    }
    if (diag) diag->curFile = NULL;
    // W2-02: 참조만 되고 조합이 하나도 없는 레이어 = 대개 이름 오타 (layer nmu). 막지는 않고 알린다.
    for (int l = 1; !bad && l < cl->layerCount; l++) {
        bool used = false;
        for (int i = 0; i < cl->chordCount && !used; i++) if (cl->chords[i].layer == l) used = true;
        if (!used) {
            wchar_t msg[160]; swprintf(msg, 160, L"layer '%ls' has no chords", cl->layerNames[l]);
            KlayDiag_Add(diag, KLAY_SEV_WARNING, 0, 1, L"W-JMT-EMPTY-LAYER", msg, L"check the layer name, or add 'Layer <name>' and its chords");
        }
    }
    free(srcFile);
    if (bad) { HeapFree(GetProcessHeap(), 0, cl); return NULL; }   // 부분 로드 대신 명시적 실패
    // 정확 크기로 축소 재할당 (RFC-0011 P0): 고정 배열 chords[2048] 전체(≈182KB)를 모든 호스트
    // 프로세스가 지는 대신, 실제 조합 수만큼만. 구조체 선언은 그대로 두되 할당만 줄인다 —
    // 소비자는 chords[0..chordCount) 만 읽는다(전체 memcpy/sizeof(ChordLayout) 사용처 없음 확인).
    {
        size_t need = offsetof(ChordLayout, chords) + (size_t)cl->chordCount * sizeof(ChordEntry);
        ChordLayout *shrunk = (ChordLayout*)HeapAlloc(GetProcessHeap(), 0, need);
        if (shrunk) { memcpy(shrunk, cl, need); HeapFree(GetProcessHeap(), 0, cl); cl = shrunk; }
    }
    return cl;
}
#undef FAIL

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

void ChordKb_ReleaseAll(ChordKbContext *c) {
    if (c->heldMod) SendMods(c->heldMod, false);   // 대상 앱에 Ctrl/Alt 가 눌린 채 남지 않게 (W1-09)
    int keep = c->curLayer;
    ChordKb_Init(c);
    c->curLayer = keep;
}

bool ChordKb_KeyDown(ChordKbContext *c, const ChordLayout *cl, UINT vk, wchar_t keyChar) {
    if (!cl || keyChar == 0 || keyChar >= 128) return false;
    int bit = cl->keyBit[(int)keyChar];
    if (bit < 0) return false;
    if (vk < 256 && c->keyDown[vk]) {
        if (GetKeyState((int)vk) & 0x8000) return true;   // 진짜 반복
        // keyup 유실로 박힌 유령 키 자가 치유 (chord.c와 동일 근거) — 새 눌림으로 재처리
        c->keyDown[vk] = false;
        if (c->role[vk] == 1 && c->pendKeys > 0) c->pendKeys--;
        else if (c->role[vk] == 2 && c->holdKeys > 0) c->holdKeys--;
        c->role[vk] = 0;
    }

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
