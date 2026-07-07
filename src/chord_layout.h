#pragma once
#include <windows.h>
#include <stdbool.h>

// 일반 코드(조합) 자판 — ARTSEY.io 처럼 글쇠 조합(chord)이 출력/동작에 대응한다. 눌린 글쇠가
// 모두 떨어질 때, 현재 레이어에서 그 조합의 동작을 수행한다. 동작은 SendInput 으로 실제 키/마우스
// 이벤트를 낸다(텍스트·특수키·모디파이어·레이어전환·마우스). 이 파일에는 특정 자판 데이터를 담지 않는다.

#define CL_MAX_CHORDS 2048
#define CL_MAX_LAYERS 16

// 모디파이어 비트 (좌/우 구분)
#define CMOD_LSHIFT 0x01
#define CMOD_RSHIFT 0x02
#define CMOD_LCTRL  0x04
#define CMOD_RCTRL  0x08
#define CMOD_LALT   0x10
#define CMOD_RALT   0x20
#define CMOD_LGUI   0x40
#define CMOD_RGUI   0x80

typedef enum {
    CA_TEXT = 0,        // 유니코드 문자열 입력
    CA_KEY,             // 가상키(vk) 전송 (pending 모디파이어 적용)
    CA_MOD_ONESHOT,     // 원샷 모디파이어 설정 (다음 출력에 적용)
    CA_LAYER_ONESHOT,   // 원샷 레이어 (다음 조합만 해당 레이어)
    CA_LAYER_TOGGLE,    // 레이어 토글 (해당 레이어 ↔ base)
    CA_LAYER_SWITCH,    // 레이어 전환 (계속 유지)
    CA_MOUSE_MOVE,      // 마우스 이동 (p1=dx, p2=dy)
    CA_MOUSE_BTN,       // 버튼: p1=버튼(0 L,1 R,2 M), p2=동작(0 click,1 down,2 up)
    CA_MOUSE_WHEEL      // 휠: p1=수직량(+위/−아래), p2=수평량
} ChordActionType;

typedef struct {
    unsigned mask;         // 눌린 글쇠 비트마스크
    int layer;             // 이 조합이 속한 레이어
    int isHold;            // 1이면 이 조합의 hold 동작 (Hold 지시문). 0이면 tap 동작 (Chord)
    ChordActionType act;
    wchar_t text[24];      // CA_TEXT
    int vk;                // CA_KEY
    int keyExt;            // CA_KEY 확장키 여부 (화살표·오른쪽 모디파이어·키패드 Enter/÷ 등)
    int mod;               // CA_MOD_ONESHOT 비트마스크 / CA_KEY 시 함께 적용
    int targetLayer;       // CA_LAYER_*
    int p1, p2;            // 마우스 파라미터
} ChordEntry;

typedef struct ChordLayout {
    wchar_t name[64];
    int keyBit[128];                 // ASCII 산출문자 → 비트(0..31), -1 = 코드 글쇠 아님
    wchar_t layerNames[CL_MAX_LAYERS][32];
    int layerCount;
    ChordEntry chords[CL_MAX_CHORDS];
    int chordCount;
} ChordLayout;

// 런타임 상태
typedef struct {
    bool keyDown[256];   // 우리가 추적 중인 눌린 글쇠 (조합 형성 + 지속 hold)
    char role[256];      // 1=조합 형성 중, 2=지속 hold(임시 레이어/모디파이어 유지)
    unsigned pendMask;   // 현재 형성 중 조합
    int pendKeys;        // 형성 중 글쇠 수
    unsigned long pendTick; // 형성 시작 시각 (tap/hold 판정)
    int holdKeys;        // 지속 hold로 눌려 있는 글쇠 수
    int momentaryLayer;  // 지속 hold로 활성화된 임시 레이어 (-1 없음)
    int heldMod;         // 지속 hold로 누르고 있는 모디파이어 (0 없음)
    int curLayer;        // 현재 레이어 (0 = base)
    int oneshotLayer;    // -1 없음, 아니면 다음 조합에만 적용할 레이어
    int oneshotMod;      // 대기 중 원샷 모디파이어 비트마스크
} ChordKbContext;

ChordLayout *ChordLayout_LoadFromFile(const wchar_t *path);
void ChordLayout_Free(ChordLayout *cl);

void ChordKb_Init(ChordKbContext *c);
// KeyDown: 코드 글쇠면 비트 누적(eaten=true). KeyUp: 모두 떨어지면 조합 동작을 SendInput으로 수행.
// 반환값은 eaten 여부(핸들러가 소비 표시에 사용).
bool ChordKb_KeyDown(ChordKbContext *c, const ChordLayout *cl, UINT vk, wchar_t keyChar);
bool ChordKb_KeyUp(ChordKbContext *c, const ChordLayout *cl, UINT vk);

// 우리가 SendInput으로 넣은 합성 입력 표식 (재진입 방지: 키 핸들러가 GetMessageExtraInfo로 확인).
#define JAMO_SYNTH_MARK ((ULONG_PTR)0x4A414D4F)
