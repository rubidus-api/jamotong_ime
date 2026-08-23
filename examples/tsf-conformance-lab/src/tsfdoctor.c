// tsf-conformance-lab / tsfdoctor.c
//
// "IME 로 전환이 안 된다 / 언어 트레이에 아이콘이 없다" 를 **추측 없이** 가르는 도구.
//
// 등록(regsvr32)과 "사용자 입력 목록에 들어가 전환 가능한 상태"는 **다른 것**이다.
// 등록은 기계 전체(HKCR/HKLM), 입력 목록은 **로그인한 사용자의 것**이다. 그래서
// 관리자 계정으로 등록하면 그 관리자의 목록에만 들어가고 실제 사용자에겐 안 보일 수 있다.
//
// 이 프로그램은 **관리자 권한 없이, 실제 사용하는 계정으로** 돌려야 한다.
//
// 사용법:
//   tsfdoctor.exe                 현재 상태 보고 (아무것도 바꾸지 않음)
//   tsfdoctor.exe --enable-jamotong   자모통 프로파일만 이 사용자에게 켠다 (권장)
//   tsfdoctor.exe --enable {CLSID}    지정 CLSID 의 프로파일만 켠다
//   tsfdoctor.exe --disable-foreign   한국어(0412)/영어(0409)/시스템(0xffff) 밖의 TIP 프로파일과,
//                                     0412 중 MS IME·한컴·자모통 외(옛한글 등)를 전부 끈다
//                                     (= 예전 --enable-all 이 켜 놓은 것을 되돌린다)
//   tsfdoctor.exe --activate N        보고서의 [N] 번 프로파일로 지금 전환
//
// ★ --enable-all 은 제거했다(2026-08-23): 모든 언어의 TIP 을 켜서 Win+Space 에 다 띄우는 사고를 냈다.
// 바꾸는 것은 오직 --enable-* / --disable-foreign / --activate 일 때뿐이다.

#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <msctf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>

// MinGW msctf.h 에 없는 수치 상수 (Windows SDK msctf.idl 에서 확인)
#ifndef TF_IPP_FLAG_ACTIVE
#define TF_IPP_FLAG_ACTIVE                      0x00000001
#define TF_IPP_FLAG_ENABLED                     0x00000002
#define TF_IPP_FLAG_SUBSTITUTEDBYINPUTPROCESSOR 0x00000004
#endif
#ifndef TF_IPPMF_FORPROCESS
#define TF_IPPMF_FORPROCESS                     0x10000000
#define TF_IPPMF_FORSESSION                     0x20000000
#define TF_IPPMF_FORSYSTEMALL                   0x40000000
#define TF_IPPMF_ENABLEPROFILE                  0x00000001
#define TF_IPPMF_DISABLEPROFILE                 0x00000002
#define TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE   0x00000004
#endif
#ifndef TF_PROFILETYPE_INPUTPROCESSOR
#define TF_PROFILETYPE_INPUTPROCESSOR 0x0001
#define TF_PROFILETYPE_KEYBOARDLAYOUT 0x0002
#endif

// 켜기/끄기 대상 판별용 CLSID (제품 소스와 공유하지 않는다 — 이 도구는 독립 실행 파일)
static const GUID CLSID_Jamotong_T = { 0xC471BCF2, 0x343F, 0x4187, { 0xA1, 0x03, 0x24, 0x15, 0x1C, 0x3E, 0x20, 0xB9 } };
static const GUID CLSID_MsImeKo_T  = { 0xA028AE76, 0x01B1, 0x46C2, { 0x99, 0xC4, 0xAC, 0xD9, 0x85, 0x8A, 0xE0, 0x2F } };
static const GUID CLSID_Hancom_T   = { 0x1BB25C39, 0xE526, 0x4F83, { 0xBD, 0xCB, 0x3F, 0xA5, 0xCA, 0xA7, 0xF8, 0xFE } };

#define MAX_PROFILES 128
static TF_INPUTPROCESSORPROFILE g_prof[MAX_PROFILES];
static int g_profCount;

static void guid_str(const GUID *g, WCHAR *out, int cch) { StringFromGUID2(g, out, cch); }

// CLSID 가 실제로 로드 가능한 DLL 을 가리키는지 — "등록은 남았는데 파일이 없다"가 흔한 원인.
static void check_inproc(const GUID *clsid, WCHAR *pathOut, int cch, BOOL *existsOut)
{
    WCHAR key[128], cls[64];
    HKEY hk = NULL;
    DWORD type = 0, cb = (DWORD)(cch * sizeof(WCHAR));

    pathOut[0] = 0;
    *existsOut = FALSE;

    guid_str(clsid, cls, 64);
    swprintf(key, 128, L"CLSID\\%s\\InProcServer32", cls);
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, key, 0, KEY_READ, &hk) != ERROR_SUCCESS) {
        wcscpy_s(pathOut, cch, L"(COM 등록 없음)");
        return;
    }
    if (RegQueryValueExW(hk, NULL, NULL, &type, (BYTE *)pathOut, &cb) == ERROR_SUCCESS) {
        pathOut[cch - 1] = 0;
        *existsOut = (GetFileAttributesW(pathOut) != INVALID_FILE_ATTRIBUTES);
    } else {
        wcscpy_s(pathOut, cch, L"(경로 값 없음)");
    }
    RegCloseKey(hk);
}

static void print_profile(int idx, const TF_INPUTPROCESSORPROFILE *p,
                          ITfInputProcessorProfiles *pp)
{
    WCHAR cls[64], prof[64], path[MAX_PATH];
    BOOL exists = FALSE;
    BSTR desc = NULL;

    guid_str(&p->clsid, cls, 64);
    guid_str(&p->guidProfile, prof, 64);

    const char *kind = (p->dwProfileType == TF_PROFILETYPE_KEYBOARDLAYOUT)
                     ? "키보드배열" : "TIP(IME)";

    wprintf(L"[%d] %S  langid=0x%04X\n", idx, kind, (unsigned)p->langid);

    if (pp && p->dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR) {
        if (SUCCEEDED(ITfInputProcessorProfiles_GetLanguageProfileDescription(
                pp, &p->clsid, p->langid, &p->guidProfile, &desc)) && desc) {
            wprintf(L"     이름   : %s\n", desc);
            SysFreeString(desc);
        }
    }
    wprintf(L"     CLSID  : %s\n", cls);
    if (p->dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR)
        wprintf(L"     Profile: %s\n", prof);

    wprintf(L"     상태   : %s%s%s (flags=0x%08lX)\n",
            (p->dwFlags & TF_IPP_FLAG_ENABLED) ? L"사용가능 " : L"**사용안함** ",
            (p->dwFlags & TF_IPP_FLAG_ACTIVE)  ? L"현재활성 " : L"",
            (p->dwFlags & TF_IPP_FLAG_SUBSTITUTEDBYINPUTPROCESSOR) ? L"대체됨 " : L"",
            (unsigned long)p->dwFlags);

    if (p->dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR) {
        check_inproc(&p->clsid, path, MAX_PATH, &exists);
        wprintf(L"     DLL    : %s  %s\n", path, exists ? L"[있음]" : L"[★파일 없음]");
    }
    wprintf(L"\n");
}

static void report_env(void)
{
    wprintf(L"== 환경 ==\n");

    // ctfmon(입력 서비스)이 살아 있는가 — 죽어 있으면 전환도 표시기도 없다.
    // 프로세스 열거 대신 CTF 창 존재로 싸게 본다.
    HWND h = FindWindowW(L"CicMarshalWndClass", NULL);
    wprintf(L"  CTF 마샬 창(ctfmon 생존 지표) : %s\n",
            h ? L"있음" : L"★없음 — 입력 서비스(ctfmon.exe)가 안 돌고 있을 수 있음");

    // 현재 스레드의 키보드 레이아웃/HKL
    HKL hkl = GetKeyboardLayout(0);
    wprintf(L"  현재 스레드 HKL              : 0x%p\n", (void *)hkl);

    // TSF 가 이 프로세스에서 서는가
    ITfThreadMgr *tm = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_ITfThreadMgr, (void **)&tm);
    wprintf(L"  ITfThreadMgr 생성            : hr=0x%08lX %s\n",
            (unsigned long)hr, SUCCEEDED(hr) ? L"OK" : L"★실패");
    if (tm) ITfThreadMgr_Release(tm);

    // 이 프로세스가 승격되어 있으면 사용자 목록이 다를 수 있다 — 그 자체가 원인일 수 있다.
    {
        HANDLE tok = NULL;
        TOKEN_ELEVATION el = { 0 };
        DWORD cb = sizeof el;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
            if (GetTokenInformation(tok, TokenElevation, &el, cb, &cb))
                wprintf(L"  이 프로그램의 권한           : %s\n",
                        el.TokenIsElevated ? L"★관리자(승격됨) — 일반 사용자 계정으로 다시 실행하세요"
                                           : L"일반 사용자 (올바름)");
            CloseHandle(tok);
        }
    }
    wprintf(L"\n");
}

int wmain(int argc, wchar_t **argv)
{
    HRESULT hr;
    ITfInputProcessorProfiles *pp = NULL;
    ITfInputProcessorProfileMgr *mgr = NULL;
    IEnumTfInputProcessorProfiles *en = NULL;
    BOOL doEnableJamotong = FALSE, doDisableForeign = FALSE;
    GUID enableClsid; BOOL haveEnableClsid = FALSE;
    int activateIdx = -1;

    _setmode(_fileno(stdout), _O_U16TEXT);

    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--enable-jamotong")) doEnableJamotong = TRUE;
        else if (!wcscmp(argv[i], L"--enable") && i + 1 < argc) { if (SUCCEEDED(CLSIDFromString(argv[++i], &enableClsid))) haveEnableClsid = TRUE; }
        else if (!wcscmp(argv[i], L"--disable-foreign")) doDisableForeign = TRUE;
        else if (!wcscmp(argv[i], L"--activate") && i + 1 < argc) activateIdx = _wtoi(argv[++i]);
        else if (!wcscmp(argv[i], L"--enable-all")) { wprintf(L"--enable-all 은 제거되었습니다(모든 언어 TIP 을 켜는 사고). --enable-jamotong 을 쓰세요.\n"); }
    }

    wprintf(L"================================================================\n");
    wprintf(L" tsfdoctor — 입력기(TIP) 프로파일 진단\n");
    wprintf(L"================================================================\n\n");

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) { wprintf(L"CoInitializeEx 실패 0x%08lX\n", (unsigned long)hr); return 1; }

    report_env();

    hr = CoCreateInstance(&CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITfInputProcessorProfiles, (void **)&pp);
    if (FAILED(hr) || !pp) {
        wprintf(L"★ InputProcessorProfiles 를 만들 수 없습니다 (0x%08lX).\n"
                L"   TSF 자체가 이 세션에서 동작하지 않는 상태입니다.\n", (unsigned long)hr);
        CoUninitialize();
        return 1;
    }

    hr = ITfInputProcessorProfiles_QueryInterface(pp, &IID_ITfInputProcessorProfileMgr,
                                                  (void **)&mgr);
    if (FAILED(hr) || !mgr) {
        wprintf(L"★ ITfInputProcessorProfileMgr 없음 (0x%08lX) — Windows 8 미만?\n",
                (unsigned long)hr);
        ITfInputProcessorProfiles_Release(pp);
        CoUninitialize();
        return 1;
    }

    // langid 0 = 모든 언어
    hr = ITfInputProcessorProfileMgr_EnumProfiles(mgr, 0, &en);
    if (FAILED(hr) || !en) {
        wprintf(L"★ EnumProfiles 실패 0x%08lX\n", (unsigned long)hr);
        goto done;
    }

    wprintf(L"== 이 사용자 계정의 입력 프로파일 목록 ==\n\n");
    for (;;) {
        TF_INPUTPROCESSORPROFILE buf[8];
        ULONG got = 0;
        hr = IEnumTfInputProcessorProfiles_Next(en, 8, buf, &got);
        if (FAILED(hr) || got == 0) break;
        for (ULONG i = 0; i < got && g_profCount < MAX_PROFILES; i++)
            g_prof[g_profCount++] = buf[i];
        if (hr == S_FALSE) break;
    }
    IEnumTfInputProcessorProfiles_Release(en);

    for (int i = 0; i < g_profCount; i++) print_profile(i, &g_prof[i], pp);

    {
        int tips = 0, tipsEnabled = 0;
        for (int i = 0; i < g_profCount; i++) {
            if (g_prof[i].dwProfileType != TF_PROFILETYPE_INPUTPROCESSOR) continue;
            tips++;
            if (g_prof[i].dwFlags & TF_IPP_FLAG_ENABLED) tipsEnabled++;
        }
        wprintf(L"== 요약 ==\n");
        wprintf(L"  전체 프로파일 %d개 / TIP(IME) %d개 / 그중 사용가능 %d개\n\n",
                g_profCount, tips, tipsEnabled);

        if (tips == 0)
            wprintf(L"  ★ 이 계정에 등록된 TIP 이 하나도 없습니다.\n"
                    L"    regsvr32 를 **다른 계정(관리자)** 으로 돌렸을 때 이렇게 됩니다.\n");
        else if (tipsEnabled == 0)
            wprintf(L"  ★ TIP 은 있는데 전부 '사용안함' 입니다 → 전환 목록에 안 나옵니다.\n"
                    L"    tsfdoctor.exe --enable-all 로 켤 수 있습니다.\n");
    }

    if (doEnableJamotong || haveEnableClsid) {
        const GUID *want = doEnableJamotong ? &CLSID_Jamotong_T : &enableClsid;
        wprintf(L"\n== 켜기: %s ==\n", doEnableJamotong ? L"Jamotong IME 만" : L"지정 CLSID 만");
        int n = 0;
        for (int i = 0; i < g_profCount; i++) {
            TF_INPUTPROCESSORPROFILE *p = &g_prof[i];
            if (p->dwProfileType != TF_PROFILETYPE_INPUTPROCESSOR) continue;
            if (!IsEqualGUID(&p->clsid, want)) continue;
            n++;
            if (p->dwFlags & TF_IPP_FLAG_ENABLED) { wprintf(L"  [%d] 이미 사용가능\n", i); continue; }
            hr = ITfInputProcessorProfileMgr_ActivateProfile(
                     mgr, TF_PROFILETYPE_INPUTPROCESSOR, p->langid, &p->clsid,
                     &p->guidProfile, NULL,
                     TF_IPPMF_ENABLEPROFILE | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
            wprintf(L"  [%d] 켜기 hr=0x%08lX %s\n", i, (unsigned long)hr, SUCCEEDED(hr) ? L"OK" : L"실패");
        }
        if (n == 0) wprintf(L"  해당 CLSID 의 프로파일이 등록되어 있지 않습니다 (install.bat 을 먼저).\n");
        wprintf(L"\n  Win+Space 로 확인하세요. 안 보이면 로그아웃 후 로그인.\n");
    }

    if (doDisableForeign) {
        wprintf(L"\n== --disable-foreign: 한국어/영어/시스템 밖 TIP 과 0412 의 MS IME·한컴·자모통 외 프로파일을 끕니다 ==\n");
        int off = 0;
        for (int i = 0; i < g_profCount; i++) {
            TF_INPUTPROCESSORPROFILE *p = &g_prof[i];
            if (p->dwProfileType != TF_PROFILETYPE_INPUTPROCESSOR) continue;
            if (!(p->dwFlags & TF_IPP_FLAG_ENABLED)) continue;
            if (p->langid == 0xFFFF) continue;                       // 시스템(잉크/음성/태블릿) 유지
            BOOL keep = FALSE;
            if (p->langid == 0x0412 && (IsEqualGUID(&p->clsid, &CLSID_MsImeKo_T) ||
                                        IsEqualGUID(&p->clsid, &CLSID_Hancom_T) ||
                                        IsEqualGUID(&p->clsid, &CLSID_Jamotong_T))) keep = TRUE;
            if (p->langid == 0x0409) keep = TRUE;                      // 영어권은 그대로
            if (keep) continue;
            // 정식 API: 사용자별 사용 플래그를 끈다 (등록 자체는 건드리지 않는다)
            hr = ITfInputProcessorProfiles_EnableLanguageProfile(pp, &p->clsid, p->langid, &p->guidProfile, FALSE);
            wprintf(L"  [%d] 끄기 langid=0x%04X hr=0x%08lX %s\n", i, (unsigned)p->langid, (unsigned long)hr, SUCCEEDED(hr) ? L"OK" : L"실패");
            off++;
        }
        wprintf(L"\n  %d개 껐습니다. 로그아웃 후 로그인하면 Win+Space 목록에서 사라집니다 (ChsIME/ChtIME 도 내려감).\n", off);
        wprintf(L"  그 뒤 Preload 가 다시 불어 있으면 5-ghost-ime\\3-preload-reset.bat.\n");
    }

    if (activateIdx >= 0 && activateIdx < g_profCount) {
        TF_INPUTPROCESSORPROFILE *p = &g_prof[activateIdx];
        wprintf(L"\n== --activate %d ==\n", activateIdx);
        hr = ITfInputProcessorProfileMgr_ActivateProfile(
                 mgr, p->dwProfileType, p->langid, &p->clsid, &p->guidProfile,
                 p->hkl, TF_IPPMF_ENABLEPROFILE | TF_IPPMF_FORSESSION);
        wprintf(L"  전환 hr=0x%08lX %s\n", (unsigned long)hr,
                SUCCEEDED(hr) ? L"OK — 이제 아무 앱에서 글자를 쳐 보세요" : L"실패");
    }

done:
    if (mgr) ITfInputProcessorProfileMgr_Release(mgr);
    if (pp) ITfInputProcessorProfiles_Release(pp);
    CoUninitialize();
    wprintf(L"\n(이 창은 닫아도 됩니다)\n");
    return 0;
}
