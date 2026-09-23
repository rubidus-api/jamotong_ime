#pragma once
#include <windows.h>
#include <stdbool.h>
#include "klay.h"   // KlayDiag (로드 진단)

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
    wchar_t text[256];     // CA_TEXT (2판은 23자까지, 3판은 줄 한도까지 — RFC-0016 §6.7)
    int vk;                // CA_KEY
    int keyExt;            // CA_KEY 확장키 여부 (화살표·오른쪽 모디파이어·키패드 Enter/÷ 등)
    int mod;               // CA_MOD_ONESHOT 비트마스크 / CA_KEY 시 함께 적용
    int targetLayer;       // CA_LAYER_*
    int p1, p2;            // 마우스 파라미터
} ChordEntry;

// 3판 조합 판정 정책 (RFC-0016 §7.1)
#define CHORD_HOLD_INTERRUPT 0   // 상위 조합 가능성이 없어진 뒤 다른 키가 눌리면 hold 확정 (1·2판 동작과 같다)
#define CHORD_HOLD_TIMEOUT   1   // HoldTermMs 가 지나야 hold

typedef struct ChordLayout {
    wchar_t name[64];
    int v3;                          // FormatVersion 3 — 3판 문법·판정 (1·2판은 기존 동작 유지, §7.1)
    int comboTermMs;                 // 3판: 첫 down 뒤 이 시간 안(경계 포함)이면 더 큰 조합을 기다린다
    int holdTermMs;                  // 3판: tap/hold 판정 시간
    int holdPolicy;                  // CHORD_HOLD_INTERRUPT / CHORD_HOLD_TIMEOUT
    int keyBit[128];                 // ASCII 산출문자 → 비트(0..31), -1 = 코드 글쇠 아님
    wchar_t layerNames[CL_MAX_LAYERS][32];
    int layerCount;
    int chordCount;                  // ★chords[] 앞에 둘 것 — 로더가 정확 크기(offsetof(chords)+n)로
                                     //   축소 할당하므로, 배열 뒤에 두면 잘려나간다(RFC-0011 P0)
    ChordEntry chords[CL_MAX_CHORDS];
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
    bool pendClosed;     // 3판: 형성 중 조합의 첫 글쇠가 떨어져 조합이 닫혔다 (새 글쇠는 다음 조합)
} ChordKbContext;

ChordLayout *ChordLayout_LoadFromFile(const wchar_t *path, KlayDiag *diag);
ChordLayout *ChordLayout_LoadFromLines(const KlayLines *L, KlayDiag *diag);   // Extends/Include 푼 줄 (RFC-0011 P4)
void ChordLayout_Free(ChordLayout *cl);

void ChordKb_Init(ChordKbContext *c);
// 시계 주입 (시험용 가짜 시계). NULL = 기본(GetTickCount). RFC-0016 §7.1: fake clock 으로 판정을 시험한다.
void ChordKb_SetClock(unsigned long (*now)(void));
// 3판: 키 이벤트 없이 시간만 흘러도 지속형 hold 가 켜진다 (§7.1). 호스트는 ChordKb_NextTickMs 가 돌려준
// 시간 뒤에 ChordKb_Tick 을 부른다. Tick 이 true 면 상태가 바뀌었다(레이어/모디파이어가 켜졌다).
// 1·2판과 hold 후보가 없는 상태에서는 NextTickMs 가 0 — 타이머가 필요 없는 파일에는 타이머를 만들지 않는다.
int  ChordKb_NextTickMs(const ChordKbContext *c, const ChordLayout *cl);
bool ChordKb_Tick(ChordKbContext *c, const ChordLayout *cl);
// 조합 경계(포커스·자판 전환)에서 호출: hold 로 눌러 둔 모디파이어에 **반드시** key-up 을 보내고
// 형성 중·hold·임시 레이어 상태를 비운다. 사용자가 고른 레이어(curLayer)는 유지. 여러 번 불러도 안전.
void ChordKb_ReleaseAll(ChordKbContext *c);
// KeyDown: 코드 글쇠면 비트 누적(eaten=true). KeyUp: 모두 떨어지면 조합 동작을 SendInput으로 수행.
// 반환값은 eaten 여부(핸들러가 소비 표시에 사용).
bool ChordKb_KeyDown(ChordKbContext *c, const ChordLayout *cl, UINT vk, wchar_t keyChar);
bool ChordKb_KeyUp(ChordKbContext *c, const ChordLayout *cl, UINT vk);

// 우리가 SendInput으로 넣은 합성 입력 표식 (재진입 방지: 키 핸들러가 GetMessageExtraInfo로 확인).
#define JAMO_SYNTH_MARK ((ULONG_PTR)0x4A414D4F)
