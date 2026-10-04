// seq_parse.c — 순차 변환 자판 파일(.jmt)을 읽는다 (앞단 조합 포함). **도구 전용**:
//   입력기 DLL 에는 들어가지 않는다 — 입력기는 구운 자판만 읽는다 (RFC-0016 P5b-2).
//   런타임(엔진·사전 열기)은 seq_layout.c 에 있다.
// seq_layout.c — 순차 변환 자판 (.jmt 3판 `Type = input` + `Engine = sequence`, RFC-0016 §6.3).
//   표는 자판 파일 안에 없다. 컴파일해 둔 사전 파일(.jdb)을 매핑해 이진 탐색으로 찾는다
//   (오너 결정 2026-09-23: 자판과 사전을 나눈다 / 자료는 컴파일을 거친다).
#include "seq_layout.h"
#include "config.h"      // Config_IsSafeDictFileName / 사전 폴더
#include "chord_layout.h"   // 앞단 조합 인식기 (RFC-0016 §6.3)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "chord_layout.h"

// ── 로더 ───────────────────────────────────────────────────────────────────────────
static void TrimEnds(wchar_t *s) {
    size_t n = wcslen(s);
    while (n && (s[n-1] == L'\n' || s[n-1] == L'\r' || s[n-1] == L' ' || s[n-1] == L'\t')) s[--n] = L'\0';
}
static const wchar_t *const kSeqDirectives[] = { L"Dictionary", L"Candidates", L"ConvertKey",
                                                 L"Engine", L"OnUnmatched", L"Chinese", L"Japanese", L"Connection", L"Scheme", L"Tones", NULL };

// 변환 글쇠 이름 → VK (§6.4). 적은 것만 받는다 — 글자 글쇠는 읽기를 만드는 데 쓰이므로 안 된다.
static int ConvertKeyVk(const wchar_t *name) {
    if (!_wcsicmp(name, L"space")) return VK_SPACE;
    if (!_wcsicmp(name, L"tab")) return VK_TAB;
    if (!_wcsicmp(name, L"hanja")) return VK_HANJA;
    if (!_wcsicmp(name, L"convert")) return 0x1C;      /* VK_CONVERT (IME 변환 글쇠) */
    if (!_wcsicmp(name, L"f9")) return VK_F9;
    return 0;
}
SeqLayout *SeqLayout_LoadFromLines(const KlayLines *L, const wchar_t *layoutPath, KlayDiag *diag) {
    SeqLayout *sl = (SeqLayout *)calloc(1, sizeof(SeqLayout));
    if (!sl) return NULL;
    wcscpy(sl->name, L"sequence");
    sl->onUnmatched = SEQ_UNMATCHED_FLUSH;
    bool bad = false;
    #define FAIL(c, code, msg, help) do { KlayDiag_Add(diag, KLAY_SEV_ERROR, lineno, (c), code, msg, help); bad = true; } while (0)

    bool inMacro = false;   // 매크로 블록의 단계 줄은 앞단 조합이 읽는다
    wchar_t line[256];
    for (int li = 0; li < L->n; li++) {
        lstrcpynW(line, L->v[li].text, 256);
        const int lineno = L->v[li].line;
        if (diag) diag->curFile = L->files[L->v[li].file];
        TrimEnds(line);
        wchar_t *p = line;
        while (*p == L' ' || *p == L'\t') p++;
        if (*p == L'\0' || *p == L'#') continue;
        if (!wcsncmp(p, L"Macro ", 6)) inMacro = true;
        else if (!_wcsicmp(p, L"EndMacro")) { inMacro = false; continue; }
        if (inMacro) continue;
        const int col0 = (int)(p - line) + 1;

        if (swscanf(p, L"Name = %63l[^\n]", sl->name) == 1) { TrimEnds(sl->name); continue; }
        if (!wcsncmp(p, L"Sequence", 8)) {
            // 오너 결정 2026-09-23: 표는 자판 파일 안에 두지 않는다 — 조용히 무시하지 않고 막는다.
            FAIL(col0, L"E-JMT-SEQ-INLINE", L"a layout file cannot hold the table itself",
                 L"put the entries in a dictionary source (.jdt), build it with 'jamotong --build-dict' and write 'Dictionary = name.jdb'");
            continue;
        }
        if (!wcsncmp(p, L"Dictionary", 10)) {
            wchar_t file[64] = {0};
            if (swscanf(p, L"Dictionary = %63l[^\n]", file) != 1) {
                FAIL(col0, L"E-JMT-DICT", L"Dictionary needs a file name", L"Dictionary = romaji-kana.jdb");
                continue;
            }
            TrimEnds(file);
            if (sl->dictFile[0]) { FAIL(col0, L"E-JMT-DICT", L"this layout already has a dictionary", L"one Dictionary line per layout"); continue; }
            if (!Config_IsSafeDictFileName(file)) {
                FAIL(col0, L"E-JMT-DICT", L"the dictionary must be a plain file name ending in .jdb",
                     L"no folders in the name - the file lives beside the layout or in the dictionary folder");
                continue;
            }
            lstrcpynW(sl->dictFile, file, 64);
            continue;
        }
        if (!wcsncmp(p, L"Candidates", 10)) {
            wchar_t file[64] = {0};
            if (swscanf(p, L"Candidates = %63l[^\n]", file) != 1) {
                FAIL(col0, L"E-JMT-DICT", L"Candidates needs a file name", L"Candidates = words.jdb");
                continue;
            }
            TrimEnds(file);
            if (sl->candFile[0]) { FAIL(col0, L"E-JMT-DICT", L"this layout already has a candidate dictionary", NULL); continue; }
            if (!Config_IsSafeDictFileName(file)) {
                FAIL(col0, L"E-JMT-DICT", L"the candidate dictionary must be a plain file name ending in .jdb", NULL);
                continue;
            }
            lstrcpynW(sl->candFile, file, 64);
            continue;
        }
        if (!wcsncmp(p, L"ConvertKey", 10)) {
            wchar_t v[32] = {0};
            if (swscanf(p, L"ConvertKey = %31ls", v) != 1) {
                FAIL(col0, L"E-JMT-VALUE", L"ConvertKey needs a key name", L"ConvertKey = space | tab | hanja | convert | f9");
                continue;
            }
            sl->convertVk = ConvertKeyVk(v);
            if (!sl->convertVk)
                FAIL(col0, L"E-JMT-VALUE", L"this key cannot be the convert key",
                     L"ConvertKey = space | tab | hanja | convert | f9");
            continue;
        }
        if (!wcsncmp(p, L"OnUnmatched", 11)) {
            wchar_t v[32] = {0};
            if (swscanf(p, L"OnUnmatched = %31ls", v) != 1)
                FAIL(col0, L"E-JMT-VALUE", L"OnUnmatched needs a value", L"OnUnmatched = flush | cancel");
            else if (!_wcsicmp(v, L"flush"))  sl->onUnmatched = SEQ_UNMATCHED_FLUSH;
            else if (!_wcsicmp(v, L"cancel")) sl->onUnmatched = SEQ_UNMATCHED_CANCEL;
            else FAIL(col0, L"E-JMT-VALUE", L"OnUnmatched must be flush or cancel",
                      L"flush types the pending letters, cancel drops them");
            continue;
        }
        if (!wcsncmp(p, L"Chinese", 7)) {   // 중국어 병음 방식 (2026-10-03): 치는 동안 후보, 끊기, 문장부호, 날짜…
            wchar_t v[32] = {0};
            if (swscanf(p, L"Chinese = %31ls", v) != 1)
                FAIL(col0, L"E-JMT-VALUE", L"Chinese needs a value", L"Chinese = simplified | traditional");
            else if (!_wcsicmp(v, L"simplified"))  sl->zh = SEQ_ZH_SIMPLIFIED;
            else if (!_wcsicmp(v, L"traditional")) sl->zh = SEQ_ZH_TRADITIONAL;
            else FAIL(col0, L"E-JMT-VALUE", L"Chinese must be simplified or traditional",
                      L"it picks the punctuation and date forms; the dictionary decides the characters");
            continue;
        }
        if (!wcsncmp(p, L"Japanese", 8)) {   // 일본어 방식 (0.70.0, RFC-0021): 문구 치환, 가나 꼴, F6~F10, 엔터 확정
            wchar_t v[32] = {0};
            if (swscanf(p, L"Japanese = %31ls", v) != 1 || _wcsicmp(v, L"yes"))
                FAIL(col0, L"E-JMT-VALUE", L"Japanese takes yes", L"Japanese = yes");
            else sl->ja = 1;
            continue;
        }
        if (!wcsncmp(p, L"Connection", 10)) {   // 연결 비용 표 (RFC-0022): Connection = 표.jdc — 일본어 방식의 래티스
            wchar_t file[64] = {0};
            size_t fl = 0;
            if (swscanf(p, L"Connection = %63ls", file) != 1 || (fl = wcslen(file)) < 5 || _wcsicmp(file + fl - 4, L".jdc"))
                FAIL(col0, L"E-JMT-DICT", L"Connection needs a plain .jdc file name", L"Connection = japanese-conn.jdc");
            else lstrcpynW(sl->connFile, file, 64);
            continue;
        }
        if (!wcsncmp(p, L"Tones", 5)) {   // 성조 병음 사전 (0.66.0): Tones = 표.jdb
            wchar_t file[64] = {0};
            if (swscanf(p, L"Tones = %63ls", file) != 1 || !Config_IsSafeDictFileName(file)) {
                FAIL(col0, L"E-JMT-DICT", L"Tones needs a plain .jdb file name", L"Tones = chinese-simplified-tones.jdb");
                continue;
            }
            lstrcpynW(sl->toneFile, file, 64);
            continue;
        }
        if (!wcsncmp(p, L"Scheme", 6)) {   // 쌍병 글쇠 표 (2026-10-03): Scheme = 이름 파일.jdb — 자판 선택(Keys)이 이름으로 고른다
            wchar_t nm[16] = {0}, file[64] = {0};
            if (swscanf(p, L"Scheme = %15ls %63ls", nm, file) != 2) {
                FAIL(col0, L"E-JMT-VALUE", L"Scheme needs a name and a key table", L"Scheme = xiaohe chinese-simplified-xiaohe.jdb");
                continue;
            }
            bool okName = true;
            for (const wchar_t *q = nm; *q; q++) if (!(*q >= L'a' && *q <= L'z')) okName = false;
            if (!okName) { FAIL(col0, L"E-JMT-VALUE", L"a scheme name is lowercase letters", L"xiaohe, ziranma, microsoft"); continue; }
            if (sl->nScheme >= SEQ_MAX_SCHEMES) { FAIL(col0, L"E-JMT-VALUE", L"too many schemes", L"up to 4 Scheme lines"); continue; }
            if (!Config_IsSafeDictFileName(file)) {
                FAIL(col0, L"E-JMT-DICT", L"the key table must be a plain file name ending in .jdb", NULL);
                continue;
            }
            for (int i = 0; i < sl->nScheme; i++)
                if (!wcscmp(sl->schemeName[i], nm)) { FAIL(col0, L"E-JMT-VALUE", L"this scheme name is used twice", NULL); okName = false; }
            if (!okName) continue;
            lstrcpynW(sl->schemeName[sl->nScheme], nm, 16);
            lstrcpynW(sl->schemeFile[sl->nScheme], file, 64);
            sl->nScheme++;
            continue;
        }
        if (!wcsncmp(p, L"Engine", 6)) continue;        // 통합 로더가 이미 읽었다
        if (!wcsncmp(p, L"Key ", 4) || !wcsncmp(p, L"Chord ", 6) || !wcsncmp(p, L"Hold ", 5)
            || !wcsncmp(p, L"Layer ", 6) || !wcsncmp(p, L"Macro ", 6) || !_wcsicmp(p, L"EndMacro")
            || !wcsncmp(p, L"ComboTermMs", 11) || !wcsncmp(p, L"HoldTermMs", 10)
            || !wcsncmp(p, L"HoldPolicy", 10)) continue;   // 앞단 조합이 읽는 줄 (§6.3)
        if (KlayHeader_IsKnownKey(p)) continue;
        if (Klay_UnknownLine(diag, p, lineno, col0, kSeqDirectives)) bad = true;
    }
    #undef FAIL
    if (diag) diag->curFile = NULL;

    if (!bad && !sl->dictFile[0]) {
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-DICT",
                     L"a sequence layout must say which dictionary it uses", L"add 'Dictionary = name.jdb'");
        bad = true;
    }

    if (!bad && ChordLayout_LinesHaveChords(L)) {   // §6.3: 앞단 조합 인식기가 있는 자판
        ChordLayout *cl = ChordLayout_LoadFromLinesEx(L, diag, true);
        if (cl) sl->chord = cl;
        else bad = true;
    }
    if (!bad && (sl->candFile[0] != 0) != (sl->convertVk != 0)) {
        // 후보 사전과 변환 글쇠는 함께 있어야 한다 — 하나만 있으면 무엇을 뜻하는지 알 수 없다
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-VALUE",
                     sl->candFile[0] ? L"a candidate dictionary needs a convert key"
                                     : L"a convert key needs a candidate dictionary",
                     L"write both 'Candidates = words.jdb' and 'ConvertKey = space'");
        bad = true;
    }
    if (!bad && sl->zh && sl->ja) {
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-VALUE", L"a layout is Chinese or Japanese, not both",
                     L"keep one of the two lines");
        bad = true;
    }
    if (!bad && sl->connFile[0] && !sl->ja) {
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-VALUE", L"Connection is for the Japanese style", L"add 'Japanese = yes' or remove the line");
        bad = true;
    }
    if (!bad && sl->ja && !sl->candFile[0]) {
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-VALUE", L"the Japanese style needs a candidate dictionary",
                     L"add 'Candidates = words.jdb' and 'ConvertKey = space'");
        bad = true;
    }
    if (!bad && sl->zh && !sl->candFile[0]) {
        KlayDiag_Add(diag, KLAY_SEV_ERROR, 0, 1, L"E-JMT-VALUE", L"the Chinese style needs a candidate dictionary",
                     L"add 'Candidates = words.jdb' and 'ConvertKey = space'");
        bad = true;
    }
    if (!bad && !SeqLayout_OpenDict(sl, layoutPath, diag)) bad = true;

    if (bad) { SeqLayout_Free(sl); return NULL; }
    return sl;
}
SeqLayout *SeqLayout_LoadFromFile(const wchar_t *path, KlayDiag *diag) {
    KlayLines L;
    bool built = KlayLines_Build(&L, path, diag);
    SeqLayout *sl = built ? SeqLayout_LoadFromLines(&L, path, diag) : NULL;
    KlayLines_Free(&L);
    return sl;
}
