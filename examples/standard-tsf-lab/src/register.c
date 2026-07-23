/* register.c — 실험체 전용 등록 (RFC-0009 §3)
 *
 * 실험체 CLSID/profile/category만 등록·해제한다. 제품 등록은 건드리지 않는다.
 */
#include "lab_tip.h"
#include <stdio.h>

/* MinGW-w64의 msctf.h는 카테고리 GUID를 일부만 준다. 필요한 것을 직접 정의한다.
   값은 Microsoft TSF 문서/SDK ctfutb.h·msctf.h 공개 상수와 같다. */
static const GUID kCatTipCapUiElementEnabled =
    { 0x49d2f9ce, 0x1f5e, 0x11d7, { 0xa6, 0xd3, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c } };
static const GUID kCatTipCapSecureMode =
    { 0x49d2f9cf, 0x1f5e, 0x11d7, { 0xa6, 0xd3, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c } };
static const GUID kCatTipCapImmersiveSupport =
    { 0x13a016df, 0x560b, 0x46f6, { 0x89, 0x69, 0x7a, 0x28, 0x0c, 0x6b, 0xfb, 0x3a } };

static HRESULT RegisterComServer(void) {
    WCHAR path[MAX_PATH], key[160], gs[64];
    if (!GetModuleFileNameW(g_lab_instance, path, MAX_PATH)) return E_FAIL;
    StringFromGUID2(&CLSID_LabService, gs, 64);

    HKEY hk = NULL, hs = NULL;
    swprintf(key, 160, L"CLSID\\%s", gs);
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, key, 0, NULL, 0, KEY_WRITE, NULL, &hk, NULL))
        return E_ACCESSDENIED;
    RegSetValueExW(hk, NULL, 0, REG_SZ, (const BYTE*)LAB_DISPLAY_NAME,
                   (DWORD)((wcslen(LAB_DISPLAY_NAME)+1)*sizeof(WCHAR)));
    if (RegCreateKeyExW(hk, L"InprocServer32", 0, NULL, 0, KEY_WRITE, NULL, &hs, NULL) == 0) {
        RegSetValueExW(hs, NULL, 0, REG_SZ, (const BYTE*)path,
                       (DWORD)((wcslen(path)+1)*sizeof(WCHAR)));
        RegSetValueExW(hs, L"ThreadingModel", 0, REG_SZ,
                       (const BYTE*)L"Apartment", sizeof(L"Apartment"));
        RegCloseKey(hs);
    }
    RegCloseKey(hk);
    return S_OK;
}

static void UnregisterComServer(void) {
    WCHAR key[160], gs[64];
    StringFromGUID2(&CLSID_LabService, gs, 64);
    swprintf(key, 160, L"CLSID\\%s\\InprocServer32", gs);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
    swprintf(key, 160, L"CLSID\\%s", gs);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
}

HRESULT Lab_RegisterServer(void) {
    WCHAR path[MAX_PATH];
    if (!GetModuleFileNameW(g_lab_instance, path, MAX_PATH)) return E_FAIL;
    HRESULT hr = RegisterComServer();
    if (FAILED(hr)) return hr;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool did_init = SUCCEEDED(hr);

    ITfInputProcessorProfiles *profiles = NULL;
    hr = CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITfInputProcessorProfiles, (void**)&profiles);
    if (SUCCEEDED(hr)) {
        hr = ITfInputProcessorProfiles_Register(profiles, &CLSID_LabService);
        if (SUCCEEDED(hr)) {
            hr = ITfInputProcessorProfiles_AddLanguageProfile(
                    profiles, &CLSID_LabService, LAB_LANGID, &GUID_LabProfile,
                    LAB_DISPLAY_NAME, (ULONG)wcslen(LAB_DISPLAY_NAME),
                    path, (ULONG)wcslen(path), 0);
        }
        ITfInputProcessorProfiles_Release(profiles);
    }

    /* 카테고리를 빠뜨리면 목록에는 떠도 활성화되지 않는다 */
    ITfCategoryMgr *cat = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_ITfCategoryMgr, (void**)&cat))) {
        ITfCategoryMgr_RegisterCategory(cat, &CLSID_LabService,
                                        &GUID_TFCAT_TIP_KEYBOARD, &CLSID_LabService);
        /* 표준 composition을 쓰겠다고 선언한다 — CUAS가 이 선언을 본다 */
        ITfCategoryMgr_RegisterCategory(cat, &CLSID_LabService,
                                        &kCatTipCapUiElementEnabled, &CLSID_LabService);
        ITfCategoryMgr_RegisterCategory(cat, &CLSID_LabService,
                                        &kCatTipCapSecureMode, &CLSID_LabService);
        /* 스토어 앱(app container)에서도 로드되게 한다 */
        ITfCategoryMgr_RegisterCategory(cat, &CLSID_LabService,
                                        &kCatTipCapImmersiveSupport, &CLSID_LabService);
        /* 표시 속성 제공자 — 이게 없으면 앱이 밑줄 모양을 물어보지 않는다 */
        ITfCategoryMgr_RegisterCategory(cat, &CLSID_LabService,
                                        &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, &CLSID_LabService);
        ITfCategoryMgr_Release(cat);
    }
    if (did_init) CoUninitialize();
    return hr;
}

HRESULT Lab_UnregisterServer(void) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool did_init = SUCCEEDED(hr);

    ITfCategoryMgr *cat = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_ITfCategoryMgr, (void**)&cat))) {
        ITfCategoryMgr_UnregisterCategory(cat, &CLSID_LabService,
                                          &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, &CLSID_LabService);
        ITfCategoryMgr_UnregisterCategory(cat, &CLSID_LabService,
                                          &kCatTipCapImmersiveSupport, &CLSID_LabService);
        ITfCategoryMgr_UnregisterCategory(cat, &CLSID_LabService,
                                          &kCatTipCapSecureMode, &CLSID_LabService);
        ITfCategoryMgr_UnregisterCategory(cat, &CLSID_LabService,
                                          &kCatTipCapUiElementEnabled, &CLSID_LabService);
        ITfCategoryMgr_UnregisterCategory(cat, &CLSID_LabService,
                                          &GUID_TFCAT_TIP_KEYBOARD, &CLSID_LabService);
        ITfCategoryMgr_Release(cat);
    }
    ITfInputProcessorProfiles *profiles = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_ITfInputProcessorProfiles, (void**)&profiles))) {
        ITfInputProcessorProfiles_RemoveLanguageProfile(profiles, &CLSID_LabService,
                                                        LAB_LANGID, &GUID_LabProfile);
        ITfInputProcessorProfiles_Unregister(profiles, &CLSID_LabService);
        ITfInputProcessorProfiles_Release(profiles);
    }
    if (did_init) CoUninitialize();
    UnregisterComServer();
    return S_OK;
}
