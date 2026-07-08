#include "jamotong.h"
#include "langbar.h"
#include "hanja_dict.h"
#include "candidate_ui.h"
#include "special_char.h"
#include "layout.h"
#include "hangul_layout.h"
#include "edit_session.h"
#include "settings_ui.h"
#include "preedit_overlay.h"
#include "code_input.h"
extern HINSTANCE g_hInst;   // dllmain.c — DLL 모듈 핸들(사전 경로·윈도 클래스 등록용)

// 현재 자판 상태 발행 — 트레이 툴(jamotong.exe)이 폴링. 호출자가 g_configLock 보유 여부 무관(재진입).
void Jamotong_PublishStatus(JamotongConfig *config) {
    EnterCriticalSection(&g_configLock);
    LayoutConfig *layout = Config_GetCurrentLayout(config);
    wchar_t ab[8] = L"?", nm[64] = L"?";
    if (layout) {
        if (layout->abbrev[0]) { wcsncpy(ab, layout->abbrev, 7); ab[7] = L'\0'; }
        if (layout->name)      { wcsncpy(nm, layout->name, 63); nm[63] = L'\0'; }
    }
    LeaveCriticalSection(&g_configLock);
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Jamotong", 0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hk, L"CurrentAbbrev", 0, REG_SZ, (const BYTE*)ab, (DWORD)((wcslen(ab)+1)*sizeof(wchar_t)));
        RegSetValueExW(hk, L"CurrentName",   0, REG_SZ, (const BYTE*)nm, (DWORD)((wcslen(nm)+1)*sizeof(wchar_t)));
        RegCloseKey(hk);
    }
}

// 캐럿 화면 rect 획득 폴백 체인 (RFC-0002 §3.1):
//   1) 방금 편집 세션에서 GetTextExt로 캡처한 값(svc->lastCaretRect) — TSF 정석.
//   2) GetGUIThreadInfo의 시스템 캐럿(rcCaret+hwndCaret) — 옛 EDIT(AkelPad)·PuTTY에서 정확.
//   둘 다 실패 → FALSE (미리보기 생략).
static BOOL GetCaretScreenRect(JamotongTextService *obj, RECT *out) {
    if (obj->lastCaretValid) { *out = obj->lastCaretRect; return TRUE; }
    GUITHREADINFO gti; memset(&gti, 0, sizeof(gti)); gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(0, &gti) && gti.hwndCaret &&
        (gti.rcCaret.bottom - gti.rcCaret.top) > 0) {
        POINT tl = { gti.rcCaret.left, gti.rcCaret.top };
        POINT br = { gti.rcCaret.right, gti.rcCaret.bottom };
        if (ClientToScreen(gti.hwndCaret, &tl) && ClientToScreen(gti.hwndCaret, &br)) {
            out->left = tl.x; out->top = tl.y; out->right = br.x; out->bottom = br.y;
            return TRUE;
        }
    }
    return FALSE;
}

// FSM 결과 출력 → 커밋 전용 편집세션(확정 음절만 삽입) + 조합 미리보기 오버레이 갱신(RFC-0002).
//   g_configLock 재진입: OnKeyDown(락 보유)에서도, KeyUp(무락)에서도 안전.
static void OutputResult(JamotongTextService *obj, ITfContext *pic, FsmResult res, BOOL isFlush) {
    (void)isFlush;
    RequestEditSession(obj, pic, res);   // 삽입 + 세션 내 캐럿 rect 캡처

    EnterCriticalSection(&g_configLock);
    bool show = obj->config.options.showPreview && res.preeditChar;
    wchar_t face[32];
    wcsncpy(face, obj->config.options.previewFont, 31); face[31] = L'\0';
    int pvSize = obj->config.options.previewFontSize;
    LeaveCriticalSection(&g_configLock);

    if (show) {
        RECT rc;
        if (GetCaretScreenRect(obj, &rc)) {
            // CUAS 낡은 좌표 보정(누적형): CUAS 앱은 커밋 삽입이 비동기라 rect가 늦게 전진한다.
            // rect가 직전 원시값과 같으면 "정체" — 그 사이 발생한 커밋 수만큼 전각 폭을 누적 보정.
            // rect가 실제로 움직이면 누적을 리셋. 네이티브 앱은 즉시 전진하므로 보정 미발동.
            RECT raw = rc;
            if (obj->prevChipValid &&
                raw.left == obj->prevChipRect.left && raw.top == obj->prevChipRect.top) {
                if (res.commitChar) obj->chipPendingAdv += (raw.bottom - raw.top);
            } else {
                obj->chipPendingAdv = 0;
            }
            rc.left += obj->chipPendingAdv; rc.right += obj->chipPendingAdv;
            obj->prevChipRect = raw;   // 비교는 항상 '원시' 좌표 기준
            obj->prevChipValid = TRUE;
            JamoDiag("CHIP raw=(%ld,%ld-%ld,%ld) adv=%d commit=%d src=%s",
                     raw.left, raw.top, raw.right, raw.bottom, obj->chipPendingAdv,
                     res.commitChar ? 1 : 0, obj->lastCaretValid ? "TextExt" : "GUITI");
            wchar_t s[2] = { res.preeditChar, L'\0' };
            PreeditOverlay_Show(&rc, s, face, pvSize);
            return;
        }
        JamoDiag("CHIP no-rect (TextExt fail + GUIThreadInfo fail) -> hide");
    }
    obj->prevChipValid = FALSE;   // 표시 안 함 → 비교 기준 리셋
    obj->chipPendingAdv = 0;
    PreeditOverlay_Hide();   // preedit 없음/옵션 꺼짐/좌표 불명 → 숨김
}

// 조합 상태 전면 리셋 — FSM·모아치기(chord)·미리보기 칩 상태를 '한 곳에서' 비운다.
// (프리뷰 숨김/칩 상태 리셋이 여러 경로에 흩어져 있어, 조합이 깨졌을 때 칩이 갇히거나
// 상태가 어긋나던 문제의 단일 진입점 — 실기 2026-07-08. 표시 갱신은 OutputResult가 유일한
// 표시 경로이고, 리셋은 이 함수가 유일한 정리 경로다.)
static void ResetComposition(JamotongTextService *obj) {
    Fsm_Init(&obj->fsm);
    Chord_Init(&obj->chord);
    obj->lastCaretValid = FALSE;
    obj->prevChipValid = FALSE;
    obj->chipPendingAdv = 0;
    PreeditOverlay_Hide();
}

// 유니코드 직접 입력용 16진수 헬퍼
static inline bool IsHexW(wchar_t c) {
    return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'F') || (c >= L'a' && c <= L'f');
}
static inline unsigned HexValW(wchar_t c) {
    if (c >= L'0' && c <= L'9') return (unsigned)(c - L'0');
    if (c >= L'A' && c <= L'F') return (unsigned)(c - L'A' + 10);
    return (unsigned)(c - L'a' + 10);
}

// Hanja UI Context
typedef struct {
    JamotongTextService *obj;
    ITfContext *pic;
    wchar_t word[32];    // 단어 변환 대상 원문 (EDIT 선택 검증용 — 실기 2026-07-08)
    bool fromSelection;  // 블록 선택(이미 선택돼 있음)에서 온 변환 — EM_REPLACESEL로 교체
} CandidateContext;
static CandidateContext g_CandCtx;

static void OnHanjaSelected(int index, const wchar_t *str, void *ctx) {
    (void)index;   // 콜백 시그니처상 받지만 실제 치환은 str로만 함
    CandidateContext *cc = (CandidateContext*)ctx;
    int replaceLen = CandidateUI_GetReplaceLen();
    EditSessionData esd = {0};
    wcsncpy(esd.committed, str, 127); esd.committed[127] = L'\0';

    if (cc->fromSelection) {
        // 블록 선택 변환: 선택이 그대로 유지돼 있으므로(후보창=NOACTIVATE) EDIT 계열은
        // EM_REPLACESEL로 선택 전체를 정확히 교체. TSF InsertTextAtSelection은 CUAS에서
        // 선택을 앞 글자만 부분 교체했다("대한민국"→"大韓민국", 실기 2026-07-08). 비-EDIT는 삽입.
        if (!EditCtl_ReplaceSelection(str))
            RequestEditSessionData(cc->obj, cc->pic, &esd);
    } else if (replaceLen > 0) {
        // 커서 앞 단어 변환: EDIT 계열이면 단어를 선택(읽기 검증)한 뒤 EM_REPLACESEL 교체.
        if (cc->word[0] && EditCtl_SelectWordBeforeCaret(cc->word) && EditCtl_ReplaceSelection(str)) {
            /* 교체 완료 */
        } else {
            RequestReplaceSessionString(cc->obj, cc->pic, replaceLen, str);   // 비-EDIT 네이티브
        }
    } else {
        // 커밋전용: 조합중 음절은 문서에 없음 → 선택 한자를 그냥 삽입(모든 앱 동작).
        RequestEditSessionData(cc->obj, cc->pic, &esd);
    }
    ResetComposition(cc->obj);   // 조합 음절이 한자로 확정됨 → 조합·칩 상태 전면 리셋
    if (cc->pic) { cc->pic->lpVtbl->Release(cc->pic); cc->pic = NULL; }   // 저장 시 AddRef한 것 해제
}

static void OnHanjaCancelled(void *ctx) {
    CandidateContext *cc = (CandidateContext*)ctx;
    if (cc && cc->pic) { cc->pic->lpVtbl->Release(cc->pic); cc->pic = NULL; }
}

// 104-key US QWERTY 기준으로 가상 키와 Shift 조합을 통해 영문 Base Char를 가져옵니다.
static wchar_t GetQwertyChar(WPARAM vk, bool shift) {
    return Layout_QwertyChar((unsigned)vk, shift ? 1 : 0);   // 공유 구현(layout.c) — TSF/IMM 일관
}

// Ctrl/Alt/Win 이 눌려 있는가 — 이 경우 키 입력은 애플리케이션 단축키(Ctrl+C 등)이므로
// IME가 소비하지 않고 그대로 통과시켜야 한다. (Shift는 대문자/된소리라 텍스트 입력에 필요 → 제외)
static bool HasCtrlAltWin(void) {
    return (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000) ||
           (GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000);
}

// 한자/훈음 사전 lazy-load: 첫 한자 요청 시 1회만 시도(실패도 캐시 — 반복 IO 방지).
static void EnsureHanjaDicts(void) {
    static bool s_tried = false;
    if (s_tried) return;
    s_tried = true;
    wchar_t dictPath[MAX_PATH];
    if (GetModuleFileNameW(g_hInst, dictPath, MAX_PATH)) {
        wchar_t *pSlash = wcsrchr(dictPath, L'\\');
        if (pSlash) {
            wcscpy(pSlash + 1, L"hanja.txt");
            HanjaDict_Load(dictPath);
            wcscpy(pSlash + 1, L"hanja_hunum.txt");   // 훈음(뜻·음) 표 — 후보창 표시용
            HunumDict_Load(dictPath);
        }
    }
}

// 조합 확정 후, 확정을 유발한 '비자모' 키를 실제 키 이벤트로 다시 보낸다(JAMO_SYNTH_MARK 표식).
// 텍스트 삽입(편집세션)은 메모장류엔 되지만 PuTTY 같은 터미널엔 안 통함 → 실제 키를 재전달해야
// 앱이 네이티브로 처리(스페이스·엔터·방향키·터미널 등). 재전달된 키는 OnKeyDown 진입부 가드로 통과.
static void SendKeyThrough(WPARAM vk, LPARAM lParam) {
    UINT sc = (UINT)((lParam >> 16) & 0xFF);
    BOOL ext = (BOOL)((lParam >> 24) & 1);
    INPUT in[2]; memset(in, 0, sizeof(in));
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = (WORD)vk; in[0].ki.wScan = (WORD)sc;
    in[0].ki.dwFlags = ext ? KEYEVENTF_EXTENDEDKEY : 0;
    in[0].ki.dwExtraInfo = JAMO_SYNTH_MARK;
    in[1] = in[0]; in[1].ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

// ── 경계키 '지연' 재전달 (실기 발견 2026-07-08: AkelPad 엔터가 마지막 음절을 소실) ─────────
// CUAS 앱은 확정 음절의 문서 전달이 비동기라, 즉시 SendInput한 합성 경계키가 그 전달을
// 추월해 음절이 사라진다(스페이스는 v0.11.0에서 단일삽입으로 해결; 엔터/방향키는 제어키라
// 삽입 불가 → 재전달 유지가 불가피). 재전달을 ~30ms 늦춰 삽입이 앱에 닿을 시간을 준다.
// 새 경계키가 그 안에 또 오면 보류분을 먼저 즉시 방출해 순서를 지킨다. (입력 스레드 전용)
#define RESEND_DELAY_MS 30
// 실제 재전달 실행. EDIT 계열(포커스 창이 EM_GETSEL에 정상 응답)이면 SendInput 대신
// **그 창의 메시지 큐에 WM_KEYDOWN/UP을 직접 게시** — 같은 큐에 뒤이어 서므로 CUAS의
// 확정문자 전달 메시지를 추월할 수 없다(순서 보장). SendInput은 시스템 입력 큐 경유라
// 앱 큐와 순서가 안 맞을 수 있음(실기 2026-07-08: 30ms 지연으로도 AkelPad 엔터가 마지막
// 음절을 소실). 비-EDIT(터미널 등)은 종전 SendInput 유지(PuTTY 검증됨).
static void ResendKeyNow(WPARAM vk, LPARAM lParam) {
    GUITHREADINFO gti; memset(&gti, 0, sizeof(gti)); gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(0, &gti) && gti.hwndFocus) {
        DWORD s = 0xFFFFFFFF, e = 0xFFFFFFFF;
        SendMessageW(gti.hwndFocus, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
        if (s != 0xFFFFFFFF && e != 0xFFFFFFFF && s <= e) {   // EDIT 계열로 판정
            LPARAM base = lParam & 0x01FF0000;   // 스캔코드·확장키 비트 유지
            PostMessageW(gti.hwndFocus, WM_KEYDOWN, vk, base | 1);
            PostMessageW(gti.hwndFocus, WM_KEYUP,   vk, base | 0xC0000001);
            JamoDiag("RESEND vk=%02X via PostMessage", (unsigned)vk);
            return;
        }
    }
    JamoDiag("RESEND vk=%02X via SendInput", (unsigned)vk);
    SendKeyThrough(vk, lParam);
}
static WPARAM  g_pendResendVk = 0;
static LPARAM  g_pendResendLp = 0;
static UINT_PTR g_pendResendTimer = 0;
static void CALLBACK ResendTimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
    (void)hwnd; (void)msg; (void)time;
    KillTimer(NULL, id);
    if (id == g_pendResendTimer) {
        g_pendResendTimer = 0;
        ResendKeyNow(g_pendResendVk, g_pendResendLp);
    }
}
static void ScheduleKeyResend(WPARAM vk, LPARAM lParam) {
    if (g_pendResendTimer) {   // 이전 보류분은 즉시 방출(순서 유지) 후 새 키를 보류
        KillTimer(NULL, g_pendResendTimer);
        g_pendResendTimer = 0;
        ResendKeyNow(g_pendResendVk, g_pendResendLp);
    }
    g_pendResendVk = vk; g_pendResendLp = lParam;
    g_pendResendTimer = SetTimer(NULL, 0, RESEND_DELAY_MS, ResendTimerProc);
    if (!g_pendResendTimer) ResendKeyNow(vk, lParam);   // 타이머 실패 시 즉시 재전달
}

// ASCII → 전각(full-width). 전각 모드일 때 라틴/숫자/기호를 전각 폭 문자로 변환.
static wchar_t ToFullWidth(wchar_t c) {
    if (c == L' ') return 0x3000;                         // 전각 공백
    if (c >= 0x21 && c <= 0x7E) return (wchar_t)(c - 0x21 + 0xFF01);   // ! ~ → ！ ～
    return c;
}

// 모디파이어/락/변환 키인가 — 조합 중 이런 키를 눌러도 조합을 확정하지 않는다(Shift로 대문자 등).
static bool IsModifierOrLock(WPARAM vk) {
    switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN: case VK_APPS:
        case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
        case VK_HANGUL: case VK_HANJA:
            return true;
    }
    return false;
}

// ------------------------------------------------------------------
// ITfKeyEventSink Implementation
// ------------------------------------------------------------------

static HRESULT STDMETHODCALLTYPE KES_QueryInterface(ITfKeyEventSink *pThis, REFIID riid, void **ppvObject) {
    JamotongTextService *obj = IMPL_TO_OBJ(KES, pThis);
    return obj->lpVtblTIP->QueryInterface((ITfTextInputProcessor*)obj, riid, ppvObject);
}

static ULONG STDMETHODCALLTYPE KES_AddRef(ITfKeyEventSink *pThis) {
    JamotongTextService *obj = IMPL_TO_OBJ(KES, pThis);
    return obj->lpVtblTIP->AddRef((ITfTextInputProcessor*)obj);
}

static ULONG STDMETHODCALLTYPE KES_Release(ITfKeyEventSink *pThis) {
    JamotongTextService *obj = IMPL_TO_OBJ(KES, pThis);
    return obj->lpVtblTIP->Release((ITfTextInputProcessor*)obj);
}

static HRESULT STDMETHODCALLTYPE KES_OnSetFocus(ITfKeyEventSink *pThis, BOOL fForeground) {
    JamotongTextService *obj = IMPL_TO_OBJ(KES, pThis);
    (void)fForeground;
    // 포커스 변경 시 조합 상태 전면 리셋 + 팝업들 정리 → 새 위치에서 깨끗이 시작.
    // 후보창은 Cancel(콜백 경유)로 닫아 pic 참조가 정리되게 한다 — 열린 채 방치되던 '멈춤' 방지.
    ResetComposition(obj);
    CodeInput_Hide();
    CandidateUI_Cancel();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE KES_OnTestKeyDown(ITfKeyEventSink *pThis, ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) {
    JamotongTextService *obj = IMPL_TO_OBJ(KES, pThis);
    (void)pic;
    if (pfEaten) *pfEaten = FALSE;
    if ((ULONG_PTR)GetMessageExtraInfo() == JAMO_SYNTH_MARK) return S_OK;   // 합성 입력 통과

    // OnKeyDown과 동일 순서(맨 앞): 코드입력/후보창/한자키를 예측-소비해야 OnKeyDown이 호출됨.
    // (TSF는 OnTestKeyDown이 TRUE로 표시한 키만 OnKeyDown 호출. 예측만 하고 부작용은 OnKeyDown에서.)
    if (CodeInput_IsVisible()) {     // 코드 입력 팝업이 뜨면 모든 키를 소비
        if (pfEaten) *pfEaten = TRUE;
        return S_OK;
    }
    if (CandidateUI_IsVisible()) {   // 후보창이 뜨면 모든 키를 소비(HandleKey가 전부 true)
        if (pfEaten) *pfEaten = TRUE;
        return S_OK;
    }

    // 이하 config/레이아웃 접근 전체를 설정 스레드의 Config_ApplyEdited(레이아웃 free)와 직렬화.
    // (기존엔 무락이라, 설정 적용 중 pHangulLayout/pChordLayout이 해제되는 순간 키가 오면 UAF.)
    EnterCriticalSection(&g_configLock);

    // 설정창 단축키 (설정 가능, 기본 Ctrl+Alt+K) 예측-소비 (OnKeyDown이 불려 설정창을 열도록).
    if (Config_IsShortcut(&obj->config, SC_FN_SETTINGS, Config_ResolveVK(wParam, lParam), Config_CurrentMods())) {
        if (pfEaten) *pfEaten = TRUE;
        goto tk_done;
    }

    // 유니코드 코드 입력 단축키 (설정 가능, 기본 Ctrl+Alt+U) 예측-소비.
    if (Config_IsShortcut(&obj->config, SC_FN_CODE, Config_ResolveVK(wParam, lParam), Config_CurrentMods())) {
        if (pfEaten) *pfEaten = TRUE;
        goto tk_done;
    }

    if (Config_IsShortcut(&obj->config, SC_FN_HANJA, Config_ResolveVK(wParam, lParam), Config_CurrentMods())
        || wParam == VK_KANJI) {   // 한자/변환 트리거 키 (기본 VK_HANJA — IsModifierOrLock이라 아래선 못 잡음)
        if (pfEaten) *pfEaten = TRUE;
        goto tk_done;
    }

    // Check if it's a layout rotate shortcut
    if (Config_IsShortcut(&obj->config, SC_FN_ROTATE, Config_ResolveVK(wParam, lParam), Config_CurrentMods())) {
        if (pfEaten) *pfEaten = TRUE;
        goto tk_done;
    }

    // Ctrl/Alt/Win 조합(Ctrl+C, Ctrl+A 등 앱 단축키): 조합 중이면 '여기서' 확정하고, 키는
    // 소비하지 않고 통과 → 앱이 원래 타이밍·원래 이벤트로 기능키를 처리한다.
    //   (RFC-0004 P0-1 개정, 실기 2026-07-08: eat+재전달 방식은 주입된 키가 이미 큐에 쌓인
    //   사용자 입력(Ctrl 뗌 등)을 추월당해 Ctrl 없이 처리됨 → 'C'가 ㅊ로 오입력되는 재앙.
    //   flush는 동기 편집세션이라 통과 전에 완료되고, 부작용은 조기 확정뿐이라 안전.)
    if (HasCtrlAltWin()) {
        if (obj->fsm.state != STATE_EMPTY) {
            FsmResult res = {Fsm_Flush(&obj->fsm), 0, false};
            JamoDiag("TK  vk=%02X ctrl/alt/win flush-in-test commit=U+%04X", (unsigned)wParam, (unsigned)res.commitChar);
            OutputResult(obj, pic, res, TRUE);   // 음절 확정 (통과 전에 동기 완료)
        }
        goto tk_done;   // pfEaten=FALSE — 앱이 단축키를 네이티브로 처리
    }

    {
        LayoutConfig *layout = Config_GetCurrentLayout(&obj->config);

        bool fullWidth = obj->config.options.fullWidth;
        if (layout && layout->type == LAYOUT_TYPE_STATIC_MAP) {   // layout==NULL(layoutCount 0) 시 크래시 방지
            bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            wchar_t qc = GetQwertyChar(wParam, isShift);
            if (qc > 0 && qc < 256 && (layout->charMap[qc] != qc || fullWidth)) {
                if (pfEaten) *pfEaten = TRUE;
                goto tk_done;
            }
        }
        if (layout && layout->type == LAYOUT_TYPE_PASSTHROUGH && fullWidth) {
            bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            wchar_t qc = GetQwertyChar(wParam, isShift);
            if (qc >= 0x20 && qc < 0x7F) { if (pfEaten) *pfEaten = TRUE; goto tk_done; }
        }

        if (layout && (layout->type == LAYOUT_TYPE_KOREAN_FSM || layout->type == LAYOUT_TYPE_HANGUL_CUSTOM)) {
            // 조합 중 백스페이스는 우리가 처리하므로 예측-소비 (앱이 먼저 지우지 않도록).
            // 모아치기 조합(obj->chord)은 fsm.state에 안 잡히므로 함께 검사.
            if (wParam == VK_BACK && (obj->fsm.state != STATE_EMPTY || obj->chord.activeKeys > 0)) {
                if (pfEaten) *pfEaten = TRUE;
                goto tk_done;
            }
            // Esc = 조합 취소 탈출구 (순차 FSM·모아치기 공통) — 갇힌 조합/칩을 확실히 비운다.
            if (wParam == VK_ESCAPE && (obj->fsm.state != STATE_EMPTY || obj->chord.activeKeys > 0)) {
                if (pfEaten) *pfEaten = TRUE;
                goto tk_done;
            }
            // 조합 중이면 비자모 키(F4·기능키 등)도 예측-소비 → OnKeyDown이 불려 조합을 확정한다.
            // (TSF는 OnTestKeyDown이 TRUE인 키만 OnKeyDown 호출. F4는 선택도 안 바꿔 sink도 안 뜸.)
            // 모디파이어/락 키는 제외(Shift로 대문자 등 조합 유지).
            if ((obj->fsm.state != STATE_EMPTY) && !IsModifierOrLock(wParam)) {
                if (pfEaten) *pfEaten = TRUE;
                goto tk_done;
            }
            const HangulLayout *hl = (const HangulLayout*)layout->pHangulLayout;
            bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            wchar_t qc = GetQwertyChar(wParam, isShift);
            JamoType jt = JAMO_NONE;
            if (qc > 0 && qc < 128)
                jt = hl ? hl->keymap[(int)qc].type : Layout_MapKeyToJamo(qc, layout->kbdVariant).type;
            if (jt != JAMO_NONE) {
                if (pfEaten) *pfEaten = TRUE;   // 자모 키만 소비 (자판별로 판정)
            }
        } else if (layout && layout->type == LAYOUT_TYPE_CHORD) {
            const ChordLayout *cl = (const ChordLayout*)layout->pChordLayout;
            bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            wchar_t qc = GetQwertyChar(wParam, isShift);
            if (cl && qc > 0 && qc < 128 && cl->keyBit[(int)qc] >= 0) {
                if (pfEaten) *pfEaten = TRUE;   // 코드 글쇠 소비
            }
        }
    }
tk_done:
    LeaveCriticalSection(&g_configLock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE KES_OnKeyDown(ITfKeyEventSink *pThis, ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) {
    JamotongTextService *obj = IMPL_TO_OBJ(KES, pThis);
    if (pfEaten) *pfEaten = FALSE;
    // 우리가 SendInput으로 넣은 합성 입력(코드 자판의 키/모디파이어 등)은 재처리하지 않고 통과
    if ((ULONG_PTR)GetMessageExtraInfo() == JAMO_SYNTH_MARK) { JamoDiag("KD  vk=%02X SYNTH-pass", (unsigned)wParam); return S_OK; }
    JamoDiag("KD  vk=%02X state=%d", (unsigned)wParam, (int)obj->fsm.state);

    bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    // 유니코드 코드 입력 팝업 키 라우팅 (열려 있으면 모든 키 소비; Enter 확정 시 문자 삽입)
    if (CodeInput_IsVisible()) {
        unsigned cp = 0;
        CodeInput_HandleKey((UINT)wParam, isShift, &cp);
        if (cp) {
            EditSessionData esd = {0};
            if (cp <= 0xFFFF) { esd.committed[0] = (wchar_t)cp; }
            else {   // BMP 밖 → UTF-16 서로게이트 쌍
                unsigned v = cp - 0x10000;
                esd.committed[0] = (wchar_t)(0xD800 + (v >> 10));
                esd.committed[1] = (wchar_t)(0xDC00 + (v & 0x3FF));
            }
            RequestEditSessionData(obj, pic, &esd);
        }
        if (pfEaten) *pfEaten = TRUE;
        return S_OK;
    }
    // Hanja UI interception (후보창 키 라우팅 — config 미접근이라 락 밖)
    if (CandidateUI_IsVisible()) {
        if (CandidateUI_HandleKey((UINT)wParam)) {
            if (pfEaten) *pfEaten = TRUE;
            return S_OK;
        }
    }

    // 여기부터 live config(한자키 설정·현재 레이아웃)를 읽고 플러그인/레이아웃을 쓰므로, 설정
    // 스레드의 Config_ApplyEdited(레이아웃 free)와 직렬화한다. 재진입 락이라 내부의
    // Config_RotateLayout 등과 중첩돼도 안전. 이후 모든 경로는 kd_done으로 해제.
    EnterCriticalSection(&g_configLock);

    // 설정창 단축키 (설정 가능, 기본 Ctrl+Alt+K — Win11 모던 설정엔 IME "옵션" 버튼이 없어 단축키로 연다).
    if (Config_IsShortcut(&obj->config, SC_FN_SETTINGS, Config_ResolveVK(wParam, lParam), Config_CurrentMods())) {
        SettingsUI_Show(&obj->config);
        if (pfEaten) *pfEaten = TRUE;
        goto kd_done;
    }

    // 코드 입력 트리거 (설정 가능한 단축키, 기본 Ctrl+Alt+U) — 조합 중이면 먼저 확정하고 캐럿 근처에 팝업.
    if (Config_IsShortcut(&obj->config, SC_FN_CODE, Config_ResolveVK(wParam, lParam), Config_CurrentMods())) {
        if (obj->fsm.state != STATE_EMPTY) {
            FsmResult res = {Fsm_Flush(&obj->fsm), 0, false};
            OutputResult(obj, pic, res, TRUE);
        }
        RECT rc; int x = 100, y = 100;
        if (GetCaretScreenRect(obj, &rc)) { x = rc.left; y = rc.bottom + 4; }
        CodeInput_Show(x, y);
        if (pfEaten) *pfEaten = TRUE;
        goto kd_done;
    }

    // Hanja trigger (설정된 한자 키 목록 — 기본 VK_HANJA, 복수 지정 가능). VK_KANJI는 항상 허용.
    if ((Config_IsShortcut(&obj->config, SC_FN_HANJA, Config_ResolveVK(wParam, lParam), Config_CurrentMods())
         || wParam == VK_KANJI) && !CandidateUI_IsVisible()) {
        EnsureHanjaDicts();   // lazy-load (첫 한자 요청 시 1회)
        wchar_t searchStr[64] = {0};
        int replaceLen = 0;
        bool special = false;
        bool fromSelection = false;   // 블록 선택 변환 여부 (EM_REPLACESEL 교체 경로 선택)

        if (obj->fsm.state == STATE_CHO) {
            // 단일 자음 + 한자키 → 특수문자 표 (호환 자모로 조회).
            // 조합 자음을 '문서에 먼저 커밋'한다 → 변환 취소 시 원본 자모가 남는다(실기 2026-07-08:
            // 취소하면 조합 중이던 글자가 사라지던 문제). 선택 시엔 이 커밋 글자를 교체(replaceLen=1).
            wchar_t ch = Layout_ChoToCompatJamo(obj->fsm.cho);
            searchStr[0] = ch; searchStr[1] = L'\0';
            special = true;
            Fsm_Init(&obj->fsm);
            FsmResult res = {ch, 0, false};
            OutputResult(obj, pic, res, TRUE);   // 문서에 자모 커밋
            replaceLen = 1;
        } else if (obj->fsm.state != STATE_EMPTY) {
            // 조합 중 음절 + 한자키 → 음절을 '문서에 먼저 커밋'(취소 시 원본 보존), 선택 시 교체.
            wchar_t syl = ComposeHangul(obj->fsm.cho, obj->fsm.jung, obj->fsm.jong);
            searchStr[0] = syl; searchStr[1] = L'\0';
            Fsm_Init(&obj->fsm);
            FsmResult res = {syl, 0, false};
            OutputResult(obj, pic, res, TRUE);   // 문서에 음절 커밋
            replaceLen = 1;
        } else {
            // [RFC-0003] 블록 선택 텍스트 변환 — 선택이 있으면 최우선.
            //   선택 교체 = InsertTextAtSelection 삽입(타이핑 덮어쓰기와 동일 경로)이라
            //   커밋 전용 원칙·CUAS 앱과 호환. 사전 미등재 선택이면 무동작(우아한 강등).
            wchar_t selBuf[24] = {0};
            RequestReadSelectionString(obj, pic, selBuf, 16);
            if (selBuf[0]) {
                wchar_t **selCands; int selCount;
                if (HanjaDict_Find(selBuf, &selCands, &selCount)) {
                    wcsncpy(searchStr, selBuf, 63); searchStr[63] = L'\0';   // 음절(1자)/단어 공용 사전
                    fromSelection = true;
                } else if (!selBuf[1] && SpecialChar_Find(selBuf[0], &selCands, &selCount)) {
                    searchStr[0] = selBuf[0]; searchStr[1] = L'\0';          // 단일 문자: 특수문자 표 폴백
                    special = true;
                    fromSelection = true;
                }
                replaceLen = 0;   // EDIT 계열은 EM_REPLACESEL, 비-EDIT는 삽입=선택 교체
            } else {
            wchar_t readBuf[32] = {0};
            if (SUCCEEDED(RequestReadSessionString(obj, pic, readBuf, 10))) {
                int len = wcslen(readBuf);
                // 유니코드 직접 입력: 커서 앞 2~6자리 16진수 → 해당 코드포인트 문자로 치환
                int hs = len;
                while (hs > 0 && IsHexW(readBuf[hs - 1])) hs--;
                int hlen = len - hs;
                if (hlen >= 2 && hlen <= 6) {
                    unsigned cp = 0;
                    for (int k = hs; k < len; k++) cp = cp * 16 + HexValW(readBuf[k]);
                    if (cp >= 0x20 && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
                        wchar_t out[3];
                        if (cp <= 0xFFFF) {
                            out[0] = (wchar_t)cp; out[1] = L'\0';
                        } else {   // BMP 밖 → UTF-16 서로게이트 쌍
                            cp -= 0x10000;
                            out[0] = (wchar_t)(0xD800 + (cp >> 10));
                            out[1] = (wchar_t)(0xDC00 + (cp & 0x3FF));
                            out[2] = L'\0';
                        }
                        RequestReplaceSessionString(obj, pic, hlen, out);
                        if (pfEaten) *pfEaten = TRUE;
                        goto kd_done;
                    }
                }
                // 단어 단위 한자 변환 (커서 앞 텍스트 — 교체는 range 편집이라 네이티브 앱 한정)
                for (int i = 0; i < len; i++) {
                    wchar_t **cands;
                    int count;
                    if (HanjaDict_Find(readBuf + i, &cands, &count)) {
                        wcscpy(searchStr, readBuf + i);
                        replaceLen = len - i;
                        break;
                    }
                }
            }
            }   // [RFC-0003] 선택 없음 분기 끝
        }

        if (searchStr[0]) {
            wchar_t **cands = NULL;
            int count = 0;
            bool found = special ? SpecialChar_Find(searchStr[0], &cands, &count)
                                 : HanjaDict_Find(searchStr, &cands, &count);
            if (found) {
                g_CandCtx.obj = obj;
                g_CandCtx.fromSelection = fromSelection;
                wcsncpy(g_CandCtx.word, searchStr, 31); g_CandCtx.word[31] = L'\0';   // 교체 검증용 원문
                // 후보창은 비동기(즉시 반환) — 나중 콜백에서 pic를 쓰므로 AddRef로 수명 고정(UAF 방지).
                // 이전 후보가 남아 있으면(방어적) 먼저 해제.
                if (g_CandCtx.pic) g_CandCtx.pic->lpVtbl->Release(g_CandCtx.pic);
                g_CandCtx.pic = pic;
                if (pic) pic->lpVtbl->AddRef(pic);

                RECT rcSel;   // 위치: 선택/캐럿 rect(방금 세션서 캡처) → GUIThreadInfo 폴백
                int x = 100, y = 100;
                if (GetCaretScreenRect(obj, &rcSel)) { x = rcSel.left; y = rcSel.bottom + 4; }
                
                CandidateUI_Show(x, y, cands, count, replaceLen, OnHanjaSelected, OnHanjaCancelled, &g_CandCtx);
                if (pfEaten) *pfEaten = TRUE;
                goto kd_done;
            }
        }
    }

    // Handle layout rotation
    if (Config_IsShortcut(&obj->config, SC_FN_ROTATE, Config_ResolveVK(wParam, lParam), Config_CurrentMods())) {
        // Flush composition before rotating
        LayoutConfig *curLayout = Config_GetCurrentLayout(&obj->config);
        if (curLayout && curLayout->type == LAYOUT_TYPE_DLL_PLUGIN) {
            JAMOTONG_PLUGIN_RESULT pRes = curLayout->pfnFlush(curLayout->pvPluginContext);
            if (pRes.wszCommitted[0]) {
                EditSessionData esd = {0};
                wcscpy(esd.committed, pRes.wszCommitted);
                RequestEditSessionData(obj, pic, &esd);
            }
        } else if (obj->fsm.state != STATE_EMPTY) {
            FsmResult res = {Fsm_Flush(&obj->fsm), 0, false};
            OutputResult(obj, pic, res, TRUE);   // 현재 음절 확정
        }
        Chord_Init(&obj->chord);   // 모아치기 잔여 상태도 정리 (자판 전환 = 조합 경계)
        Config_RotateLayout(&obj->config);
        LangBar_Update(obj->pLangBarItem);
        Jamotong_PublishStatus(&obj->config);   // 트레이 모니터링 갱신
        if (pfEaten) *pfEaten = TRUE;
        goto kd_done;
    }

    // Ctrl/Alt/Win 조합(앱 단축키): 확정은 OnTestKeyDown의 flush-in-test가 담당하고 키는
    // 통과되므로 보통 여기 도달하지 않는다. 도달하면(호스트가 Test 없이 KeyDown만 주는 등)
    // 방어적으로 확정만 하고 통과. 재전달·eat 금지 — 주입 키가 큐의 사용자 입력에 추월당해
    // 자모로 오입력되던 실기 재앙(2026-07-08, ㅊ/ㅍ) 재발 방지.
    if (HasCtrlAltWin()) {
        if (obj->fsm.state != STATE_EMPTY) {
            FsmResult res = {Fsm_Flush(&obj->fsm), 0, false};
            OutputResult(obj, pic, res, TRUE);   // 현재 음절 확정
        }
        goto kd_done;   // pfEaten=FALSE 유지 → 앱이 단축키 직접 처리
    }

    LayoutConfig *layout = Config_GetCurrentLayout(&obj->config);
    
    bool fullWidth = obj->config.options.fullWidth;
    if (layout && layout->type == LAYOUT_TYPE_STATIC_MAP) {
        wchar_t qc = GetQwertyChar(wParam, isShift);
        if (qc > 0 && qc < 256) {
            wchar_t mappedChar = layout->charMap[qc];
            // 리매핑됐거나(≠원본) 전각 모드면 가로채서 출력 (전각이면 전각 문자로 변환)
            if (mappedChar != 0 && (mappedChar != qc || fullWidth)) {
                if (pfEaten) *pfEaten = TRUE;
                FsmResult res = {0};
                res.commitChar = fullWidth ? ToFullWidth(mappedChar) : mappedChar;
                OutputResult(obj, pic, res, FALSE);
                goto kd_done;
            }
        }
    }
    // 영문 패스스루 + 전각 모드: 출력 가능한 ASCII를 전각으로 변환해 커밋
    if (layout && layout->type == LAYOUT_TYPE_PASSTHROUGH && fullWidth) {
        wchar_t qc = GetQwertyChar(wParam, isShift);
        if (qc >= 0x20 && qc < 0x7F) {
            if (pfEaten) *pfEaten = TRUE;
            FsmResult res = {0}; res.commitChar = ToFullWidth(qc);
            OutputResult(obj, pic, res, FALSE);
            goto kd_done;
        }
    }

    if (layout && layout->type == LAYOUT_TYPE_DLL_PLUGIN) {
        BYTE kbdState[256];
        GetKeyboardState(kbdState);
        JAMOTONG_PLUGIN_RESULT plugRes = layout->pfnProcessKey(
            layout->pvPluginContext, wParam, lParam, kbdState, NULL);
        
        if (plugRes.bEaten) {
            if (pfEaten) *pfEaten = TRUE;
        }
        if (plugRes.wszComposing[0] || plugRes.wszCommitted[0]) {
            EditSessionData esd = {0};
            wcscpy(esd.committed, plugRes.wszCommitted);
            wcscpy(esd.composing, plugRes.wszComposing);
            RequestEditSessionData(obj, pic, &esd);
            // 미리보기: 플러그인 조합 문자열(다중 글자 가능) 표시/숨김 (락 보유 중)
            RECT rc;
            if (obj->config.options.showPreview && plugRes.wszComposing[0] && GetCaretScreenRect(obj, &rc))
                PreeditOverlay_Show(&rc, plugRes.wszComposing, obj->config.options.previewFont,
                                    obj->config.options.previewFontSize);
            else
                PreeditOverlay_Hide();
        }
        goto kd_done;
    }

    if (layout && layout->type == LAYOUT_TYPE_CHORD) {
        // 일반 코드 자판(ARTSEY류): KeyDown에서 눌린 글쇠 누적만, 동작은 KES_OnKeyUp(모두 해제 시).
        const ChordLayout *cl = (const ChordLayout*)layout->pChordLayout;
        wchar_t keyChar = GetQwertyChar(wParam, isShift);
        bool eaten = ChordKb_KeyDown(&obj->chordKb, cl, (UINT)wParam, keyChar);
        if (eaten && pfEaten) *pfEaten = TRUE;   // 코드 글쇠가 아니면 통과(pfEaten=FALSE)
        goto kd_done;
    }

    if (layout && (layout->type == LAYOUT_TYPE_KOREAN_FSM || layout->type == LAYOUT_TYPE_HANGUL_CUSTOM)) {
        const HangulLayout *hl = (const HangulLayout*)layout->pHangulLayout;
        // Esc = 조합 취소 (확정하지 않고 비움) — MS IME 관례이자, 상태가 어긋난 조합/칩의
        // 확실한 탈출구(순차 FSM·모아치기 공통. 실기 2026-07-08 '갇힌 글자' 대응).
        if (wParam == VK_ESCAPE && (obj->fsm.state != STATE_EMPTY || obj->chord.activeKeys > 0)) {
            ResetComposition(obj);
            if (pfEaten) *pfEaten = TRUE;
            goto kd_done;
        }
        if (hl && hl->moachigi) {
            // 모아치기(동시치기): KeyDown에서 자모를 누적하고 조합 중 글자만 보여준다.
            // 확정은 눌린 글쇠가 모두 떨어지는 KES_OnKeyUp에서 이루어진다.
            if (wParam == VK_BACK && (obj->fsm.state != STATE_EMPTY || obj->chord.activeKeys > 0)) {
                ResetComposition(obj);   // 조합 중 백스페이스 → 조합 비움 (chord는 fsm.state에 안 잡힘)
                if (pfEaten) *pfEaten = TRUE;
                goto kd_done;
            }
            wchar_t keyChar = GetQwertyChar(wParam, isShift);
            ChordResult cr = Chord_KeyDown(&obj->chord, hl, (UINT)wParam, keyChar);
            if (cr.eaten) {
                if (pfEaten) *pfEaten = TRUE;
                if (cr.composing) { FsmResult res = {0, cr.composing, true}; OutputResult(obj, pic, res, FALSE); }
                goto kd_done;
            }
            // 자모 아님 → 아래 공통 처리로 진행하지 않고 통과 (space 등은 앱으로)
        } else {
            if (wParam == VK_BACK && obj->fsm.state != STATE_EMPTY) {   // 조합 중 백스페이스
                wchar_t pe = 0;
                if (obj->config.options.jamoDelete) {
                    Fsm_Backspace(&obj->fsm, &pe);          // 자소 단위: 마지막 자모만 제거
                } else {
                    Fsm_Init(&obj->fsm);                     // 음절 단위: 조합 전체 삭제
                }
                FsmResult res = {0, pe, true};               // pe==0 이면 조합 비움
                OutputResult(obj, pic, res, FALSE);
                if (pfEaten) *pfEaten = TRUE;
                goto kd_done;
            }
            LayoutResult lr = {JAMO_NONE, 0};
            wchar_t keyChar = GetQwertyChar(wParam, isShift);   // a~z + 숫자/기호 (세벌식/사용자 자판용)
            if (keyChar > 0 && keyChar < 128) lr = hl ? hl->keymap[(int)keyChar] : Layout_MapKeyToJamo(keyChar, layout->kbdVariant);
            if (lr.type != JAMO_NONE) {
                FsmResult res = Fsm_ProcessKey(&obj->fsm, keyChar, layout->kbdVariant, hl);
                if (pfEaten) *pfEaten = res.eaten;
                JamoDiag("FSM key=%c commit=U+%04X preedit=U+%04X", (char)keyChar, (unsigned)res.commitChar, (unsigned)res.preeditChar);
                if (res.commitChar || res.preeditChar) OutputResult(obj, pic, res, FALSE);
                goto kd_done;
            } else if (!IsModifierOrLock(wParam) && obj->fsm.state != STATE_EMPTY) {
                // [실험] 스페이스 경계: 확정 음절+공백을 '한 번의 삽입'으로 처리(재전달 없음).
                //   CUAS(AkelPad)에서 합성 경계키와 결과문자 전달이 경합해 마지막 음절이
                //   소실되는 현상 대응 — 삽입 경로 하나로 직렬화하면 경합 자체가 없다.
                //   (공백은 '문자'라 터미널 포함 삽입으로 전달 가능. 엔터/방향키는 제어키라 기존 재전달 유지)
                if (wParam == VK_SPACE) {
                    EditSessionData esd = {0};
                    wchar_t c = Fsm_Flush(&obj->fsm);
                    int n = 0;
                    if (c) esd.committed[n++] = c;
                    esd.committed[n++] = L' ';
                    JamoDiag("FLUSH+SPACE single-insert commit=U+%04X", (unsigned)c);
                    RequestEditSessionData(obj, pic, &esd);
                    obj->prevChipValid = FALSE; obj->chipPendingAdv = 0;
                    PreeditOverlay_Hide();   // 조합 종료 → 미리보기 제거
                    if (pfEaten) *pfEaten = TRUE;
                    goto kd_done;
                }
                // 그 외 비자모 키: 조합만 확정하고, 원래 키는 실제 이벤트로 재전달 → 앱이 네이티브 처리.
                // (편집세션 텍스트 삽입은 터미널(PuTTY 등)엔 안 통함. 방향키 이동·엔터·터미널 모두 지원.)
                FsmResult res = {Fsm_Flush(&obj->fsm), 0, false};   // 초성만/중성만 부분 상태도 올바르게 확정
                JamoDiag("FLUSH commit=U+%04X then resend vk=%02X (delayed)", (unsigned)res.commitChar, (unsigned)wParam);
                OutputResult(obj, pic, res, TRUE);   // 어절 경계 → 현재 음절 확정
                ScheduleKeyResend(wParam, lParam);   // 지연 재전달 — CUAS 전달 경합 방지 (AkelPad 엔터 소실)
                if (pfEaten) *pfEaten = TRUE;   // 원본 소비(재전달본이 대신 처리)
            }
        }
    } else {
        // Passthrough or other layouts
        // If there is an active FSM composition (shouldn't happen here, but safe to flush)
        if (obj->fsm.state != STATE_EMPTY) {
            FsmResult res = {Fsm_Flush(&obj->fsm), 0, false};
            OutputResult(obj, pic, res, TRUE);
        }
    }

kd_done:
    LeaveCriticalSection(&g_configLock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE KES_OnTestKeyUp(ITfKeyEventSink *pThis, ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) {
    (void)pic; (void)lParam;
    JamotongTextService *obj = IMPL_TO_OBJ(KES, pThis);
    if (pfEaten) *pfEaten = FALSE;
    // 모아치기/코드 자판으로 먹은 글쇠의 해제는 소비해야 OnKeyUp에서 확정 처리를 할 수 있다.
    if (wParam < 256 && (obj->chord.keyDown[wParam] || obj->chordKb.keyDown[wParam])) {
        if (pfEaten) *pfEaten = TRUE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE KES_OnKeyUp(ITfKeyEventSink *pThis, ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) {
    (void)lParam;
    JamotongTextService *obj = IMPL_TO_OBJ(KES, pThis);
    if (pfEaten) *pfEaten = FALSE;
    if (wParam >= 256) return S_OK;

    // 한글 모아치기: 눌린 글쇠가 모두 떨어지면 음절 확정 (키 이벤트는 입력 스레드에서 직렬 처리됨)
    if (obj->chord.keyDown[wParam]) {
        ChordResult cr = Chord_KeyUp(&obj->chord, (UINT)wParam);
        if (cr.eaten) {
            if (pfEaten) *pfEaten = TRUE;
            if (cr.commit) { FsmResult res = {cr.commit, 0, true}; OutputResult(obj, pic, res, FALSE); }
        }
        return S_OK;
    }

    // 일반 코드 자판(ARTSEY류): 모두 떨어지면 조합 동작 수행 (SendInput은 함수 내부에서)
    if (obj->chordKb.keyDown[wParam]) {
        EnterCriticalSection(&g_configLock);
        LayoutConfig *layout = Config_GetCurrentLayout(&obj->config);
        const ChordLayout *cl = (layout && layout->type == LAYOUT_TYPE_CHORD)
                                ? (const ChordLayout*)layout->pChordLayout : NULL;
        bool eaten = ChordKb_KeyUp(&obj->chordKb, cl, (UINT)wParam);
        LeaveCriticalSection(&g_configLock);
        if (eaten && pfEaten) *pfEaten = TRUE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE KES_OnPreservedKey(ITfKeyEventSink *pThis, ITfContext *pic, REFGUID rguid, BOOL *pfEaten) {
    (void)pThis; (void)pic; (void)rguid;
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

static ITfKeyEventSinkVtbl KeyEventSinkVtbl = {
    KES_QueryInterface,
    KES_AddRef,
    KES_Release,
    KES_OnSetFocus,
    KES_OnTestKeyDown,
    KES_OnTestKeyUp,   // 순서 수정: OnTestKeyUp이 OnKeyDown보다 앞 (TSF ITfKeyEventSink 규약)
    KES_OnKeyDown,
    KES_OnKeyUp,
    KES_OnPreservedKey
};


// ------------------------------------------------------------------
// ITfTextInputProcessor Implementation
// ------------------------------------------------------------------

static HRESULT STDMETHODCALLTYPE TIP_QueryInterface(ITfTextInputProcessor *pThis, REFIID riid, void **ppvObject) {
    JamotongTextService *obj = (JamotongTextService*)pThis;
    if (!ppvObject) return E_INVALIDARG;

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfTextInputProcessor)) {
        *ppvObject = &obj->lpVtblTIP;
    } else if (IsEqualIID(riid, &IID_ITfKeyEventSink)) {
        *ppvObject = &obj->lpVtblKES;
    } else if (IsEqualIID(riid, &IID_ITfDisplayAttributeProvider_J)) {
        *ppvObject = &obj->lpVtblDAP;   // composition display attribute provider
    } else if (IsEqualIID(riid, &IID_ITfFunctionProvider)) {
        *ppvObject = &obj->lpVtblFuncProv;   // 설정 "옵션" 노출
    } else if (IsEqualIID(riid, &IID_ITfFnConfigure_J) || IsEqualIID(riid, &IID_ITfFunction_J)) {
        *ppvObject = &obj->lpVtblFnConfig;   // "옵션" → 설정창
    } else {
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    obj->lpVtblTIP->AddRef(pThis);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE TIP_AddRef(ITfTextInputProcessor *pThis) {
    JamotongTextService *obj = (JamotongTextService*)pThis;
    return InterlockedIncrement(&obj->refCount);
}

static ULONG STDMETHODCALLTYPE TIP_Release(ITfTextInputProcessor *pThis) {
    JamotongTextService *obj = (JamotongTextService*)pThis;
    ULONG res = InterlockedDecrement(&obj->refCount);
    if (res == 0) {
        Config_Free(&obj->config);   // 레이아웃 리소스(플러그인 DLL/컨텍스트·name) 해제 — live의 유일 소유자
        InterlockedDecrement(&g_DllRefCount);
        HeapFree(GetProcessHeap(), 0, obj);
    }
    return res;
}

// ── ITfThreadMgrEventSink + ITfTextEditSink (문서 관여) ───────────────────────────
//   NavilIME 등 표준 TIP은 이 싱크들을 붙여 포커스 문서에 '관여'한다. 이게 없으면 CUAS(AkelPad
//   등 IMM32 브리지)가 우리 조합을 미소유로 보고 세션 후 확정·종료시키는 것으로 보인다.
//   (msctf.h에 선언만 있고 uuid 라이브러리에 없을 수 있어 IID를 직접 정의)
static const GUID kIID_ITfSource               = { 0x4ea48a35, 0x60ae, 0x446f, { 0x8f, 0xd6, 0xe6, 0xa8, 0xd8, 0x24, 0x59, 0xf7 } };
static const GUID kIID_ITfThreadMgrEventSink   = { 0xaa80e80e, 0x2021, 0x11d2, { 0x93, 0xe0, 0x00, 0x60, 0xb0, 0x67, 0xb8, 0x6e } };
static const GUID kIID_ITfTextEditSink         = { 0x8127d409, 0xccd3, 0x4683, { 0x96, 0x7a, 0xb4, 0x3d, 0x5b, 0x48, 0x2b, 0xf7 } };

static void AdviseTextEditSink(JamotongTextService *obj, ITfContext *pContext);

// ITfTextEditSink
static HRESULT STDMETHODCALLTYPE TES_QueryInterface(ITfTextEditSink *pThis, REFIID riid, void **ppv) {
    JamotongTextService *obj = IMPL_TO_OBJ(TES, pThis);
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &kIID_ITfTextEditSink)) {
        *ppv = &obj->lpVtblTES; obj->lpVtblTIP->AddRef((ITfTextInputProcessor*)obj); return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE TES_AddRef(ITfTextEditSink *pThis)  { JamotongTextService *obj = IMPL_TO_OBJ(TES, pThis); return obj->lpVtblTIP->AddRef((ITfTextInputProcessor*)obj); }
static ULONG STDMETHODCALLTYPE TES_Release(ITfTextEditSink *pThis) { JamotongTextService *obj = IMPL_TO_OBJ(TES, pThis); return obj->lpVtblTIP->Release((ITfTextInputProcessor*)obj); }
static HRESULT STDMETHODCALLTYPE TES_OnEndEdit(ITfTextEditSink *pThis, ITfContext *pic, TfEditCookie ec, ITfEditRecord *pRec) {
    (void)pThis; (void)pic; (void)ec; (void)pRec; return S_OK;   // 관여 표시가 목적; 처리는 없음
}
static ITfTextEditSinkVtbl g_TESVtbl = { TES_QueryInterface, TES_AddRef, TES_Release, TES_OnEndEdit };

// ITfThreadMgrEventSink
static HRESULT STDMETHODCALLTYPE TMES_QueryInterface(ITfThreadMgrEventSink *pThis, REFIID riid, void **ppv) {
    JamotongTextService *obj = IMPL_TO_OBJ(TMES, pThis);
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &kIID_ITfThreadMgrEventSink)) {
        *ppv = &obj->lpVtblTMES; obj->lpVtblTIP->AddRef((ITfTextInputProcessor*)obj); return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE TMES_AddRef(ITfThreadMgrEventSink *pThis)  { JamotongTextService *obj = IMPL_TO_OBJ(TMES, pThis); return obj->lpVtblTIP->AddRef((ITfTextInputProcessor*)obj); }
static ULONG STDMETHODCALLTYPE TMES_Release(ITfThreadMgrEventSink *pThis) { JamotongTextService *obj = IMPL_TO_OBJ(TMES, pThis); return obj->lpVtblTIP->Release((ITfTextInputProcessor*)obj); }
static HRESULT STDMETHODCALLTYPE TMES_OnInitDocumentMgr(ITfThreadMgrEventSink *pThis, ITfDocumentMgr *p)   { (void)pThis; (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE TMES_OnUninitDocumentMgr(ITfThreadMgrEventSink *pThis, ITfDocumentMgr *p) { (void)pThis; (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE TMES_OnSetFocus(ITfThreadMgrEventSink *pThis, ITfDocumentMgr *pdimFocus, ITfDocumentMgr *pdimPrev) {
    JamotongTextService *obj = IMPL_TO_OBJ(TMES, pThis);
    (void)pdimPrev;
    ITfContext *pCtx = NULL;
    if (pdimFocus && SUCCEEDED(pdimFocus->lpVtbl->GetTop(pdimFocus, &pCtx)) && pCtx) {
        AdviseTextEditSink(obj, pCtx);        // 새 포커스 컨텍스트에 텍스트편집 싱크 부착
        pCtx->lpVtbl->Release(pCtx);
    } else {
        AdviseTextEditSink(obj, NULL);
    }
    ResetComposition(obj);   // 문서 포커스 이동 → 조합·미리보기 잔상 전면 리셋
    CodeInput_Hide();
    CandidateUI_Cancel();    // 후보창도 취소(pic 정리) — 다른 문서 위 잔류 방지
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE TMES_OnPushContext(ITfThreadMgrEventSink *pThis, ITfContext *pic) { (void)pThis; (void)pic; return S_OK; }
static HRESULT STDMETHODCALLTYPE TMES_OnPopContext(ITfThreadMgrEventSink *pThis, ITfContext *pic)  { (void)pThis; (void)pic; return S_OK; }
static ITfThreadMgrEventSinkVtbl g_TMESVtbl = {
    TMES_QueryInterface, TMES_AddRef, TMES_Release,
    TMES_OnInitDocumentMgr, TMES_OnUninitDocumentMgr, TMES_OnSetFocus, TMES_OnPushContext, TMES_OnPopContext
};

// 텍스트편집 싱크를 (이전 것 해제 후) 주어진 컨텍스트에 부착. pContext=NULL이면 해제만.
static void AdviseTextEditSink(JamotongTextService *obj, ITfContext *pContext) {
    if (obj->pTESContext) {
        ITfSource *pSrc = NULL;
        if (SUCCEEDED(obj->pTESContext->lpVtbl->QueryInterface(obj->pTESContext, &kIID_ITfSource, (void**)&pSrc))) {
            pSrc->lpVtbl->UnadviseSink(pSrc, obj->tesCookie);
            pSrc->lpVtbl->Release(pSrc);
        }
        obj->pTESContext->lpVtbl->Release(obj->pTESContext);
        obj->pTESContext = NULL; obj->tesCookie = TF_INVALID_COOKIE;
    }
    if (!pContext) return;
    ITfSource *pSrc = NULL;
    if (SUCCEEDED(pContext->lpVtbl->QueryInterface(pContext, &kIID_ITfSource, (void**)&pSrc))) {
        if (SUCCEEDED(pSrc->lpVtbl->AdviseSink(pSrc, &kIID_ITfTextEditSink, (IUnknown*)&obj->lpVtblTES, &obj->tesCookie))) {
            obj->pTESContext = pContext; pContext->lpVtbl->AddRef(pContext);
        }
        pSrc->lpVtbl->Release(pSrc);
    }
}

static void AdviseThreadMgrEventSink(JamotongTextService *obj) {
    ITfSource *pSrc = NULL;
    if (obj->threadMgr && SUCCEEDED(obj->threadMgr->lpVtbl->QueryInterface(obj->threadMgr, &kIID_ITfSource, (void**)&pSrc))) {
        pSrc->lpVtbl->AdviseSink(pSrc, &kIID_ITfThreadMgrEventSink, (IUnknown*)&obj->lpVtblTMES, &obj->tmesCookie);
        pSrc->lpVtbl->Release(pSrc);
    }
    // 이미 포커스 문서가 있으면 지금 텍스트편집 싱크 부착 (OnSetFocus가 이후 갱신)
    if (obj->threadMgr) {
        ITfDocumentMgr *pdim = NULL;
        if (SUCCEEDED(obj->threadMgr->lpVtbl->GetFocus(obj->threadMgr, &pdim)) && pdim) {
            ITfContext *pCtx = NULL;
            if (SUCCEEDED(pdim->lpVtbl->GetTop(pdim, &pCtx)) && pCtx) { AdviseTextEditSink(obj, pCtx); pCtx->lpVtbl->Release(pCtx); }
            pdim->lpVtbl->Release(pdim);
        }
    }
}
static void UnadviseThreadMgrEventSink(JamotongTextService *obj) {
    AdviseTextEditSink(obj, NULL);
    ITfSource *pSrc = NULL;
    if (obj->threadMgr && obj->tmesCookie != TF_INVALID_COOKIE
        && SUCCEEDED(obj->threadMgr->lpVtbl->QueryInterface(obj->threadMgr, &kIID_ITfSource, (void**)&pSrc))) {
        pSrc->lpVtbl->UnadviseSink(pSrc, obj->tmesCookie);
        pSrc->lpVtbl->Release(pSrc);
        obj->tmesCookie = TF_INVALID_COOKIE;
    }
}

static void AdviseKeyEventSink(JamotongTextService *obj) {
    if (obj->threadMgr) {
        ITfKeystrokeMgr *pKeystrokeMgr = NULL;
        if (SUCCEEDED(obj->threadMgr->lpVtbl->QueryInterface(obj->threadMgr, &IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr))) {
            pKeystrokeMgr->lpVtbl->AdviseKeyEventSink(pKeystrokeMgr, obj->clientId, (ITfKeyEventSink*)&obj->lpVtblKES, TRUE);
            pKeystrokeMgr->lpVtbl->Release(pKeystrokeMgr);
        }
    }
}

static void UnadviseKeyEventSink(JamotongTextService *obj) {
    if (obj->threadMgr) {
        ITfKeystrokeMgr *pKeystrokeMgr = NULL;
        if (SUCCEEDED(obj->threadMgr->lpVtbl->QueryInterface(obj->threadMgr, &IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr))) {
            pKeystrokeMgr->lpVtbl->UnadviseKeyEventSink(pKeystrokeMgr, obj->clientId);
            pKeystrokeMgr->lpVtbl->Release(pKeystrokeMgr);
        }
    }
}

static HRESULT STDMETHODCALLTYPE TIP_Activate(ITfTextInputProcessor *pThis, ITfThreadMgr *ptim, TfClientId tid) {
    JamotongTextService *obj = IMPL_TO_OBJ(TIP, pThis);
    obj->threadMgr = ptim;
    obj->threadMgr->lpVtbl->AddRef(obj->threadMgr);
    obj->clientId = tid;

    AdviseKeyEventSink(obj);
    AdviseThreadMgrEventSink(obj);   // 문서 포커스/편집 싱크 부착 (CUAS 조합유지 목적)
    FuncConfig_Advise(obj);   // 설정 "옵션"(ITfFunctionProvider) in-session 노출

    obj->daAtom = DA_RegisterAtom(ptim);   // composition display-attribute atom (per thread)

    // 최초 1회 전역 초기화: 후보창 윈도 클래스 등록(없으면 CreateWindowEx 실패 → 후보창 안 뜸).
    // 한자/훈음 사전은 여기서 로드하지 않는다 — TIP은 텍스트를 쓰는 모든 프로세스에 로드되므로
    // 활성화마다 파일 IO를 하면 낭비. 첫 한자 요청 시 EnsureHanjaDicts()가 1회 로드(RFC-0004 §6.1).
    {
        static bool s_globalInit = false;
        if (!s_globalInit) {
            s_globalInit = true;
            CandidateUI_Initialize();
        }
    }

    Jamotong_PublishStatus(&obj->config);   // 활성화 시 현재 자판 상태 발행

    // 언어 바 아이템 등록
    ITfLangBarItemMgr *pLangBarMgr = NULL;
    if (SUCCEEDED(ptim->lpVtbl->QueryInterface(ptim, &IID_ITfLangBarItemMgr, (void**)&pLangBarMgr))) {
        obj->pLangBarItem = LangBar_Create(obj);
        if (obj->pLangBarItem) {
            pLangBarMgr->lpVtbl->AddItem(pLangBarMgr, (ITfLangBarItem*)obj->pLangBarItem);
        }
        pLangBarMgr->lpVtbl->Release(pLangBarMgr);
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE TIP_Deactivate(ITfTextInputProcessor *pThis) {
    JamotongTextService *obj = IMPL_TO_OBJ(TIP, pThis);

    // 설정창 스레드 종료 후 조인 — 이걸 안 하면 설정창이 (곧 해제될) obj->config를 계속 참조해 UAF.
    SettingsUI_Shutdown();

    // 진행 중이던 오토마타 상태 정리 (재활성 후 유령 입력 방지) + 팝업 창들 파괴(입력 스레드).
    // 후보창은 Cancel(콜백 경유)로 닫아 pic 참조와 g_CandCtx.obj(raw 서비스 포인터)를 정리 —
    // 열린 채 Deactivate되면 콜백이 해제된 서비스를 만질 수 있다(RFC-0004 P1-1 UAF).
    CandidateUI_Cancel();
    Fsm_Init(&obj->fsm);
    Chord_Init(&obj->chord);
    PreeditOverlay_Uninitialize();
    CodeInput_Uninitialize();

    // 언어 바 아이템 해제. 셸이 아이템 참조를 우리 Release 이후까지 잡고 있을 수 있으므로,
    // 먼저 pService 역참조를 끊는다(아이템 메서드들이 NULL 체크로 no-op) — 서비스 해제 후 UAF 방지.
    if (obj->pLangBarItem && obj->threadMgr) {
        ITfLangBarItemMgr *pLangBarMgr = NULL;
        if (SUCCEEDED(obj->threadMgr->lpVtbl->QueryInterface(obj->threadMgr, &IID_ITfLangBarItemMgr, (void**)&pLangBarMgr))) {
            pLangBarMgr->lpVtbl->RemoveItem(pLangBarMgr, (ITfLangBarItem*)obj->pLangBarItem);
            pLangBarMgr->lpVtbl->Release(pLangBarMgr);
        }
        obj->pLangBarItem->pService = NULL;
        obj->pLangBarItem->lpVtblButton->Release((ITfLangBarItemButton*)obj->pLangBarItem);
        obj->pLangBarItem = NULL;
    }
    
    FuncConfig_Unadvise(obj);
    UnadviseThreadMgrEventSink(obj);
    UnadviseKeyEventSink(obj);

    if (obj->threadMgr) {
        obj->threadMgr->lpVtbl->Release(obj->threadMgr);
        obj->threadMgr = NULL;
    }
    obj->clientId = 0;
    return S_OK;
}

static ITfTextInputProcessorVtbl TextInputProcessorVtbl = {
    TIP_QueryInterface,
    TIP_AddRef,
    TIP_Release,
    TIP_Activate,
    TIP_Deactivate
};

// ------------------------------------------------------------------
// Instance Creation
// ------------------------------------------------------------------

HRESULT JamotongTextService_Create(IUnknown *pUnkOuter, REFIID riid, void **ppvObject) {
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    if (!ppvObject) return E_INVALIDARG;

    JamotongTextService *obj = (JamotongTextService*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(JamotongTextService));
    if (!obj) return E_OUTOFMEMORY;

    obj->lpVtblTIP = &TextInputProcessorVtbl;
    obj->lpVtblKES = &KeyEventSinkVtbl;
    obj->lpVtblDAP = &g_JamotongDAPVtbl;
    obj->lpVtblTMES = &g_TMESVtbl;
    obj->lpVtblTES = &g_TESVtbl;
    obj->tmesCookie = TF_INVALID_COOKIE;
    obj->tesCookie = TF_INVALID_COOKIE;
    obj->pTESContext = NULL;
    FuncConfig_Init(obj);   // ITfFunctionProvider/ITfFnConfigure vtbl (설정 "옵션")
    obj->refCount = 1;
    Config_LoadDefault(&obj->config);
    {   // 저장된 사용자 설정이 있으면 덮어씀(설정 "옵션"·활성화 모두 현재값 반영)
        wchar_t cfgPath[MAX_PATH];
        if (Config_UserPath(cfgPath, MAX_PATH)) Config_LoadFromFile(&obj->config, cfgPath);
    }
    Fsm_Init(&obj->fsm);
    Chord_Init(&obj->chord);
    ChordKb_Init(&obj->chordKb);
    InterlockedIncrement(&g_DllRefCount);

    HRESULT hr = obj->lpVtblTIP->QueryInterface((ITfTextInputProcessor*)obj, riid, ppvObject);
    obj->lpVtblTIP->Release((ITfTextInputProcessor*)obj);
    return hr;
}
