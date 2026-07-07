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
static HanjaEntry *g_HanjaDict = NULL;
static int g_HanjaCount = 0;

// O(1) + O(log M) 초고속 인덱싱 테이블 (첫 글자 기준)
typedef struct {
    int startIdx;
    int count;
} CharIndex;
static CharIndex g_CharIndex[65536];

// 이진 탐색을 위한 정렬 비교기
static int CompareHanjaEntry(const void *a, const void *b) {
    return wcscmp(((HanjaEntry*)a)->hangul, ((HanjaEntry*)b)->hangul);
}

// ---------------------------------------------------------
// 로버스트 파서 (Robust In-place Parser)
// ---------------------------------------------------------
bool HanjaDict_Load(const wchar_t *filepath) {
    HanjaDict_Free();
    
    FILE *fp = _wfopen(filepath, L"rb");
    if (!fp) return false;
    
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (fsize <= 0) {
        fclose(fp);
        return false;
    }
    
    char *rawBuf = (char*)malloc(fsize + 1);
    if (!rawBuf) {
        fclose(fp);
        return false;
    }
    
    size_t readSize = fread(rawBuf, 1, fsize, fp);
    fclose(fp);
    rawBuf[readSize] = '\0';
    
    wchar_t *wbuf = NULL;
    int wlen = 0;
    
    // BOM 체크 및 디코딩
    if (fsize >= 2 && (unsigned char)rawBuf[0] == 0xFF && (unsigned char)rawBuf[1] == 0xFE) {
        // UTF-16 LE
        wlen = (fsize - 2) / 2;
        wbuf = (wchar_t*)Arena_Alloc(&g_Arena, (wlen + 1) * sizeof(wchar_t));
        if (wbuf) {
            memcpy(wbuf, rawBuf + 2, wlen * sizeof(wchar_t));
            wbuf[wlen] = L'\0';
        }
    } else {
        // UTF-8 (with or without BOM)
        char *textStart = rawBuf;
        int textSize = (int)readSize;
        if (textSize >= 3 && (unsigned char)rawBuf[0] == 0xEF && (unsigned char)rawBuf[1] == 0xBB && (unsigned char)rawBuf[2] == 0xBF) {
            textStart += 3;
            textSize -= 3;
        }
        wlen = MultiByteToWideChar(CP_UTF8, 0, textStart, textSize, NULL, 0);
        if (wlen > 0) {
            wbuf = (wchar_t*)Arena_Alloc(&g_Arena, (wlen + 1) * sizeof(wchar_t));
            if (wbuf) {
                MultiByteToWideChar(CP_UTF8, 0, textStart, textSize, wbuf, wlen);
                wbuf[wlen] = L'\0';
            }
        }
    }
    
    free(rawBuf); // 임시 버퍼 즉시 해제
    
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
        
        // 첫 글자 기반 인덱싱 테이블 구축
        for (int i = 0; i < 65536; i++) {
            g_CharIndex[i].startIdx = -1;
            g_CharIndex[i].count = 0;
        }
        
        for (int i = 0; i < g_HanjaCount; i++) {
            wchar_t firstChar = g_HanjaDict[i].hangul[0];
            if (firstChar) {
                unsigned short idx = (unsigned short)firstChar;
                if (g_CharIndex[idx].startIdx == -1) {
                    g_CharIndex[idx].startIdx = i;
                }
                g_CharIndex[idx].count++;
            }
        }
    }
    
    return true;
}

void HanjaDict_Free(void) {
    Arena_FreeAll(&g_Arena);
    g_HanjaDict = NULL;
    g_HanjaCount = 0;
}

bool HanjaDict_Find(const wchar_t *hangul, wchar_t ***pppCandidates, int *pCount) {
    if (!g_HanjaDict || g_HanjaCount == 0 || !hangul || !hangul[0]) return false;
    
    unsigned short firstChar = (unsigned short)hangul[0];
    int start = g_CharIndex[firstChar].startIdx;
    int count = g_CharIndex[firstChar].count;
    
    // 해당 글자로 시작하는 단어가 아예 없으면 즉시 종료 (O(1))
    if (start == -1 || count == 0) return false;
    
    // 범위를 대폭 좁혀서 이진 탐색
    int left = start;
    int right = start + count - 1;
    
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
    return (int)((const HunumEntry*)a)->ch - (int)((const HunumEntry*)b)->ch;
}

bool HunumDict_Load(const wchar_t *filepath) {
    HunumDict_Free();
    FILE *fp = _wfopen(filepath, L"rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END); long fsize = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (fsize <= 0) { fclose(fp); return false; }
    char *raw = (char*)malloc((size_t)fsize + 1);
    if (!raw) { fclose(fp); return false; }
    size_t rd = fread(raw, 1, (size_t)fsize, fp);
    fclose(fp); raw[rd] = '\0';

    // UTF-8 (BOM 허용) → 와이드 변환 (아레나에 상주)
    char *text = raw; int tlen = (int)rd;
    if (tlen >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) { text += 3; tlen -= 3; }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, tlen, NULL, 0);
    wchar_t *wbuf = (wlen > 0) ? (wchar_t*)Arena_Alloc(&g_HunumArena, ((size_t)wlen + 1) * sizeof(wchar_t)) : NULL;
    if (wbuf) { MultiByteToWideChar(CP_UTF8, 0, text, tlen, wbuf, wlen); wbuf[wlen] = L'\0'; }
    free(raw);
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
    if (g_HunumCount > 0) qsort(g_Hunum, (size_t)g_HunumCount, sizeof(HunumEntry), CompareHunum);
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
