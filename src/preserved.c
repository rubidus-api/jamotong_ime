#include "preserved.h"
#include "preserved_map.h"

void JamoDiag(const char *fmt, ...);   // dllmain.c (JAMO_DIAG 빌드에서만 실동작)

// 명령 GUID 기반: {7A3D5E10-9C42-4B8A-8E31-006A0D170000} 의 Data4[6]=기능, Data4[7]=슬롯.
// (제품 전용 임의 GUID — lab·프로필·표시속성과 겹치지 않음)
static GUID CmdGuid(int fn, int slot) {
    GUID g = { 0x7a3d5e10, 0x9c42, 0x4b8a, { 0x8e, 0x31, 0x00, 0x6a, 0x0d, 0x17, 0x00, 0x00 } };
    g.Data4[6] = (unsigned char)fn;
    g.Data4[7] = (unsigned char)slot;
    return g;
}

// 사용자 UI(단축키 목록)에 노출될 수 있는 설명 — 기능당 하나(영어, README 용어와 일치)
static const wchar_t *FnDesc(int fn) {
    switch (fn) {
        case SC_FN_ROTATE:      return L"Jamotong: switch layout (Kor/Eng)";
        case SC_FN_PASSTHROUGH: return L"Jamotong: pass-through (direct input)";
        case SC_FN_SETTINGS:    return L"Jamotong: open settings";
        case SC_FN_CODE:        return L"Jamotong: Unicode codepoint input";
        default:                return L"Jamotong command";
    }
}

// 문맥 무관 명령만 (RFC-0013 §2C — 한자는 문맥 의존이라 제외)
static const int kFns[] = { SC_FN_ROTATE, SC_FN_PASSTHROUGH, SC_FN_SETTINGS, SC_FN_CODE };

void Preserved_Register(JamotongTextService *obj) {
    obj->preservedCount = 0;

    bool on;
    EnterCriticalSection(&g_configLock);
    on = obj->config.options.usePreservedKeys;
    // 단축키 표 스냅숏 (락 밖에서 COM 호출하기 위해)
    struct { int fn; UINT vKey; UINT mods; } snap[JAMO_PRESERVED_MAX];
    int nsnap = 0;
    if (on) {
        for (size_t f = 0; f < sizeof kFns / sizeof kFns[0]; f++) {
            const ShortcutList *sl = &obj->config.shortcuts[kFns[f]];
            for (int i = 0; i < sl->count && nsnap < JAMO_PRESERVED_MAX; i++) {
                snap[nsnap].fn = kFns[f];
                snap[nsnap].vKey = sl->keys[i].vKey;
                snap[nsnap].mods = sl->keys[i].mods;
                nsnap++;
            }
        }
    }
    LeaveCriticalSection(&g_configLock);
    if (!on || !obj->threadMgr) return;

    ITfKeystrokeMgr *km = NULL;
    if (FAILED(obj->threadMgr->lpVtbl->QueryInterface(obj->threadMgr, &IID_ITfKeystrokeMgr, (void**)&km)) || !km)
        return;

    int slotPerFn[SC_FN_COUNT] = {0};
    for (int i = 0; i < nsnap; i++) {
        unsigned uVKey = 0, uMods = 0;
        if (!PreservedMap_Build(snap[i].vKey, snap[i].mods, &uVKey, &uMods))
            continue;   // GUI 조합·맨 모디파이어 → key sink 폴백 (등록 안 함)
        int fn = snap[i].fn;
        JamoPreservedEntry *e = &obj->preserved[obj->preservedCount];
        e->guid = CmdGuid(fn, slotPerFn[fn]++);
        e->fn = fn;
        e->key.uVKey = uVKey;
        e->key.uModifiers = uMods;
        const wchar_t *desc = FnDesc(fn);
        HRESULT hr = km->lpVtbl->PreserveKey(km, obj->clientId, &e->guid, &e->key,
                                             desc, (ULONG)wcslen(desc));
        JamoDiag("PRESERVE fn=%d vk=0x%02X mod=0x%X hr=0x%08lX", fn, uVKey, uMods, (unsigned long)hr);
        if (SUCCEEDED(hr)) obj->preservedCount++;
        // 실패(TF_E_ALREADY_EXISTS 등)해도 계속 — 그 키는 sink 폴백이 받는다
    }
    km->lpVtbl->Release(km);
}

void Preserved_Unregister(JamotongTextService *obj) {
    if (obj->preservedCount <= 0 || !obj->threadMgr) { obj->preservedCount = 0; return; }
    ITfKeystrokeMgr *km = NULL;
    if (SUCCEEDED(obj->threadMgr->lpVtbl->QueryInterface(obj->threadMgr, &IID_ITfKeystrokeMgr, (void**)&km)) && km) {
        for (int i = 0; i < obj->preservedCount; i++)
            km->lpVtbl->UnpreserveKey(km, &obj->preserved[i].guid, &obj->preserved[i].key);
        km->lpVtbl->Release(km);
    }
    obj->preservedCount = 0;
}

int Preserved_Lookup(const JamotongTextService *obj, const GUID *rguid) {
    for (int i = 0; i < obj->preservedCount; i++)
        if (IsEqualGUID(rguid, &obj->preserved[i].guid)) return obj->preserved[i].fn;
    return -1;
}
