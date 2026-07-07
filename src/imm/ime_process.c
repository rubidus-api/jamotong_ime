// ime_process.c — IMM32 조합 처리 본체.
//   ImeProcessKey: 우리가 처리할 키인지 예측. ImeToAsciiEx: FSM으로 조합/확정 → COMPOSITIONSTRING
//   갱신 + WM_IME_STARTCOMPOSITION/COMPOSITION/ENDCOMPOSITION 메시지 생성.
#include "ime_internal.h"
#include "../layout.h"
#include "../hangul_layout.h"
#include "../hanja_dict.h"
#include "../special_char.h"
#include "../candidate_ui.h"
#include <string.h>

void Ime_PrivInit(ImePrivate *priv) {
    if (priv) Fsm_Init(&priv->fsm);
}

// ── 한자/특수문자 후보 (candidate_ui.c 독립 창 + hanja_dict.c 재사용) ─────────────
static wchar_t g_hanjaResult[40];   // 선택된 한자 문자열(콜백이 채움)
static bool s_hanjaSel = false, s_hanjaCancel = false;
static void HanjaOnSelect(int idx, const wchar_t *str, void *ctx) {
    (void)idx; (void)ctx;
    if (str) { wcsncpy(g_hanjaResult, str, 39); g_hanjaResult[39] = L'\0'; }
    s_hanjaSel = true;
}
static void HanjaOnCancel(void *ctx) { (void)ctx; s_hanjaCancel = true; }

void Ime_HanjaInit(void) {   // 프로세스 1회: 후보창 클래스 등록 + 한자사전 로드(.ime 옆 hanja.txt)
    CandidateUI_Initialize();
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(g_hImeInst, path, MAX_PATH)) {
        wchar_t *slash = wcsrchr(path, L'\\');
        if (slash) { wcscpy(slash + 1, L"hanja.txt"); HanjaDict_Load(path); }
    }
}

// 현재 조합 음절로 한자/특수문자 후보창을 띄운다. 떴으면 true.
static bool Ime_HanjaShow(FsmContext *fsm) {
    wchar_t key = 0; bool special = false;
    if (fsm->state == STATE_CHO) {            // 단일 자음 + 한자키 → 특수문자 표
        key = Layout_ChoToCompatJamo(fsm->cho); special = true;
    } else if (fsm->state != STATE_EMPTY) {   // 조합 중 음절 → 한자
        key = ComposeHangul(fsm->cho, fsm->jung, fsm->jong);
    }
    if (!key) return false;
    wchar_t sstr[2] = { key, 0 };
    wchar_t **cands = NULL; int count = 0;
    bool found = special ? SpecialChar_Find(key, &cands, &count)
                         : HanjaDict_Find(sstr, &cands, &count);
    if (!found || count <= 0) return false;
    POINT pt; int x = 100, y = 100;
    if (GetCaretPos(&pt)) { HWND h = GetFocus(); if (h) ClientToScreen(h, &pt); x = pt.x; y = pt.y + 25; }
    CandidateUI_Show(x, y, cands, count, 1, HanjaOnSelect, HanjaOnCancel, NULL);
    return true;
}

// 조합에 쓸 한글 자판(FSM 계열) + variant + hl 얻기.
//   IMM32 IME는 '한국어 IME'이고 한/영은 IME_CMODE_NATIVE로 전환한다. config의 '현재 자판'이
//   영문(en_qwerty 등)일 수 있으므로, 현재가 한글이면 그걸, 아니면 config의 첫 한글 자판을 쓴다.
static const HangulLayout *CurLayout(int *variant, bool *isKorean) {
    LayoutConfig *layout = Config_GetCurrentLayout(&g_imeConfig);
    if (!(layout && (layout->type == LAYOUT_TYPE_KOREAN_FSM || layout->type == LAYOUT_TYPE_HANGUL_CUSTOM))) {
        layout = NULL;
        for (int i = 0; i < g_imeConfig.layoutCount; i++) {
            int t = g_imeConfig.layouts[i].type;
            if (t == LAYOUT_TYPE_KOREAN_FSM || t == LAYOUT_TYPE_HANGUL_CUSTOM) { layout = &g_imeConfig.layouts[i]; break; }
        }
    }
    *isKorean = (layout != NULL);
    *variant = layout ? layout->kbdVariant : KBD_DUBEOL;
    return layout ? (const HangulLayout*)layout->pHangulLayout : NULL;
}

static JamoType JamoOf(wchar_t qc, const HangulLayout *hl, int variant) {
    if (qc <= 0 || qc >= 128) return JAMO_NONE;
    return hl ? hl->keymap[(int)qc].type : Layout_MapKeyToJamo(qc, variant).type;
}

// ── ImeProcessKey: 이 키를 IME가 처리할지(TRUE면 ImeToAsciiEx 호출됨) ─────────────
BOOL WINAPI ImeProcessKey(HIMC hIMC, UINT vKey, LPARAM lKeyData, CONST LPBYTE kbd) {
    (void)lKeyData;
    if (!hIMC || !kbd) return FALSE;
    Ime_EnsureConfig();
    LPINPUTCONTEXT pIC = ImmLockIMC(hIMC);
    if (!pIC) return FALSE;

    BOOL handle = FALSE;
    bool ctrl = (kbd[VK_CONTROL] & 0x80) != 0;
    bool alt  = (kbd[VK_MENU]    & 0x80) != 0;
    bool shift = (kbd[VK_SHIFT]  & 0x80) != 0;
    bool native = (pIC->fdwConversion & IME_CMODE_NATIVE) != 0;

    ImePrivate *priv = pIC->hPrivate ? (ImePrivate*)ImmLockIMCC(pIC->hPrivate) : NULL;
    bool composing = priv && priv->fsm.state != STATE_EMPTY;

    if (CandidateUI_IsVisible()) {           // 한자 후보창 열림 → 모든 키를 후보창으로 라우팅
        handle = TRUE;
    } else if (vKey == VK_HANGUL) {          // 한/영 토글 키 (한국어 키보드 오른쪽 Alt = VK_HANGUL)
        handle = TRUE;
    } else if (native && !ctrl && !alt) {
        int variant; bool isKorean;
        const HangulLayout *hl = CurLayout(&variant, &isKorean);
        wchar_t qc = Layout_QwertyChar(vKey, shift);
        if (isKorean) {
            if (JamoOf(qc, hl, variant) != JAMO_NONE) handle = TRUE;   // 자모 키
            else if (composing) {
                if (vKey == VK_BACK) handle = TRUE;                    // 조합 중 백스페이스
                else if (qc >= 0x20) handle = TRUE;                    // space/출력가능 문자 → 확정+추가
                // 종료키: 현재 음절 확정 후 그 키를 앱에 재주입(Enter/Tab/화살표/Del/Ins), Esc=취소
                else if (vKey == VK_RETURN || vKey == VK_TAB || vKey == VK_ESCAPE ||
                         (vKey >= VK_PRIOR && vKey <= VK_DOWN) || vKey == VK_DELETE || vKey == VK_INSERT)
                    handle = TRUE;
                else if (vKey == VK_HANJA) handle = TRUE;   // 한자 변환 트리거
            }
        }
    }

    if (priv) ImmUnlockIMCC(pIC->hPrivate);
    ImmUnlockIMC(hIMC);
    return handle;
}

// ── COMPOSITIONSTRING 빌드 (comp=조합/미확정, result=확정) ───────────────────────
static DWORD Align4(DWORD x) { return (x + 3u) & ~3u; }

static HIMCC BuildCompStr(HIMCC hCompStr, const wchar_t *comp, DWORD compLen,
                          const wchar_t *result, DWORD resultLen) {
    DWORD total = sizeof(COMPOSITIONSTRING);
    if (compLen)   total = Align4(total) + compLen /*attr*/;
    total = Align4(total) + (compLen ? 2*sizeof(DWORD) : 0) /*compClause*/ + compLen*sizeof(WCHAR) /*compStr*/;
    total = Align4(total) + (resultLen ? 2*sizeof(DWORD) : 0) /*resultClause*/ + resultLen*sizeof(WCHAR) /*resultStr*/;
    total += 8;

    if (!hCompStr) hCompStr = ImmCreateIMCC(total);
    else if (ImmGetIMCCSize(hCompStr) < total) hCompStr = ImmReSizeIMCC(hCompStr, total);
    if (!hCompStr) return NULL;

    LPCOMPOSITIONSTRING pcs = (LPCOMPOSITIONSTRING)ImmLockIMCC(hCompStr);
    if (!pcs) return hCompStr;
    memset(pcs, 0, sizeof(COMPOSITIONSTRING));
    BYTE *base = (BYTE*)pcs;
    DWORD off = sizeof(COMPOSITIONSTRING);

    if (compLen) {
        // comp attr (BYTE per char = ATTR_INPUT)
        pcs->dwCompAttrLen = compLen; pcs->dwCompAttrOffset = off;
        memset(base + off, ATTR_INPUT, compLen); off += compLen;
        // comp clause {0, compLen}
        off = Align4(off);
        pcs->dwCompClauseLen = 2*sizeof(DWORD); pcs->dwCompClauseOffset = off;
        ((DWORD*)(base+off))[0] = 0; ((DWORD*)(base+off))[1] = compLen; off += 2*sizeof(DWORD);
        // comp str
        pcs->dwCompStrLen = compLen; pcs->dwCompStrOffset = off;
        memcpy(base+off, comp, compLen*sizeof(WCHAR)); off += compLen*sizeof(WCHAR);
        pcs->dwCursorPos = compLen;
    }
    if (resultLen) {
        off = Align4(off);
        pcs->dwResultClauseLen = 2*sizeof(DWORD); pcs->dwResultClauseOffset = off;
        ((DWORD*)(base+off))[0] = 0; ((DWORD*)(base+off))[1] = resultLen; off += 2*sizeof(DWORD);
        pcs->dwResultStrLen = resultLen; pcs->dwResultStrOffset = off;
        memcpy(base+off, result, resultLen*sizeof(WCHAR)); off += resultLen*sizeof(WCHAR);
    }
    pcs->dwSize = off;
    ImmUnlockIMCC(hCompStr);
    return hCompStr;
}

// 메시지 슬롯 채우기(용량 초과 방지)
static void PushMsg(LPTRANSMSGLIST buf, UINT cap, UINT *idx, UINT msg, WPARAM wp, LPARAM lp) {
    if (*idx < cap) { buf->TransMsg[*idx].message = msg; buf->TransMsg[*idx].wParam = wp; buf->TransMsg[*idx].lParam = lp; }
    (*idx)++;
}

// ── ImeToAsciiEx: 키 처리 → 조합/확정 반영 ───────────────────────────────────────
UINT WINAPI ImeToAsciiEx(UINT uVKey, UINT uScanCode, CONST LPBYTE kbd,
                         LPTRANSMSGLIST lpTransBuf, UINT fuState, HIMC hIMC) {
    (void)fuState;
    if (!hIMC || !kbd || !lpTransBuf) return 0;
    LPINPUTCONTEXT pIC = ImmLockIMC(hIMC);
    if (!pIC) return 0;
    ImePrivate *priv = pIC->hPrivate ? (ImePrivate*)ImmLockIMCC(pIC->hPrivate) : NULL;
    if (!priv) { ImmUnlockIMC(hIMC); return 0; }

    int variant; bool isKorean;
    const HangulLayout *hl = CurLayout(&variant, &isKorean);
    bool shift = (kbd[VK_SHIFT] & 0x80) != 0;
    wchar_t qc = Layout_QwertyChar(uVKey, shift);
    bool wasComposing = (priv->fsm.state != STATE_EMPTY);

    wchar_t result[40]; DWORD rn = 0;   // 한자(다자) 수용 위해 넉넉히
    wchar_t comp[8];    DWORD cn = 0;

    JamoType jt = JamoOf(qc, hl, variant);
    UINT passVK = 0;          // 확정 후 앱에 재주입할 종료키(Enter/Tab/화살표/Del/Ins)
    bool modeToggled = false;
    bool noChange = false;    // 조합/결과 변화 없음(후보 취소·후보 표시) → 메시지 미생성

    if (CandidateUI_IsVisible()) {
        s_hanjaSel = false; s_hanjaCancel = false;
        CandidateUI_HandleKey(uVKey);              // 숫자/방향/Enter=선택, Esc=취소 (내부에서 창 닫음)
        if (s_hanjaSel) {
            for (const wchar_t *p = g_hanjaResult; *p && rn < 39; p++) result[rn++] = *p;
            Fsm_Init(&priv->fsm);                  // 조합 음절 → 선택 한자로 확정
        } else {
            noChange = true;                        // 취소/미선택 → 조합 그대로
        }
    } else if (uVKey == VK_HANJA && wasComposing) {
        Ime_HanjaShow(&priv->fsm);                 // 후보창 표시(조합 유지) — 성패 무관
        noChange = true;
    } else if (uVKey == VK_HANGUL) {
        if (wasComposing) { wchar_t last = Fsm_Flush(&priv->fsm); if (last) result[rn++] = last; }
        pIC->fdwConversion ^= IME_CMODE_NATIVE;   // 한/영 전환 (조합 중이면 먼저 확정)
        modeToggled = true;
    } else if (jt != JAMO_NONE) {
        FsmResult r = Fsm_ProcessKey(&priv->fsm, qc, variant, hl);
        if (r.commitChar) result[rn++] = r.commitChar;
        if (r.preeditChar) comp[cn++] = r.preeditChar;
    } else if (uVKey == VK_BACK) {
        wchar_t pe = 0;
        if (g_imeConfig.options.jamoDelete) Fsm_Backspace(&priv->fsm, &pe);
        else Fsm_Init(&priv->fsm);
        if (pe) comp[cn++] = pe;
    } else if (uVKey == VK_ESCAPE && wasComposing) {
        Fsm_Init(&priv->fsm);                    // 조합 취소(결과 없이 비움)
    } else if (wasComposing && qc >= 0x20) {
        wchar_t last = Fsm_Flush(&priv->fsm);    // 현재 음절 확정
        if (last) result[rn++] = last;
        result[rn++] = qc;                        // 그 출력문자도 확정
    } else if (wasComposing && (uVKey == VK_RETURN || uVKey == VK_TAB ||
               (uVKey >= VK_PRIOR && uVKey <= VK_DOWN) || uVKey == VK_DELETE || uVKey == VK_INSERT)) {
        wchar_t last = Fsm_Flush(&priv->fsm);    // 음절 확정 후
        if (last) result[rn++] = last;
        passVK = uVKey;                           // 종료키를 앱에 재주입
    }

    if (noChange) { ImmUnlockIMCC(pIC->hPrivate); ImmUnlockIMC(hIMC); return 0; }

    bool nowComposing = (cn > 0);

    // COMPOSITIONSTRING 갱신 (핸들 재할당 가능 → 되돌려 저장)
    HIMCC newComp = BuildCompStr(pIC->hCompStr, comp, cn, result, rn);
    if (newComp) pIC->hCompStr = newComp;

    // 메시지 시퀀스
    UINT cap = lpTransBuf->uMsgCount ? lpTransBuf->uMsgCount : 8;
    UINT idx = 0;
    if (!wasComposing && (cn > 0 || rn > 0))
        PushMsg(lpTransBuf, cap, &idx, WM_IME_STARTCOMPOSITION, 0, 0);
    DWORD flags = 0;
    if (rn > 0) flags |= GCS_RESULTSTR;
    if (cn > 0) flags |= GCS_COMPSTR | GCS_CURSORPOS | GCS_DELTASTART;
    else if (wasComposing) flags |= GCS_COMPSTR;   // 빈 조합으로 갱신 → 앱이 preedit 제거(확정/취소)
    if (flags) {
        WPARAM wp = (rn > 0) ? (WPARAM)result[rn-1] : 0;
        PushMsg(lpTransBuf, cap, &idx, WM_IME_COMPOSITION, wp, (LPARAM)flags);
    }
    if ((wasComposing || rn > 0) && !nowComposing)
        PushMsg(lpTransBuf, cap, &idx, WM_IME_ENDCOMPOSITION, 0, 0);

    // 확정 후 종료키 재주입(조합 제거 뒤 앱이 정상 처리: Enter=개행, 화살표=이동 등)
    if (passVK == VK_RETURN)   PushMsg(lpTransBuf, cap, &idx, WM_CHAR, L'\r', 0x001C0001);
    else if (passVK == VK_TAB) PushMsg(lpTransBuf, cap, &idx, WM_CHAR, L'\t', 0x000F0001);
    else if (passVK) {
        LPARAM lp = (LPARAM)(1u | (uScanCode << 16));
        PushMsg(lpTransBuf, cap, &idx, WM_KEYDOWN, passVK, lp);
        PushMsg(lpTransBuf, cap, &idx, WM_KEYUP,   passVK, lp | 0xC0000000u);
    }
    if (modeToggled)   // 한/영 모드 변경 통지(표시기 갱신)
        PushMsg(lpTransBuf, cap, &idx, WM_IME_NOTIFY, IMN_SETCONVERSIONMODE, 0);

    ImmUnlockIMCC(pIC->hPrivate);
    ImmUnlockIMC(hIMC);
    return (idx <= cap) ? idx : cap;   // 버퍼 용량 초과 반환 금지(과다읽기 방어)
}

// hMsgBuf에 메시지를 이어붙인다(pIC 잠금 상태에서 호출). 실제 전달은 호출자가 잠금 해제 후
// ImmGenerateMessage로 한다(ImmGenerateMessage가 내부에서 IMC를 잠그므로 잠금 중 호출 시 교착).
static void Ime_QueueMessages(LPINPUTCONTEXT pIC, const TRANSMSG *src, DWORD n) {
    if (!pIC || n == 0) return;
    DWORD old = pIC->dwNumMsgBuf;
    DWORD need = (old + n) * sizeof(TRANSMSG);
    HIMCC h = pIC->hMsgBuf;
    if (!h) h = ImmCreateIMCC(need);
    else if (ImmGetIMCCSize(h) < need) h = ImmReSizeIMCC(h, need);
    if (!h) return;
    pIC->hMsgBuf = h;
    TRANSMSG *buf = (TRANSMSG*)ImmLockIMCC(h);
    if (buf) {
        memcpy(buf + old, src, n * sizeof(TRANSMSG));
        ImmUnlockIMCC(h);
        pIC->dwNumMsgBuf = old + n;
    }
}

// ── NotifyIME: 앱/시스템 통지 ────────────────────────────────────────────────────
BOOL WINAPI NotifyIME(HIMC hIMC, DWORD dwAction, DWORD dwIndex, DWORD dwValue) {
    (void)dwValue;
    if (!hIMC) return FALSE;
    // 조합 확정(CPS_COMPLETE)/취소(CPS_CANCEL) — 포커스 이탈 등. 현재 음절을 제대로 확정/제거한다
    // (예전엔 Fsm_Init만 해 음절이 유실되고 preedit가 남을 수 있었음). ImmGenerateMessage로 전달.
    if (dwAction == NI_COMPOSITIONSTR && (dwIndex == CPS_COMPLETE || dwIndex == CPS_CANCEL)) {
        LPINPUTCONTEXT pIC = ImmLockIMC(hIMC);
        if (!pIC) return TRUE;
        ImePrivate *priv = pIC->hPrivate ? (ImePrivate*)ImmLockIMCC(pIC->hPrivate) : NULL;
        bool wasComposing = priv && priv->fsm.state != STATE_EMPTY;
        wchar_t result[8]; DWORD rn = 0;
        if (priv) {
            if (dwIndex == CPS_COMPLETE && wasComposing) { wchar_t last = Fsm_Flush(&priv->fsm); if (last) result[rn++] = last; }
            else Fsm_Init(&priv->fsm);   // CPS_CANCEL: 버림
        }
        bool posted = false;
        if (wasComposing) {
            HIMCC nc = BuildCompStr(pIC->hCompStr, NULL, 0, result, rn);
            if (nc) pIC->hCompStr = nc;
            TRANSMSG msgs[2]; DWORD n = 0;
            msgs[n].message = WM_IME_COMPOSITION;
            msgs[n].wParam  = (rn > 0) ? (WPARAM)result[rn-1] : 0;
            msgs[n].lParam  = (rn > 0) ? GCS_RESULTSTR : GCS_COMPSTR;   // COMPLETE→결과, CANCEL→빈 조합
            n++;
            msgs[n].message = WM_IME_ENDCOMPOSITION; msgs[n].wParam = 0; msgs[n].lParam = 0; n++;
            Ime_QueueMessages(pIC, msgs, n);
            posted = true;
        }
        if (priv) ImmUnlockIMCC(pIC->hPrivate);
        ImmUnlockIMC(hIMC);
        if (posted) ImmGenerateMessage(hIMC);   // 잠금 해제 후(재진입 안전)
        return TRUE;
    }
    return TRUE;   // 기타 통지는 수용
}
