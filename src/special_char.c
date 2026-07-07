#include "special_char.h"

// 자음별 특수문자 표 (KS X 1001 관례를 따른 실용 부분집합). 각 후보는 단일 문자열.
// MS 한글 IME의 "자음 + 한자키" 관례: ㅁ=일반기호, ㅅ=그리스, ㅇ=원문자, ㅈ=로마숫자 등.

static const wchar_t *S_GIYEOK[] = {   // ㄱ: 문장부호/일반
    L"！", L"＇", L"，", L"．", L"／", L"：", L"；", L"？", L"＾", L"＿",
    L"｀", L"｜", L"～", L"´", L"～", L"ˇ", L"˘", L"˝", L"¨", L"°",
    L"·", L"‥", L"…", L"¸", L"˛", L"‘", L"’", L"“", L"”", L"〔",
    L"〕", L"§", L"※", L"☆", L"★", L"○", L"●", L"◎", L"◇", L"◆",
};
static const wchar_t *S_NIEUN[] = {    // ㄴ: 괄호
    L"（", L"）", L"｛", L"｝", L"〔", L"〕", L"【", L"】", L"〈", L"〉",
    L"《", L"》", L"「", L"」", L"『", L"』", L"〖", L"〗", L"［", L"］",
    L"‹", L"›", L"«", L"»",
};
static const wchar_t *S_DIGEUT[] = {   // ㄷ: 수학/학술
    L"＋", L"－", L"＜", L"＝", L"＞", L"±", L"×", L"÷", L"≠", L"≤",
    L"≥", L"∞", L"∴", L"♂", L"♀", L"∠", L"⊥", L"⌒", L"∂", L"∇",
    L"≡", L"≒", L"≪", L"≫", L"√", L"∽", L"∝", L"∵", L"∫", L"∬",
    L"∈", L"∋", L"⊆", L"⊇", L"⊂", L"⊃", L"∪", L"∩", L"∧", L"∨",
    L"￢", L"⇒", L"⇔", L"∀", L"∃",
};
static const wchar_t *S_RIEUL[] = {    // ㄹ: 단위
    L"㎜", L"㎝", L"㎞", L"㎎", L"㎏", L"㏈", L"㎧", L"㎨", L"㎡", L"㎥",
    L"㎠", L"㎢", L"㎣", L"㎤", L"㎦", L"㎖", L"㎗", L"ℓ", L"㏄", L"℃",
    L"℉", L"°", L"′", L"″", L"㏊", L"㎍", L"㎉", L"㎾", L"㎿", L"Ω",
    L"㏀", L"㎐", L"㎑", L"㎒", L"㎓", L"㎔", L"＄", L"￡", L"￥", L"₩",
};
static const wchar_t *S_MIEUM[] = {    // ㅁ: 일반기호 (가장 자주 쓰임)
    L"※", L"☆", L"★", L"○", L"●", L"◎", L"◇", L"◆", L"□", L"■",
    L"△", L"▲", L"▽", L"▼", L"→", L"←", L"↑", L"↓", L"↔", L"↕",
    L"↗", L"↘", L"↙", L"↖", L"◁", L"◀", L"▷", L"▶", L"♠", L"♣",
    L"♥", L"♡", L"♧", L"♤", L"⊙", L"◈", L"▣", L"◐", L"◑", L"▒",
    L"§", L"¶", L"†", L"‡", L"♨", L"☏", L"☎", L"☜", L"☞", L"♭",
    L"♪", L"♩", L"♬", L"㉿", L"㈜", L"№", L"™", L"㏂", L"㏘", L"℡",
};
static const wchar_t *S_BIEUP[] = {    // ㅂ: 원/괄호 한글·숫자
    L"㉠", L"㉡", L"㉢", L"㉣", L"㉤", L"㉥", L"㉦", L"㉧", L"㉨", L"㉩",
    L"㉪", L"㉫", L"㉬", L"㉭", L"㉮", L"㉯", L"㉰", L"㉱", L"㉲", L"㉳",
    L"㈀", L"㈁", L"㈂", L"㈃", L"㈄", L"㈅", L"㈆", L"㈇", L"㈈", L"㈉",
    L"⑴", L"⑵", L"⑶", L"⑷", L"⑸", L"⑹", L"⑺", L"⑻", L"⑼", L"⑽",
};
static const wchar_t *S_SIOT[] = {     // ㅅ: 그리스 문자
    L"Α", L"Β", L"Γ", L"Δ", L"Ε", L"Ζ", L"Η", L"Θ", L"Ι", L"Κ",
    L"Λ", L"Μ", L"Ν", L"Ξ", L"Ο", L"Π", L"Ρ", L"Σ", L"Τ", L"Υ",
    L"Φ", L"Χ", L"Ψ", L"Ω", L"α", L"β", L"γ", L"δ", L"ε", L"ζ",
    L"η", L"θ", L"ι", L"κ", L"λ", L"μ", L"ν", L"ξ", L"ο", L"π",
    L"ρ", L"σ", L"τ", L"υ", L"φ", L"χ", L"ψ", L"ω",
};
static const wchar_t *S_IEUNG[] = {    // ㅇ: 원문자 숫자·한글
    L"①", L"②", L"③", L"④", L"⑤", L"⑥", L"⑦", L"⑧", L"⑨", L"⑩",
    L"⑪", L"⑫", L"⑬", L"⑭", L"⑮", L"⑯", L"⑰", L"⑱", L"⑲", L"⑳",
    L"㉮", L"㉯", L"㉰", L"㉱", L"㉲", L"㉳", L"㉴", L"㉵", L"㉶", L"㉷",
    L"⓵", L"⓶", L"⓷", L"⓸", L"⓹", L"⓺", L"⓻", L"⓼", L"⓽", L"⓾",
};
static const wchar_t *S_JIEUT[] = {    // ㅈ: 로마 숫자
    L"ⅰ", L"ⅱ", L"ⅲ", L"ⅳ", L"ⅴ", L"ⅵ", L"ⅶ", L"ⅷ", L"ⅸ", L"ⅹ",
    L"Ⅰ", L"Ⅱ", L"Ⅲ", L"Ⅳ", L"Ⅴ", L"Ⅵ", L"Ⅶ", L"Ⅷ", L"Ⅸ", L"Ⅹ",
};
static const wchar_t *S_CHIEUT[] = {   // ㅊ: 분수/첨자
    L"½", L"⅓", L"⅔", L"¼", L"¾", L"⅛", L"⅜", L"⅝", L"⅞", L"¹",
    L"²", L"³", L"⁴", L"ⁿ", L"₁", L"₂", L"₃", L"₄", L"‰", L"℅",
};
static const wchar_t *S_KIEUK[] = {    // ㅋ: 한글 낱자
    L"ㄱ", L"ㄲ", L"ㄳ", L"ㄴ", L"ㄵ", L"ㄶ", L"ㄷ", L"ㄸ", L"ㄹ", L"ㄺ",
    L"ㄻ", L"ㄼ", L"ㄽ", L"ㄾ", L"ㄿ", L"ㅀ", L"ㅁ", L"ㅂ", L"ㅃ", L"ㅄ",
    L"ㅅ", L"ㅆ", L"ㅇ", L"ㅈ", L"ㅉ", L"ㅊ", L"ㅋ", L"ㅌ", L"ㅍ", L"ㅎ",
    L"ㅏ", L"ㅐ", L"ㅑ", L"ㅒ", L"ㅓ", L"ㅔ", L"ㅕ", L"ㅖ", L"ㅗ", L"ㅜ",
};
static const wchar_t *S_TIEUT[] = {    // ㅌ: 라틴 확장
    L"Æ", L"Ð", L"ª", L"Ħ", L"Ĳ", L"Ŀ", L"Ł", L"Ø", L"Œ", L"º",
    L"Þ", L"Ŧ", L"Ŋ", L"æ", L"đ", L"ð", L"ħ", L"ı", L"ĳ", L"ĸ",
    L"ŀ", L"ł", L"ø", L"œ", L"ß", L"þ", L"ŧ", L"ŋ", L"ŉ",
};
static const wchar_t *S_PIEUP[] = {    // ㅍ: 일본어 가나
    L"あ", L"い", L"う", L"え", L"お", L"か", L"き", L"く", L"け", L"こ",
    L"さ", L"し", L"す", L"せ", L"そ", L"た", L"ち", L"つ", L"て", L"と",
    L"ア", L"イ", L"ウ", L"エ", L"オ", L"カ", L"キ", L"ク", L"ケ", L"コ",
    L"サ", L"シ", L"ス", L"セ", L"ソ", L"タ", L"チ", L"ツ", L"テ", L"ト",
};
static const wchar_t *S_HIEUT[] = {    // ㅎ: 러시아 키릴
    L"А", L"Б", L"В", L"Г", L"Д", L"Е", L"Ж", L"З", L"И", L"Й",
    L"К", L"Л", L"М", L"Н", L"О", L"П", L"Р", L"С", L"Т", L"У",
    L"а", L"б", L"в", L"г", L"д", L"е", L"ж", L"з", L"и", L"й",
    L"к", L"л", L"м", L"н", L"о", L"п", L"р", L"с", L"т", L"у",
};

#define SET(a) do { arr = (wchar_t**)(a); n = (int)(sizeof(a)/sizeof((a)[0])); } while (0)

bool SpecialChar_Find(wchar_t consonant, wchar_t ***pppCandidates, int *pCount) {
    wchar_t **arr = NULL; int n = 0;
    switch (consonant) {
        case L'ㄱ': case L'ㄲ': SET(S_GIYEOK); break;
        case L'ㄴ':             SET(S_NIEUN);  break;
        case L'ㄷ': case L'ㄸ': SET(S_DIGEUT); break;
        case L'ㄹ':             SET(S_RIEUL);  break;
        case L'ㅁ':             SET(S_MIEUM);  break;
        case L'ㅂ': case L'ㅃ': SET(S_BIEUP);  break;
        case L'ㅅ': case L'ㅆ': SET(S_SIOT);   break;
        case L'ㅇ':             SET(S_IEUNG);  break;
        case L'ㅈ': case L'ㅉ': SET(S_JIEUT);  break;
        case L'ㅊ':             SET(S_CHIEUT); break;
        case L'ㅋ':             SET(S_KIEUK);  break;
        case L'ㅌ':             SET(S_TIEUT);  break;
        case L'ㅍ':             SET(S_PIEUP);  break;
        case L'ㅎ':             SET(S_HIEUT);  break;
        default: return false;
    }
    if (pppCandidates) *pppCandidates = arr;
    if (pCount) *pCount = n;
    return true;
}
