/* hangul2_test.c — LAB-FSM-001~008 (RFC-0009 §14.1)
 *
 * Windows 없이 도는 portable test. RFC §21: "첫 구현은 COM DLL이 아니다."
 * 빌드: cc -std=c11 -Iinclude -o t tests/hangul2_test.c src/hangul2.c && ./t
 */
#include "hangul2.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
static const char *g_case = "";

static void ok(bool cond, const char *what) {
    if (cond) { g_pass++; }
    else { g_fail++; printf("  FAIL [%s] %s\n", g_case, what); }
}
static void eq_u16(const WCHAR *got, uint8_t got_len,
                   const WCHAR *want, uint8_t want_len, const char *what) {
    bool same = (got_len == want_len) && (memcmp(got, want, (size_t)want_len * sizeof(WCHAR)) == 0);
    if (!same) {
        g_fail++;
        printf("  FAIL [%s] %s — got[%u]:", g_case, what, got_len);
        for (uint8_t i = 0; i < got_len; i++) printf(" U+%04X", got[i]);
        printf("  want[%u]:", want_len);
        for (uint8_t i = 0; i < want_len; i++) printf(" U+%04X", want[i]);
        printf("\n");
    } else g_pass++;
}

/* 키 문자열을 순서대로 눌러 확정+preedit를 모은다. 대문자는 Shift로 본다. */
typedef struct { WCHAR text[64]; uint8_t len; HangulState st; } Runner;
static void run_reset(Runner *r) { r->len = 0; Hangul2_Reset(&r->st); }
static void run_key(Runner *r, char c) {
    bool shift = (c >= 'A' && c <= 'Z');
    UINT vk = (UINT)(shift ? c : (c - 'a' + 'A'));
    HangulStep s;
    if (!Hangul2_Step(&r->st, vk, shift, &s)) return;
    for (uint8_t i = 0; i < s.committed_len; i++) r->text[r->len++] = s.committed[i];
    r->st = s.next;
}
static void run_bs(Runner *r) {
    HangulStep s;
    if (!Hangul2_Backspace(&r->st, &s)) return;
    r->st = s.next;
}
/* 확정 + 현재 preedit 을 합친 화면 문자열 */
static uint8_t run_screen(Runner *r, WCHAR *out) {
    uint8_t n = r->len;
    memcpy(out, r->text, (size_t)n * sizeof(WCHAR));
    n += Hangul2_Preedit(&r->st, out + n, 4);
    return n;
}
static void expect(const char *keys, const WCHAR *want, uint8_t want_len, const char *what) {
    Runner r; run_reset(&r);
    for (const char *p = keys; *p; p++) run_key(&r, *p);
    WCHAR got[64]; uint8_t n = run_screen(&r, got);
    eq_u16(got, n, want, want_len, what);
}
#define W1(a)       ((WCHAR[]){a}), 1
#define W2(a,b)     ((WCHAR[]){a,b}), 2
#define W3(a,b,c)   ((WCHAR[]){a,b,c}), 3

/* ── LAB-FSM-001: A-Z/Shift 두벌식 매핑 전체 ── */
static void fsm001(void) {
    g_case = "LAB-FSM-001";
    struct { char key; bool shift; JamoKind kind; int idx; } t[] = {
        /* 자음 14 + Shift 된소리 5 */
        {'r',0,JAMO_INITIAL,L_G},   {'r',1,JAMO_INITIAL,L_GG},
        {'s',0,JAMO_INITIAL,L_N},   {'e',0,JAMO_INITIAL,L_D},  {'e',1,JAMO_INITIAL,L_DD},
        {'f',0,JAMO_INITIAL,L_R},   {'a',0,JAMO_INITIAL,L_M},
        {'q',0,JAMO_INITIAL,L_B},   {'q',1,JAMO_INITIAL,L_BB},
        {'t',0,JAMO_INITIAL,L_S},   {'t',1,JAMO_INITIAL,L_SS},
        {'d',0,JAMO_INITIAL,L_NG},  {'w',0,JAMO_INITIAL,L_J},  {'w',1,JAMO_INITIAL,L_JJ},
        {'c',0,JAMO_INITIAL,L_C},   {'z',0,JAMO_INITIAL,L_K},
        {'x',0,JAMO_INITIAL,L_T},   {'v',0,JAMO_INITIAL,L_P},  {'g',0,JAMO_INITIAL,L_H},
        /* 모음 */
        {'k',0,JAMO_VOWEL,V_A},     {'o',0,JAMO_VOWEL,V_AE},   {'o',1,JAMO_VOWEL,V_YAE},
        {'i',0,JAMO_VOWEL,V_YA},    {'j',0,JAMO_VOWEL,V_EO},
        {'p',0,JAMO_VOWEL,V_E},     {'p',1,JAMO_VOWEL,V_YE},
        {'u',0,JAMO_VOWEL,V_YEO},   {'h',0,JAMO_VOWEL,V_O},    {'y',0,JAMO_VOWEL,V_YO},
        {'n',0,JAMO_VOWEL,V_U},     {'b',0,JAMO_VOWEL,V_YU},
        {'m',0,JAMO_VOWEL,V_EU},    {'l',0,JAMO_VOWEL,V_I},
    };
    for (unsigned i = 0; i < sizeof(t)/sizeof(t[0]); i++) {
        UINT vk = (UINT)(t[i].key - 'a' + 'A');
        JamoKey k = Hangul2_MapDubeolsik(vk, t[i].shift);
        char msg[64];
        snprintf(msg, sizeof msg, "%c%s", t[i].key, t[i].shift ? "+shift" : "");
        ok(k.kind == t[i].kind && k.index == t[i].idx, msg);
    }
    /* 매핑되지 않는 키는 JAMO_NONE — 통과시켜야 하는 키들 */
    JamoKey none = Hangul2_MapDubeolsik(0x20, false);   /* VK_SPACE */
    ok(none.kind == JAMO_NONE, "unmapped key -> JAMO_NONE");
}

/* ── LAB-FSM-002: 초성 19 × 중성 21 × 종성 28 음절 공식 ── */
static void fsm002(void) {
    g_case = "LAB-FSM-002";
    ok(Hangul2_Compose(L_G, V_A, T_NONE) == 0xAC00, "가 = U+AC00");
    ok(Hangul2_Compose(L_H, V_I, T_H)    == 0xD7A3, "힣 = U+D7A3");
    /* 전 조합이 현대 한글 범위 안에 있고 중복이 없다 */
    int seen_ok = 1;
    for (int l = 0; l < L_COUNT; l++)
        for (int v = 0; v < V_COUNT; v++)
            for (int t = 0; t < T_COUNT; t++) {
                WCHAR c = Hangul2_Compose(l, v, t);
                if (c < 0xAC00 || c > 0xD7A3) { seen_ok = 0; }
            }
    ok(seen_ok, "19*21*28 전부 U+AC00..U+D7A3");
    ok(L_COUNT * V_COUNT * T_COUNT == 11172, "총 11172음절");
    expect("rk",  W1(0xAC00), "rk -> 가");
    expect("rkr", W1(0xAC01), "rkr -> 각");
}

/* ── LAB-FSM-003: 겹모음 7개 결합/Backspace ── */
static void fsm003(void) {
    g_case = "LAB-FSM-003";
    ok(kCompoundVowelCount == 7, "겹모음 표 7개");
    /* 표 전체를 순회: 초성 ㅇ + 첫 모음 + 둘째 모음 = 결합 모음 */
    for (unsigned i = 0; i < kCompoundVowelCount; i++) {
        const PairMap *p = &kCompoundVowels[i];
        HangulState st; Hangul2_Reset(&st);
        st.initial = L_NG; st.vowel = p->first; st.final = T_NONE;
        st.history[0] = (HangulKeyStep){JAMO_INITIAL, L_NG};
        st.history[1] = (HangulKeyStep){JAMO_VOWEL, p->first};
        st.history_len = 2;
        /* 둘째 모음의 vk를 역으로 찾는다 */
        UINT vk = 0; bool sh = false;
        for (UINT k = 'A'; k <= 'Z' && !vk; k++)
            for (int s = 0; s < 2 && !vk; s++) {
                JamoKey jk = Hangul2_MapDubeolsik(k, s != 0);
                if (jk.kind == JAMO_VOWEL && jk.index == p->second) { vk = k; sh = (s != 0); }
            }
        HangulStep out;
        ok(vk != 0, "둘째 모음 키를 찾음");
        if (!vk) continue;
        ok(Hangul2_Step(&st, vk, sh, &out) && out.next.vowel == p->combined,
           "겹모음 결합");
        ok(out.committed_len == 0, "겹모음 결합은 확정 없음");
        /* Backspace 한 번이면 결합 전 모음으로 */
        HangulStep bs;
        ok(Hangul2_Backspace(&out.next, &bs) && bs.next.vowel == p->first,
           "Backspace로 결합 해제");
    }
    expect("rhk", W1(0xACFC), "rhk -> 과");
}

/* ── LAB-FSM-004: 겹받침 11개 결합/분리 ── */
static void fsm004(void) {
    g_case = "LAB-FSM-004";
    ok(kCompoundFinalCount == 11, "겹받침 표 11개");
    for (unsigned i = 0; i < kCompoundFinalCount; i++) {
        const PairMap *p = &kCompoundFinals[i];
        HangulState st; Hangul2_Reset(&st);
        st.initial = L_G; st.vowel = V_A; st.final = p->first;
        st.history_len = 0;
        int li = Hangul2_FinalToInitial(p->second);
        ok(li >= 0, "둘째 종성에 대응하는 초성이 있음");
        if (li < 0) continue;
        UINT vk = 0; bool sh = false;
        for (UINT k = 'A'; k <= 'Z' && !vk; k++)
            for (int s = 0; s < 2 && !vk; s++) {
                JamoKey jk = Hangul2_MapDubeolsik(k, s != 0);
                if (jk.kind == JAMO_INITIAL && jk.index == li) { vk = k; sh = (s != 0); }
            }
        if (!vk) { ok(false, "둘째 자음 키를 찾음"); continue; }
        HangulStep out;
        ok(Hangul2_Step(&st, vk, sh, &out) && out.next.final == p->combined,
           "겹받침 결합");
        ok(out.committed_len == 0, "겹받침 결합은 확정 없음");
    }
    expect("rkqt", W1(0xAC12), "rkqt -> 값");
}

/* ── LAB-FSM-005: 단일 종성 도깨비불 분리 ── */
static void fsm005(void) {
    g_case = "LAB-FSM-005";
    expect("rkrk",  W2(0xAC00, 0xAC00), "rkrk -> 가가");        /* 각+ㅏ = 가|가 */
    expect("rkqtk", W2(0xAC11, 0xC0AC), "rkqtk -> 갑사");        /* 값+ㅏ = 갑|사 */
    expect("dkssud", W2(0xC548, 0xB155), "dkssud -> 안녕");
    expect("gksrmf", W2(0xD55C, 0xAE00), "gksrmf -> 한글");
}

/* ── LAB-FSM-006: 된소리와 Shift key history ── */
static void fsm006(void) {
    g_case = "LAB-FSM-006";
    expect("Rk", W1(0xAE4C), "Shift+r,k -> 까");
    /* ㄲ를 Shift 한 번으로 만들면 Backspace 한 번에 초성이 통째로 사라진다 */
    Runner r; run_reset(&r);
    run_key(&r, 'R'); run_bs(&r);
    ok(Hangul2_IsEmpty(&r.st), "Shift+R 뒤 Backspace 한 번 -> 빈 상태");
    /* 종성 ㄳ는 ㄱ+ㅅ 두 단계이므로 Backspace가 ㄱ만 남긴다 */
    run_reset(&r);
    run_key(&r,'r'); run_key(&r,'k'); run_key(&r,'r'); run_key(&r,'t');
    WCHAR got[64]; uint8_t n = run_screen(&r, got);
    eq_u16(got, n, W1(0xAC03), "rkrt -> 갃");
    run_bs(&r);
    n = run_screen(&r, got);
    eq_u16(got, n, W1(0xAC01), "Backspace -> 각");
}

/* ── LAB-FSM-007: 모든 preedit 상태에서 Backspace가 끝까지 지운다 ── */
static void fsm007(void) {
    g_case = "LAB-FSM-007";
    const char *seqs[] = { "r", "rk", "rkr", "rkrt", "rhk", "Rk", "rkqt", "dks", "" };
    for (int i = 0; seqs[i][0]; i++) {
        Runner r; run_reset(&r);
        for (const char *p = seqs[i]; *p; p++) run_key(&r, *p);
        int guard = 0;
        while (!Hangul2_IsEmpty(&r.st) && guard++ < 32) run_bs(&r);
        char msg[64]; snprintf(msg, sizeof msg, "\"%s\" Backspace로 완전 소거", seqs[i]);
        ok(Hangul2_IsEmpty(&r.st) && guard <= 32, msg);
    }
    /* 빈 상태의 Backspace는 handled=false — 앱이 원래 Backspace를 받아야 한다 */
    HangulState st; Hangul2_Reset(&st);
    HangulStep out;
    ok(!Hangul2_Backspace(&st, &out), "빈 상태 Backspace는 통과시킨다");
}

/* ── LAB-FSM-008: 임의 100만 키에서 OOB/invalid UTF-16 없음 ── */
static void fsm008(void) {
    g_case = "LAB-FSM-008";
    uint32_t seed = 12345;
    HangulState st; Hangul2_Reset(&st);
    int bad = 0;
    for (int i = 0; i < 1000000; i++) {
        seed = seed * 1103515245u + 12345u;
        UINT vk = 'A' + (seed >> 16) % 26;
        bool sh = ((seed >> 8) & 1) != 0;
        HangulStep out;
        if (((seed >> 4) & 31) == 0) {            /* 가끔 Backspace */
            if (Hangul2_Backspace(&st, &out)) st = out.next;
            continue;
        }
        if (!Hangul2_Step(&st, vk, sh, &out)) continue;
        for (uint8_t k = 0; k < out.committed_len; k++) {
            WCHAR c = out.committed[k];
            bool syl  = (c >= 0xAC00 && c <= 0xD7A3);
            bool jamo = (c >= 0x3131 && c <= 0x3163);   /* 낱자 확정(초성만 치다 말았을 때) */
            if (!syl && !jamo) bad++;
        }
        for (uint8_t k = 0; k < out.preedit_len; k++) {
            WCHAR c = out.preedit[k];
            bool syl = (c >= 0xAC00 && c <= 0xD7A3);
            bool jamo = (c >= 0x3131 && c <= 0x3163);   /* 낱자 preedit */
            if (!syl && !jamo) bad++;
        }
        if (out.committed_len > 4 || out.preedit_len > 4) bad++;
        if (out.next.history_len > LAB_HISTORY_MAX) bad++;
        st = out.next;
    }
    ok(bad == 0, "100만 키 — 범위 밖 코드포인트/오버플로 0");
}

int main(void) {
    fsm001(); fsm002(); fsm003(); fsm004();
    fsm005(); fsm006(); fsm007(); fsm008();
    printf("\nhangul2: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
