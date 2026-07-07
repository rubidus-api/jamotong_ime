#include "jamotong.h"

static const WCHAR c_szProfileDesc[] = L"Jamotong IME";
static const LANGID c_langIdKorean = MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN);

// 모던 Windows(Store/UWP 앱·시스템 트레이) 지원 카테고리. 이 MinGW의 msctf.h엔 없어 직접 정의한다.
// 미등록 시 IME가 데스크톱 앱(메모장 등)에선 되지만 Store/UWP 앱과 Win10/11 모던 입력 전환에서
// 나타나지 않거나 동작하지 않는다(잘 알려진 TSF 함정).
// 플레인 const로 정의(DEFINE_GUID는 INITGUID 없이는 선언만 → 링크 에러).
static const GUID GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT_J = { 0x13a016df, 0x560b, 0x46cd, { 0x94, 0x7a, 0x4c, 0x3a, 0xf1, 0xe0, 0xe3, 0x5d } };
static const GUID GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT_J   = { 0x25504fb4, 0x7bab, 0x4bc1, { 0x9c, 0x69, 0xcf, 0x81, 0x89, 0x0f, 0x0e, 0xf5 } };

extern HINSTANCE g_hInst;   // dllmain.c — 프로파일 아이콘을 이 DLL에서 추출하기 위한 모듈 핸들

#define PROFILE_ICON_RESID 100   // src/jamotong.rc 의 아이콘 리소스 ID

HRESULT RegisterProfiles(void) {
    // 모던 API(ITfInputProcessorProfileMgr::RegisterProfile)로 등록해야 Win11 트레이 입력표시기가
    // 우리 브랜딩 아이콘("자모통")을 표시한다(구형 AddLanguageProfile은 표시기가 아이콘을 못 집음).
    // 아이콘 인덱스는 '음수 = 리소스 ID' 규약(SampleIME도 -IDIS_SAMPLEIME 전달) → -100.
    ITfInputProcessorProfiles *pProfiles = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER, &IID_ITfInputProcessorProfiles, (void**)&pProfiles);
    if (FAILED(hr)) return hr;

    ITfInputProcessorProfileMgr *pMgr = NULL;
    hr = pProfiles->lpVtbl->QueryInterface(pProfiles, &IID_ITfInputProcessorProfileMgr, (void**)&pMgr);
    if (SUCCEEDED(hr) && pMgr) {
        WCHAR szDll[MAX_PATH] = {0};
        DWORD n = GetModuleFileNameW(g_hInst, szDll, MAX_PATH);
        hr = pMgr->lpVtbl->RegisterProfile(pMgr, &CLSID_JamotongIME, c_langIdKorean, &GUID_Profile_Jamotong,
                                           c_szProfileDesc, (ULONG)wcslen(c_szProfileDesc),
                                           (n > 0) ? szDll : NULL, (n > 0) ? n : 0,
                                           (ULONG)(-PROFILE_ICON_RESID),   // 음수 = 리소스 ID
                                           NULL, 0, TRUE, 0);
        pMgr->lpVtbl->Release(pMgr);
    } else {
        // 폴백: 구형 API
        WCHAR szDll[MAX_PATH] = {0};
        DWORD n = GetModuleFileNameW(g_hInst, szDll, MAX_PATH);
        if (SUCCEEDED(pProfiles->lpVtbl->Register(pProfiles, &CLSID_JamotongIME)))
            hr = pProfiles->lpVtbl->AddLanguageProfile(pProfiles, &CLSID_JamotongIME, c_langIdKorean, &GUID_Profile_Jamotong,
                                                       c_szProfileDesc, (ULONG)wcslen(c_szProfileDesc),
                                                       (n > 0) ? szDll : NULL, (n > 0) ? n : 0, (ULONG)(-PROFILE_ICON_RESID));
    }

    pProfiles->lpVtbl->Release(pProfiles);
    return hr;
}

HRESULT UnregisterProfiles(void) {
    ITfInputProcessorProfiles *pProfiles;
    HRESULT hr = CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER, &IID_ITfInputProcessorProfiles, (void**)&pProfiles);
    if (FAILED(hr)) return hr;

    hr = pProfiles->lpVtbl->Unregister(pProfiles, &CLSID_JamotongIME);

    pProfiles->lpVtbl->Release(pProfiles);
    return hr;
}

HRESULT RegisterCategories(void) {
    ITfCategoryMgr *pCategoryMgr;
    HRESULT hr = CoCreateInstance(&CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, &IID_ITfCategoryMgr, (void**)&pCategoryMgr);
    if (FAILED(hr)) return hr;

    hr = pCategoryMgr->lpVtbl->RegisterCategory(pCategoryMgr, &CLSID_JamotongIME, &GUID_TFCAT_TIP_KEYBOARD, &CLSID_JamotongIME);
    if (SUCCEEDED(hr)) hr = pCategoryMgr->lpVtbl->RegisterCategory(pCategoryMgr, &CLSID_JamotongIME, &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, &CLSID_JamotongIME);
    // 모던 앱/시스템 트레이 지원 (Win8+)
    if (SUCCEEDED(hr)) hr = pCategoryMgr->lpVtbl->RegisterCategory(pCategoryMgr, &CLSID_JamotongIME, &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT_J, &CLSID_JamotongIME);
    if (SUCCEEDED(hr)) hr = pCategoryMgr->lpVtbl->RegisterCategory(pCategoryMgr, &CLSID_JamotongIME, &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT_J, &CLSID_JamotongIME);

    pCategoryMgr->lpVtbl->Release(pCategoryMgr);
    return hr;
}

HRESULT UnregisterCategories(void) {
    ITfCategoryMgr *pCategoryMgr;
    HRESULT hr = CoCreateInstance(&CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, &IID_ITfCategoryMgr, (void**)&pCategoryMgr);
    if (FAILED(hr)) return hr;

    hr = pCategoryMgr->lpVtbl->UnregisterCategory(pCategoryMgr, &CLSID_JamotongIME, &GUID_TFCAT_TIP_KEYBOARD, &CLSID_JamotongIME);
    hr = pCategoryMgr->lpVtbl->UnregisterCategory(pCategoryMgr, &CLSID_JamotongIME, &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, &CLSID_JamotongIME);
    hr = pCategoryMgr->lpVtbl->UnregisterCategory(pCategoryMgr, &CLSID_JamotongIME, &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT_J, &CLSID_JamotongIME);
    hr = pCategoryMgr->lpVtbl->UnregisterCategory(pCategoryMgr, &CLSID_JamotongIME, &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT_J, &CLSID_JamotongIME);

    pCategoryMgr->lpVtbl->Release(pCategoryMgr);
    return S_OK;
}
