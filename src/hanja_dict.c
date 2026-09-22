#include "hanja_dict.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// ---------------------------------------------------------
// 아레나(Arena) 기반 단단한 메모리 관리
// 수만 개의 문자열을 개별 malloc하지 않고 통째로 할당 및 해제
// ---------------------------------------------------------
typedef struct ArenaNode {
    struct ArenaNode *next;
    size_t capacity;
    size_t used;
    uint8_t data[];
} ArenaNode;

typedef struct {
    ArenaNode *head;
} Arena;

static void *Arena_Alloc(Arena *arena, size_t size) {
    size = (size + 7) & ~7; // 8-byte align
    if (!arena->head || arena->head->used + size > arena->head->capacity) {
        size_t newCap = 1024 * 1024; // 기본 1MB 청크 할당
        if (size > newCap) newCap = size;
        ArenaNode *newNode = (ArenaNode*)malloc(sizeof(ArenaNode) + newCap);
        if (!newNode) return NULL; // OOM 방어
        newNode->capacity = newCap;
        newNode->used = 0;
        newNode->next = arena->head;
        arena->head = newNode;
    }
    void *ptr = arena->head->data + arena->head->used;
    arena->head->used += size;
    return ptr;
}

static void Arena_FreeAll(Arena *arena) {
    ArenaNode *curr = arena->head;
    while (curr) {
        ArenaNode *next = curr->next;
        free(curr);
        curr = next;
    }
    arena->head = NULL;
}

static Arena g_Arena = {NULL};

// ---------------------------------------------------------
// 사전 파일 → 와이드 문자열 (RFC-0008 W2-02). 결과는 arena 에 상주.
//   실패(NULL): 열 수 없음·빈 파일·상한 초과·덜 읽힘·잘못된 UTF-8·홀수 길이 UTF-16·글 가운데 NUL.
//   반쯤 읽은 사전으로 조용히 돌지 않는다 — 실패하면 사전 없이(한자 기능 꺼짐) 돈다.
// ---------------------------------------------------------
#ifndef JAMO_DICT_MAX_BYTES
#define JAMO_DICT_MAX_BYTES (8L * 1024 * 1024)   // 배포 사전은 0.2MB 미만
#endif
#ifndef MB_ERR_INVALID_CHARS
#define MB_ERR_INVALID_CHARS 0x08
#endif
static wchar_t *LoadDictText(const wchar_t *path, Arena *arena, int *pLen) {
    *pLen = 0;
    FILE *fp = _wfopen(path, L"rb");
    if (!fp) return NULL;
    long fsize = -1;
    if (fseek(fp, 0, SEEK_END) == 0) fsize = ftell(fp);
    if (fsize <= 0 || fsize > JAMO_DICT_MAX_BYTES || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    unsigned char *raw = (unsigned char*)malloc((size_t)fsize);
    if (!raw) { fclose(fp); return NULL; }
    size_t rd = fread(raw, 1, (size_t)fsize, fp);
    bool bad = (rd != (size_t)fsize) || ferror(fp);
    fclose(fp);
    wchar_t *w = NULL; int wlen = 0;
    if (!bad && rd >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {           // UTF-16 LE
        if ((rd - 2) % 2 == 0) {
            wlen = (int)((rd - 2) / 2);
            w = (wchar_t*)Arena_Alloc(arena, ((size_t)wlen + 1) * sizeof(wchar_t));
            if (w) for (int i = 0; i < wlen; i++) w[i] = (wchar_t)(raw[2 + 2*i] | (raw[3 + 2*i] << 8));
        }
    } else if (!bad) {                                                   // UTF-8 (BOM 허용)
        const char *t = (const char*)raw; int n = (int)rd;
        if (n >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) { t += 3; n -= 3; }
        wlen = n > 0 ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, t, n, NULL, 0) : 0;
        if (wlen > 0) {
            w = (wchar_t*)Arena_Alloc(arena, ((size_t)wlen + 1) * sizeof(wchar_t));
            if (w && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, t, n, w, wlen) != wlen) w = NULL;
        }
    }
    free(raw);
    if (!w || wlen <= 0) return NULL;
    w[wlen] = L'\0';
    if (wcslen(w) != (size_t)wlen) return NULL;                          // 가운데 NUL — 뒤를 잃는다
    *pLen = wlen;
    return w;
}
static HanjaEntry *g_HanjaDict = NULL;
static int g_HanjaCount = 0;

// 조회 = 정렬된 g_HanjaDict 전체에 대한 이진 탐색(항목 ~3천 → wcscmp ≤12회).
// (예전의 65536 항목 첫글자 인덱스는 BSS 512KB 를 모든 호스트 프로세스에 얹었다 — 제거.)

// 한자 → 대표 독음 역인덱스 (음절 항목에서 구축, 코드포인트 정렬 + 이진 탐색)
typedef struct { wchar_t hanja; wchar_t reading; } ReadingEntry;
static ReadingEntry *g_Reading = NULL;
static int g_ReadingCount = 0;
static int CompareReading(const void *a, const void *b) {
    return (int)((const ReadingEntry*)a)->hanja - (int)((const ReadingEntry*)b)->hanja;
}

// 이진 탐색을 위한 정렬 비교기
//   같은 한글 키면 파일에서 먼저 나온 줄이 앞(키는 모두 한 버퍼 안 — 포인터 순서 = 파일 순서).
static int CompareHanjaEntry(const void *a, const void *b) {
    const HanjaEntry *x = (const HanjaEntry*)a, *y = (const HanjaEntry*)b;
    int c = wcscmp(x->hangul, y->hangul);
    if (c) return c;
    return (x->hangul > y->hangul) - (x->hangul < y->hangul);
}

// ---------------------------------------------------------
// 로버스트 파서 (Robust In-place Parser)
// ---------------------------------------------------------
bool HanjaDict_Load(const wchar_t *filepath) {
    HanjaDict_Free();
    
    int wlen = 0;
    wchar_t *wbuf = LoadDictText(filepath, &g_Arena, &wlen);
    if (!wbuf) {
        HanjaDict_Free();
        return false;
    }
    
    // In-place 파싱 준비: 줄 수 세기
    int lineCount = 0;
    for (int i = 0; i < wlen; i++) {
        if (wbuf[i] == L'\n') lineCount++;
    }
    lineCount++; 
    
    g_HanjaDict = (HanjaEntry*)Arena_Alloc(&g_Arena, lineCount * sizeof(HanjaEntry));
    if (!g_HanjaDict) {
        HanjaDict_Free();
        return false;
    }
    
    // 텍스트 복사(Allocation) 없이 포인터와 Null-terminator만으로 쪼개기
    wchar_t *curr = wbuf;
    while (*curr) {
        // 공백 및 개행 무시
        while (*curr == L'\r' || *curr == L'\n' || *curr == L' ') curr++;
        if (!*curr) break;
        
        // 주석 무시
        if (*curr == L'#') {
            while (*curr && *curr != L'\n') curr++;
            continue;
        }
        
        wchar_t *lineStart = curr;
        while (*curr && *curr != L'\n' && *curr != L'\r') curr++;
        
        bool isEOF = (*curr == L'\0');
        *curr = L'\0'; // 줄 바꿈을 널 문자로 변경하여 라인 격리
        
        // "한글:한자,한자" 파싱
        wchar_t *colon = wcschr(lineStart, L':');
        if (colon) {
            *colon = L'\0'; // 콜론을 널 문자로 변경하여 한글 단어 격리
            wchar_t *hangul = lineStart;
            wchar_t *candsStr = colon + 1;
            
            // 콤마 수 세기
            int candsCount = 1;
            for (wchar_t *c = candsStr; *c; c++) {
                if (*c == L',') candsCount++;
            }
            
            // 1000개 이상의 기형적인 공격 방어
            if (candsCount > 1000) candsCount = 1000;
            
            wchar_t **candsArr = (wchar_t**)Arena_Alloc(&g_Arena, candsCount * sizeof(wchar_t*));
            if (candsArr) {
                int candIdx = 0;
                wchar_t *candStart = candsStr;
                while (candIdx < candsCount) {
                    wchar_t *comma = wcschr(candStart, L',');
                    if (comma) {
                        *comma = L'\0'; // 콤마를 널 문자로 변경하여 한자 단어 격리
                        candsArr[candIdx++] = candStart;
                        candStart = comma + 1;
                    } else {
                        if (*candStart) candsArr[candIdx++] = candStart;
                        break;
                    }
                }
                
                if (candIdx > 0) {
                    g_HanjaDict[g_HanjaCount].hangul = hangul;
                    g_HanjaDict[g_HanjaCount].candidates = candsArr;
                    g_HanjaDict[g_HanjaCount].candidateCount = candIdx;
                    g_HanjaCount++;
                }
            }
        }
        
        if (isEOF) break;
        curr++;
    }
    
    // 이진 탐색을 위해 정렬
    if (g_HanjaCount > 0) {
        qsort(g_HanjaDict, g_HanjaCount, sizeof(HanjaEntry), CompareHanjaEntry);
        // 중복 키: 먼저 나온 줄만 남긴다 (RFC-0008 W2-02 — 전엔 어느 줄이 찾아질지 정해지지 않았다)
        int w = 0;
        for (int i = 0; i < g_HanjaCount; i++)
            if (w == 0 || wcscmp(g_HanjaDict[w-1].hangul, g_HanjaDict[i].hangul) != 0) g_HanjaDict[w++] = g_HanjaDict[i];
        g_HanjaCount = w;
        
        // 한자 → 대표 독음 역인덱스: 음절 항목(hangul 1글자)에서 각 후보 한자에 그 음을 매핑.
        //   상한 = 전체 후보 수. 첫 매핑만 유지(다음자는 대표 음 하나). 후보창 음 폴백용.
        int cap = 0;
        for (int i = 0; i < g_HanjaCount; i++)
            if (g_HanjaDict[i].hangul[0] && !g_HanjaDict[i].hangul[1]) cap += g_HanjaDict[i].candidateCount;
        g_Reading = (ReadingEntry*)Arena_Alloc(&g_Arena, (size_t)(cap > 0 ? cap : 1) * sizeof(ReadingEntry));
        if (g_Reading) {
            for (int i = 0; i < g_HanjaCount; i++) {
                const HanjaEntry *e = &g_HanjaDict[i];
                if (!e->hangul[0] || e->hangul[1]) continue;   // 음절(1글자) 항목만
                for (int c = 0; c < e->candidateCount; c++) {
                    const wchar_t *cand = e->candidates[c];
                    if (cand && cand[0] && !cand[1])           // 단일 한자 후보만
                        g_Reading[g_ReadingCount++] = (ReadingEntry){ cand[0], e->hangul[0] };
                }
            }
            qsort(g_Reading, (size_t)g_ReadingCount, sizeof(ReadingEntry), CompareReading);
            // 중복 한자(다음자) 제거 — 정렬 후 첫 항목만 유지
            int w = 0;
            for (int r = 0; r < g_ReadingCount; r++)
                if (w == 0 || g_Reading[w-1].hanja != g_Reading[r].hanja) g_Reading[w++] = g_Reading[r];
            g_ReadingCount = w;
        }
    }

    return true;
}

wchar_t HanjaDict_ReadingOf(wchar_t hanja) {
    int lo = 0, hi = g_ReadingCount - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (g_Reading[mid].hanja == hanja) return g_Reading[mid].reading;
        if (g_Reading[mid].hanja < hanja) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

void HanjaDict_Free(void) {
    Arena_FreeAll(&g_Arena);
    g_HanjaDict = NULL;
    g_HanjaCount = 0;
    g_Reading = NULL;
    g_ReadingCount = 0;
}

bool HanjaDict_Find(const wchar_t *hangul, wchar_t ***pppCandidates, int *pCount) {
    if (!g_HanjaDict || g_HanjaCount == 0 || !hangul || !hangul[0]) return false;
    
    int left = 0;
    int right = g_HanjaCount - 1;
    
    while (left <= right) {
        int mid = left + (right - left) / 2;
        int cmp = wcscmp(g_HanjaDict[mid].hangul, hangul);
        
        if (cmp == 0) {
            if (pppCandidates) *pppCandidates = g_HanjaDict[mid].candidates;
            if (pCount) *pCount = g_HanjaDict[mid].candidateCount;
            return true;
        } else if (cmp < 0) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    
    return false;
}

// ---------------------------------------------------------
// 훈음(뜻·음) 표 — hanja_hunum.txt ("字:훈 음"). 후보창 표시용(RFC-0002/TODO#2).
//   자체 아레나 + (한자 코드포인트, 문자열) 정렬 배열 + 이진 탐색.
// ---------------------------------------------------------
typedef struct { wchar_t ch; const wchar_t *text; } HunumEntry;
static Arena g_HunumArena = {NULL};
static HunumEntry *g_Hunum = NULL;
static int g_HunumCount = 0;

static int CompareHunum(const void *a, const void *b) {
    const HunumEntry *x = (const HunumEntry*)a, *y = (const HunumEntry*)b;
    if (x->ch != y->ch) return (x->ch > y->ch) - (x->ch < y->ch);
    return (x->text > y->text) - (x->text < y->text);   // 같은 글자면 먼저 나온 줄이 앞
}

bool HunumDict_Load(const wchar_t *filepath) {
    HunumDict_Free();
    int wlen = 0;
    wchar_t *wbuf = LoadDictText(filepath, &g_HunumArena, &wlen);
    if (!wbuf) { HunumDict_Free(); return false; }

    int lineCount = 1;
    for (int i = 0; i < wlen; i++) if (wbuf[i] == L'\n') lineCount++;
    g_Hunum = (HunumEntry*)Arena_Alloc(&g_HunumArena, (size_t)lineCount * sizeof(HunumEntry));
    if (!g_Hunum) { HunumDict_Free(); return false; }

    wchar_t *curr = wbuf;                       // in-place 파싱 (hanja.txt 로더와 동일 기법)
    while (*curr) {
        while (*curr == L'\r' || *curr == L'\n' || *curr == L' ') curr++;
        if (!*curr) break;
        if (*curr == L'#') { while (*curr && *curr != L'\n') curr++; continue; }
        wchar_t *line = curr;
        while (*curr && *curr != L'\n' && *curr != L'\r') curr++;
        bool eof = (*curr == L'\0');
        *curr = L'\0';
        // "字:훈 음" — 키는 정확히 1문자
        if (line[0] && line[1] == L':' && line[2]) {
            size_t end = wcslen(line);
            while (end > 2 && (line[end-1] == L' ' || line[end-1] == L'\t')) line[--end] = L'\0';
            g_Hunum[g_HunumCount].ch = line[0];
            g_Hunum[g_HunumCount].text = line + 2;
            g_HunumCount++;
        }
        if (eof) break;
        curr++;
    }
    if (g_HunumCount > 0) {
        qsort(g_Hunum, (size_t)g_HunumCount, sizeof(HunumEntry), CompareHunum);
        int w = 0;                                  // 중복 글자: 먼저 나온 줄만 (W2-02)
        for (int i = 0; i < g_HunumCount; i++)
            if (w == 0 || g_Hunum[w-1].ch != g_Hunum[i].ch) g_Hunum[w++] = g_Hunum[i];
        g_HunumCount = w;
    }
    return g_HunumCount > 0;
}

void HunumDict_Free(void) {
    Arena_FreeAll(&g_HunumArena);
    g_Hunum = NULL; g_HunumCount = 0;
}

const wchar_t *HunumDict_Find(wchar_t hanja) {
    int lo = 0, hi = g_HunumCount - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (g_Hunum[mid].ch == hanja) return g_Hunum[mid].text;
        if (g_Hunum[mid].ch < hanja) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}
