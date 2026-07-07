#include "layout.h"

// 초성 19개 호환 자모
static const wchar_t c_ChoCompat[] = {
    L'ㄱ', L'ㄲ', L'ㄴ', L'ㄷ', L'ㄸ', L'ㄹ', L'ㅁ', L'ㅂ', L'ㅃ',
    L'ㅅ', L'ㅆ', L'ㅇ', L'ㅈ', L'ㅉ', L'ㅊ', L'ㅋ', L'ㅌ', L'ㅍ', L'ㅎ'
};

// 중성 21개 호환 자모
static const wchar_t c_JungCompat[] = {
    L'ㅏ', L'ㅐ', L'ㅑ', L'ㅒ', L'ㅓ', L'ㅔ', L'ㅕ', L'ㅖ', L'ㅗ',
    L'ㅘ', L'ㅙ', L'ㅚ', L'ㅛ', L'ㅜ', L'ㅝ', L'ㅞ', L'ㅟ', L'ㅠ',
    L'ㅡ', L'ㅢ', L'ㅣ'
};

// 종성 28개 호환 자모 (0번은 채움 문자용, 실제 출력은 안됨)
static const wchar_t c_JongCompat[] = {
    0, L'ㄱ', L'ㄲ', L'ㄳ', L'ㄴ', L'ㄵ', L'ㄶ', L'ㄷ', L'ㄹ',
    L'ㄺ', L'ㄻ', L'ㄼ', L'ㄽ', L'ㄾ', L'ㄿ', L'ㅀ', L'ㅁ', L'ㅂ',
    L'ㅄ', L'ㅅ', L'ㅆ', L'ㅇ', L'ㅈ', L'ㅊ', L'ㅋ', L'ㅌ', L'ㅍ', L'ㅎ'
};

// 2벌식 매핑 테이블 정밀 조정
// b = ㅠ (Jung 17)
// k = ㅏ (Jung 0) -> 위 a도 ㅏ(Jung 0)로 오타 났음. a는 ㅁ(Cho 6).
// 다시 정확하게!

static const LayoutResult c_DubeolMapLower[26] = {
    {JAMO_CHO, 6},   // a -> ㅁ
    {JAMO_JUNG, 17}, // b -> ㅠ
    {JAMO_CHO, 14},  // c -> ㅊ
    {JAMO_CHO, 11},  // d -> ㅇ
    {JAMO_CHO, 3},   // e -> ㄷ
    {JAMO_CHO, 5},   // f -> ㄹ
    {JAMO_CHO, 18},  // g -> ㅎ
    {JAMO_JUNG, 8},  // h -> ㅗ
    {JAMO_JUNG, 2},  // i -> ㅑ
    {JAMO_JUNG, 4},  // j -> ㅓ
    {JAMO_JUNG, 0},  // k -> ㅏ
    {JAMO_JUNG, 20}, // l -> ㅣ
    {JAMO_JUNG, 18}, // m -> ㅡ
    {JAMO_JUNG, 13}, // n -> ㅜ
    {JAMO_JUNG, 1},  // o -> ㅐ
    {JAMO_JUNG, 5},  // p -> ㅔ
    {JAMO_CHO, 7},   // q -> ㅂ
    {JAMO_CHO, 0},   // r -> ㄱ
    {JAMO_CHO, 2},   // s -> ㄴ
    {JAMO_CHO, 9},   // t -> ㅅ
    {JAMO_JUNG, 6},  // u -> ㅕ
    {JAMO_CHO, 17},  // v -> ㅍ
    {JAMO_CHO, 12},  // w -> ㅈ
    {JAMO_CHO, 16},  // x -> ㅌ
    {JAMO_JUNG, 12}, // y -> ㅛ
    {JAMO_CHO, 15}   // z -> ㅋ
};

static const LayoutResult c_DubeolMapUpper[26] = {
    {JAMO_CHO, 6},   // A -> ㅁ
    {JAMO_JUNG, 17}, // B -> ㅠ
    {JAMO_CHO, 14},  // C -> ㅊ
    {JAMO_CHO, 11},  // D -> ㅇ
    {JAMO_CHO, 4},   // E -> ㄸ
    {JAMO_CHO, 5},   // F -> ㄹ
    {JAMO_CHO, 18},  // G -> ㅎ
    {JAMO_JUNG, 8},  // H -> ㅗ
    {JAMO_JUNG, 2},  // I -> ㅑ
    {JAMO_JUNG, 4},  // J -> ㅓ
    {JAMO_JUNG, 0},  // K -> ㅏ
    {JAMO_JUNG, 20}, // L -> ㅣ
    {JAMO_JUNG, 18}, // M -> ㅡ
    {JAMO_JUNG, 13}, // N -> ㅜ
    {JAMO_JUNG, 3},  // O -> ㅒ
    {JAMO_JUNG, 7},  // P -> ㅖ
    {JAMO_CHO, 8},   // Q -> ㅃ
    {JAMO_CHO, 1},   // R -> ㄲ
    {JAMO_CHO, 2},   // S -> ㄴ
    {JAMO_CHO, 10},  // T -> ㅆ
    {JAMO_JUNG, 6},  // U -> ㅕ
    {JAMO_CHO, 17},  // V -> ㅍ
    {JAMO_CHO, 13},  // W -> ㅉ
    {JAMO_CHO, 16},  // X -> ㅌ
    {JAMO_JUNG, 12}, // Y -> ㅛ
    {JAMO_CHO, 15}   // Z -> ㅋ
};

// ── 세벌식 최종(3-91) 매핑 — QWERTY 산출 문자(ASCII) 인덱스 ──────────────────────────
// 구조: 오른손=초성, 가운데=중성, 왼쪽/숫자열=종성(받침). 겹받침은 직접 키가 있고, 된소리
// 초성(ㄲㄸㅃㅆㅉ)은 직접 키가 없어 같은 초성을 거듭 눌러 입력한다(Layout_CombineCho, FSM에서
// 세벌식일 때만). 겹모음은 모음 2개를 Layout_CombineJung 로 합친다.
// 출처/라이선스: 세벌식 최종은 공병우·한글문화원이 자유 이용을 위해 공개한 공개 표준 자판이며,
// 자판 배열(어느 키=어느 자모)은 사실(fact)·방법이라 저작권 대상이 아니다. 본 표는 그 공개 배열을
// 본 프로젝트 형식(ASCII 키 → 초/중/종성 인덱스)으로 독자 인코딩한 것이다. 자유 라이선스 참조:
// Wikimedia Commons "KB Sebeolsik Flnal.svg"(CC BY-SA 3.0), 한국어 위키백과 "세벌식 자판".
// 정확성은 실제 조합 검증으로 확인: 한글=m f s k g w, 까=k k f, 닭=u f @.
// 한계: 세벌식 최종은 숫자를 Shift+오른손 자리에 두는데(예: Shift+j=1), 본 IME는 아직 자모만
// 처리하므로 그 숫자 계층은 통과(패스스루)한다. — 자모 입력은 완전 지원.
static const LayoutResult c_SebeolMap[128] = {
    // 초성 (오른손 + 숫자열 0=ㅋ, 홑따옴표=ㅌ)
    ['k']={JAMO_CHO,0},  ['h']={JAMO_CHO,2},  ['u']={JAMO_CHO,3},  ['y']={JAMO_CHO,5},  ['i']={JAMO_CHO,6},
    [';']={JAMO_CHO,7},  ['n']={JAMO_CHO,9},  ['j']={JAMO_CHO,11}, ['l']={JAMO_CHO,12}, ['o']={JAMO_CHO,14},
    ['0']={JAMO_CHO,15}, ['\'']={JAMO_CHO,16},['p']={JAMO_CHO,17}, ['m']={JAMO_CHO,18},
    // 중성 (가운데 + 숫자열 4~9)
    ['f']={JAMO_JUNG,0},  ['r']={JAMO_JUNG,1},  ['6']={JAMO_JUNG,2},  ['G']={JAMO_JUNG,3},  ['t']={JAMO_JUNG,4},
    ['c']={JAMO_JUNG,5},  ['e']={JAMO_JUNG,6},  ['7']={JAMO_JUNG,7},  ['v']={JAMO_JUNG,8},  ['4']={JAMO_JUNG,12},
    ['9']={JAMO_JUNG,13}, ['5']={JAMO_JUNG,17}, ['g']={JAMO_JUNG,18}, ['8']={JAMO_JUNG,19}, ['d']={JAMO_JUNG,20},
    ['b']={JAMO_JUNG,13}, ['/']={JAMO_JUNG,8},
    // 종성 (왼쪽 + 숫자열 1~3 + Shift 겹받침)
    ['x']={JAMO_JONG,1},  ['!']={JAMO_JONG,2},  ['V']={JAMO_JONG,3},  ['s']={JAMO_JONG,4},  ['E']={JAMO_JONG,5},
    ['S']={JAMO_JONG,6},  ['A']={JAMO_JONG,7},  ['w']={JAMO_JONG,8},  ['@']={JAMO_JONG,9},  ['F']={JAMO_JONG,10},
    ['D']={JAMO_JONG,11}, ['T']={JAMO_JONG,12}, ['%']={JAMO_JONG,13}, ['$']={JAMO_JONG,14}, ['R']={JAMO_JONG,15},
    ['z']={JAMO_JONG,16}, ['3']={JAMO_JONG,17}, ['X']={JAMO_JONG,18}, ['q']={JAMO_JONG,19}, ['2']={JAMO_JONG,20},
    ['a']={JAMO_JONG,21}, ['#']={JAMO_JONG,22}, ['Z']={JAMO_JONG,23}, ['C']={JAMO_JONG,24}, ['W']={JAMO_JONG,25},
    ['Q']={JAMO_JONG,26}, ['1']={JAMO_JONG,27},
};

LayoutResult Layout_MapKeyToJamo(wchar_t keyChar, int variant) {
    if (variant == KBD_SEBEOL) {
        if (keyChar > 0 && keyChar < 128) return c_SebeolMap[(int)keyChar];
        LayoutResult none = {JAMO_NONE, 0};
        return none;
    }
    // KBD_DUBEOL (기본 2벌식)
    if (keyChar >= L'a' && keyChar <= L'z') return c_DubeolMapLower[keyChar - L'a'];
    if (keyChar >= L'A' && keyChar <= L'Z') return c_DubeolMapUpper[keyChar - L'A'];
    LayoutResult none = {JAMO_NONE, 0};
    return none;
}

// 드보락(미국) 배열 — QWERTY 물리 키가 내는 문자 → 드보락 출력 문자. 공개 표준(ANSI INCITS
// 207-1991)이라 자유 이용. 정적 맵이므로 기존 LAYOUT_TYPE_STATIC_MAP(charMap) 경로를 그대로 쓴다.
void Layout_FillDvorak(wchar_t *charMap) {
    for (int i = 0; i < 256; i++) charMap[i] = (wchar_t)i;   // 기본 항등
    static const char *q = "-=qwertyuiop[]asdfghjkl;'zxcvbnm,./_+QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>?";
    static const char *d = "[]',.pyfgcrl/=aoeuidhtns-;qjkxbmwvz{}\"<>PYFGCRL?+AOEUIDHTNS_:QJKXBMWVZ";
    for (int i = 0; q[i]; i++) charMap[(unsigned char)q[i]] = (wchar_t)d[i];
}

// 세벌식: 같은 초성을 거듭 누르면 된소리 (세벌식 최종엔 된소리 직접 키가 없음). 없으면 -1.
int Layout_CombineCho(int cho1, int cho2) {
    if (cho1 != cho2) return -1;
    switch (cho1) {
        case 0:  return 1;   // ㄱㄱ→ㄲ
        case 3:  return 4;   // ㄷㄷ→ㄸ
        case 7:  return 8;   // ㅂㅂ→ㅃ
        case 9:  return 10;  // ㅅㅅ→ㅆ
        case 12: return 13;  // ㅈㅈ→ㅉ
    }
    return -1;
}

// 세벌식: 직접 입력된 종성 두 개를 겹받침으로 결합 (없으면 -1)
int Layout_CombineJongPair(int jong1, int jong2) {
    if (jong1 == 1  && jong2 == 19) return 3;   // ㄱ+ㅅ=ㄳ
    if (jong1 == 4  && jong2 == 22) return 5;   // ㄴ+ㅈ=ㄵ
    if (jong1 == 4  && jong2 == 27) return 6;   // ㄴ+ㅎ=ㄶ
    if (jong1 == 8  && jong2 == 1)  return 9;   // ㄹ+ㄱ=ㄺ
    if (jong1 == 8  && jong2 == 16) return 10;  // ㄹ+ㅁ=ㄻ
    if (jong1 == 8  && jong2 == 17) return 11;  // ㄹ+ㅂ=ㄼ
    if (jong1 == 8  && jong2 == 19) return 12;  // ㄹ+ㅅ=ㄽ
    if (jong1 == 8  && jong2 == 25) return 13;  // ㄹ+ㅌ=ㄾ
    if (jong1 == 8  && jong2 == 26) return 14;  // ㄹ+ㅍ=ㄿ
    if (jong1 == 8  && jong2 == 27) return 15;  // ㄹ+ㅎ=ㅀ
    if (jong1 == 17 && jong2 == 19) return 18;  // ㅂ+ㅅ=ㅄ
    return -1;
}

wchar_t Layout_ChoToCompatJamo(int choIndex) {
    if (choIndex < 0 || choIndex >= HANGUL_CHO_COUNT) return 0;
    return c_ChoCompat[choIndex];
}

wchar_t Layout_JungToCompatJamo(int jungIndex) {
    if (jungIndex < 0 || jungIndex >= HANGUL_JUNG_COUNT) return 0;
    return c_JungCompat[jungIndex];
}

wchar_t Layout_JongToCompatJamo(int jongIndex) {
    if (jongIndex <= 0 || jongIndex >= HANGUL_JONG_COUNT) return 0;
    return c_JongCompat[jongIndex];
}

int Layout_ChoToJong(int choIndex) {
    static const int map[19] = {
        1, 2, 4, 7, -1, 8, 16, 17, -1, 19, 20, 21, 22, -1, 23, 24, 25, 26, 27
    };
    if (choIndex < 0 || choIndex > 18) return -1;
    return map[choIndex];
}

int Layout_CombineJung(int j1, int j2) {
    if (j1 == 8 && j2 == 0) return 9;   // ㅗ + ㅏ = ㅘ
    if (j1 == 8 && j2 == 1) return 10;  // ㅗ + ㅐ = ㅙ
    if (j1 == 8 && j2 == 20) return 11; // ㅗ + ㅣ = ㅚ
    if (j1 == 13 && j2 == 4) return 14; // ㅜ + ㅓ = ㅝ
    if (j1 == 13 && j2 == 5) return 15; // ㅜ + ㅔ = ㅞ
    if (j1 == 13 && j2 == 20) return 16;// ㅜ + ㅣ = ㅟ
    if (j1 == 18 && j2 == 20) return 19;// ㅡ + ㅣ = ㅢ
    return -1;
}

int Layout_CombineJong(int jong1, int cho2) {
    if (jong1 == 1 && cho2 == 9) return 3;   // ㄱ + ㅅ = ㄳ
    if (jong1 == 4 && cho2 == 12) return 5;  // ㄴ + ㅈ = ㄵ
    if (jong1 == 4 && cho2 == 18) return 6;  // ㄴ + ㅎ = ㄶ
    if (jong1 == 8 && cho2 == 0) return 9;   // ㄹ + ㄱ = ㄺ
    if (jong1 == 8 && cho2 == 6) return 10;  // ㄹ + ㅁ = ㄻ
    if (jong1 == 8 && cho2 == 7) return 11;  // ㄹ + ㅂ = ㄼ
    if (jong1 == 8 && cho2 == 9) return 12;  // ㄹ + ㅅ = ㄽ
    if (jong1 == 8 && cho2 == 16) return 13; // ㄹ + ㅌ = ㄾ
    if (jong1 == 8 && cho2 == 17) return 14; // ㄹ + ㅍ = ㄿ
    if (jong1 == 8 && cho2 == 18) return 15; // ㄹ + ㅎ = ㅀ
    if (jong1 == 17 && cho2 == 9) return 18; // ㅂ + ㅅ = ㅄ
    return -1;
}

void Layout_SplitJong(int combinedJong, int *outJong1, int *outCho2) {
    switch (combinedJong) {
        case 3:  *outJong1 = 1; *outCho2 = 9; break;   // ㄳ
        case 5:  *outJong1 = 4; *outCho2 = 12; break;  // ㄵ
        case 6:  *outJong1 = 4; *outCho2 = 18; break;  // ㄶ
        case 9:  *outJong1 = 8; *outCho2 = 0; break;   // ㄺ
        case 10: *outJong1 = 8; *outCho2 = 6; break;   // ㄻ
        case 11: *outJong1 = 8; *outCho2 = 7; break;   // ㄼ
        case 12: *outJong1 = 8; *outCho2 = 9; break;   // ㄽ
        case 13: *outJong1 = 8; *outCho2 = 16; break;  // ㄾ
        case 14: *outJong1 = 8; *outCho2 = 17; break;  // ㄿ
        case 15: *outJong1 = 8; *outCho2 = 18; break;  // ㅀ
        case 18: *outJong1 = 17; *outCho2 = 9; break;  // ㅄ
        default: {
            static const int c_JongToCho[28] = {
                -1, 0, 1, -1, 2, -1, -1, 3, 5, -1, -1, -1, -1, -1, -1, -1, 6, 7, -1, 9, 10, 11, 12, 14, 15, 16, 17, 18
            };
            *outJong1 = 0;
            if (combinedJong > 0 && combinedJong < 28) {
                *outCho2 = c_JongToCho[combinedJong];
            } else {
                *outCho2 = 0;
            }
            break;
        }
    }
}

// 가상키(VK) → QWERTY 문자 (shift 반영). windows.h 비의존(네이티브 테스트 빌드 호환) 위해 VK 값 직접.
wchar_t Layout_QwertyChar(unsigned vk, int shift) {
    if (vk >= 'A' && vk <= 'Z') return shift ? (wchar_t)vk : (wchar_t)(vk + 32);
    if (vk >= '0' && vk <= '9') {
        if (!shift) return (wchar_t)vk;
        static const wchar_t shiftNums[] = L")!@#$%^&*(";
        return shiftNums[vk - '0'];
    }
    switch (vk) {
        case 0xBA: return shift ? L':' : L';';   // VK_OEM_1
        case 0xBB: return shift ? L'+' : L'=';   // VK_OEM_PLUS
        case 0xBC: return shift ? L'<' : L',';   // VK_OEM_COMMA
        case 0xBD: return shift ? L'_' : L'-';   // VK_OEM_MINUS
        case 0xBE: return shift ? L'>' : L'.';   // VK_OEM_PERIOD
        case 0xBF: return shift ? L'?' : L'/';   // VK_OEM_2
        case 0xC0: return shift ? L'~' : L'`';   // VK_OEM_3
        case 0xDB: return shift ? L'{' : L'[';   // VK_OEM_4
        case 0xDC: return shift ? L'|' : L'\\';  // VK_OEM_5
        case 0xDD: return shift ? L'}' : L']';   // VK_OEM_6
        case 0xDE: return shift ? L'"' : L'\'';  // VK_OEM_7
        case 0x20: return L' ';                  // VK_SPACE
    }
    return 0;
}
