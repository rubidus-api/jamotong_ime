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
    CA_MOUSE_WHEEL,     // 휠: p1=수직량(+위/−아래), p2=수평량
    // ── 3판 포인터 (RFC-0016 §6.5) ──
    CA_PTR_MOVE,        // pointer move(dx,dy): Hold = 누르는 동안 연속 이동, Chord = 한 번 dx,dy 픽셀
    CA_PTR_BTN,         // pointer click/down/up/drag-toggle(btn): p1=버튼, p2=0 click 1 down 2 up 3 drag-toggle
    CA_PTR_WHEEL,       // pointer wheel(dx,dy): Hold = 연속 스크롤, Chord = 한 번 (노치 단위)
    CA_CANCEL,          // cancel actions: 연속 동작 정지·드래그 해제·대기 중 원샷 취소·매크로 취소
    CA_MACRO,           // macro <이름>: 유한한 동작열 실행 (p1 = 매크로 번호)
    // ── 3판 입력 자판 (RFC-0016 §6.3) — 반드시 끝에 붙인다(구운 자판이 번호를 싣는다) ──
    CA_SYMBOL           // symbol "...": OS 재주입 없이 **현재 엔진**으로 보내는 논리 입력.
                        //   입력 자판(`Type = input`)에서만 쓸 수 있고, 만들어진 symbol 은 다시
                        //   조합 인식기로 돌아가지 않는다(무한 재귀 없음).
} ChordActionType;

// ── 3판 매크로 (RFC-0016 §6.6): 이름 붙인 정적 동작열. 반복·조건·외부 실행은 없다 ──────────
#define CL_MAX_MACROS   8
#define CL_MAX_STEPS    128
#define CL_MACRO_TEXT   512
#define CL_MACRO_MAX_MS 3000    // 한 번 실행의 전체 시간 상한
#define MS_TEXT     0
#define MS_KEY      1
#define MS_PTR      2           // vk 자리에 CA_PTR_* 를 담는다
#define MS_WAIT     3           // p1 = ms
#define MS_WITH     4           // mod = 누를 수정키
#define MS_ENDWITH  5
typedef struct {
    unsigned char kind;
    int vk, mod, p1, p2, prof;
    unsigned short textOff, textLen;
} ChordMacroStep;
typedef struct { wchar_t name[32]; int first, count; } ChordMacro;

// 포인터 속도 프로필 (§6.5) — 가속(px/s^2)·상한(px/s), 휠은 노치/s
#define CPROF_SLOW   0
#define CPROF_NORMAL 1
#define CPROF_FAST   2
#define CPROF_SCROLL 3

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
    int p1, p2;            // 마우스 파라미터 (3판 포인터: move/wheel = dx,dy / btn = 버튼,동작)
    int prof;              // 3판 포인터 속도 프로필 (CPROF_*)
} ChordEntry;

// 3판 조합 판정 정책 (RFC-0016 §7.1)
#define CHORD_HOLD_INTERRUPT 0   // 상위 조합 가능성이 없어진 뒤 다른 키가 눌리면 hold 확정 (1·2판 동작과 같다)
#define CHORD_HOLD_TIMEOUT   1   // HoldTermMs 가 지나야 hold

typedef struct ChordLayout {
    wchar_t name[64];
    ChordMacro macros[CL_MAX_MACROS];
    ChordMacroStep steps[CL_MAX_STEPS];
    wchar_t macroText[CL_MACRO_TEXT];
    int macroCount, stepCount, macroTextLen;
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
    void (*symbolSink)(void *ctx, const wchar_t *sym);   // CA_SYMBOL 이 갈 곳 (RFC-0016 §6.3)
    void  *symbolCtx;
    void (*textSink)(void *ctx, const wchar_t *s);       // CA_TEXT·매크로 text 가 갈 곳 (B11 잔여)
    void  *textCtx;
    bool pendClosed;     // 3판: 형성 중 조합의 첫 글쇠가 떨어져 조합이 닫혔다 (새 글쇠는 다음 조합)
    // 3판 연속 포인터 (§6.5): 글쇠마다 제 몫을 기억해 두었다가 그 글쇠를 떼면 그만큼만 뺀다.
    signed char ptrKeyX[256], ptrKeyY[256];   // 이동 방향 (-1/0/1)
    signed char whKeyX[256], whKeyY[256];     // 휠 방향
    unsigned char ptrKeyProf[256], whKeyProf[256];
    int ptrX, ptrY, whX, whY;                 // 지금 활성 합 (반대 방향은 상쇄)
    int ptrProf, whProf;                      // 활성 프로필 (가장 빠른 쪽)
    unsigned long ptrStart, ptrLast, whStart, whLast;
    int ptrRemX, ptrRemY, whRemX, whRemY;     // 1/1000 픽셀·노치 잔량 (timer 지터·저속 끊김 방지)
    int dragBtn;                              // drag-toggle 로 누르고 있는 버튼 (-1 없음)
    // 3판 매크로 실행 상태 (§6.6): 한 번에 하나, 취소되면 자기가 누른 수정키만 놓는다
    int macroIdx, macroStep, macroMods;
    unsigned long macroResume, macroStart;
} ChordKbContext;

ChordLayout *ChordLayout_LoadFromFile(const wchar_t *path, KlayDiag *diag);
ChordLayout *ChordLayout_LoadFromLines(const KlayLines *L, KlayDiag *diag);   // Extends/Include 푼 줄 (RFC-0011 P4)
// 입력 자판(`Type = input`)의 앞단으로 읽는다 (RFC-0016 §6.3): `symbol` 을 허용하고 엔진 지시문은 지나친다.
ChordLayout *ChordLayout_LoadFromLinesEx(const KlayLines *L, KlayDiag *diag, bool forInput);
// 줄 목록에 조합 지시문(Key/Chord/Hold/Layer/Macro)이 하나라도 있는가 — 앞단을 둘지 정한다.
bool ChordLayout_LinesHaveChords(const KlayLines *L);
void ChordLayout_Free(ChordLayout *cl);

void ChordKb_Init(ChordKbContext *c);
// `symbol` 동작이 갈 곳 (RFC-0016 §6.3). 입력 자판에서 입력기가 여기에 엔진을 물린다.
//   싱크가 없으면 symbol 은 버려진다 — 엔진 없는 자판에서는 파서가 이미 막는다.
void ChordKb_SetSymbolSink(ChordKbContext *c, void (*sink)(void *ctx, const wchar_t *sym), void *ctx);
// `text` 가 갈 곳. 입력기는 여기에 **문서 편집 경로**(TSF 편집 세션 / EDIT 의 선택 치환)를 물린다 —
// 합성 유니코드 입력은 시스템 입력 큐를 거쳐 우리가 방금 넣은 글자와 순서가 엉킬 수 있다.
//   싱크가 없으면(관리 앱 시험칸, 매크로의 늦은 단계처럼 문맥이 없는 자리) 예전처럼 합성 입력으로
//   보낸다 — 터미널처럼 편집 세션이 안 통하는 곳도 그 길로 산다.
void ChordKb_SetTextSink(ChordKbContext *c, void (*sink)(void *ctx, const wchar_t *s), void *ctx);
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
