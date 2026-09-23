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
                                                 L"Engine", L"OnUnmatched", NULL };

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
