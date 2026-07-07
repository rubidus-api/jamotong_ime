#pragma once
#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JAMOTONG_PLUGIN_API_VERSION 1

// 플러그인이 키 처리 후 반환하는 값 기반 구조체
typedef struct {
    HRESULT hr;                            // 처리 결과 (S_OK 등)
    BOOL bEaten;                           // 호스트 앱(메모장 등)으로 이 키를 넘길지 여부
    wchar_t wszComposing[64];              // 현재 밑줄과 함께 조합 중인 텍스트
    wchar_t wszCommitted[64];              // 최종적으로 확정되어 출력될 텍스트
} JAMOTONG_PLUGIN_RESULT;

// 메인 입력기(Jamotong)가 플러그인에 제공하는 호스트 콜백 포인터 모음
typedef struct {
    void* hHostSession; // Jamotong 내부 세션 식별자
    void* reserved1; 
    void* reserved2;
} JAMOTONG_HOST_CALLBACKS;

// -----------------------------------------------------------------------------
// Plugin Export Functions Type Definitions
// -----------------------------------------------------------------------------

// 1. 초기화
typedef HRESULT (WINAPI *PFN_JamoPlugin_Initialize)(
    int nVersion, 
    const JAMOTONG_HOST_CALLBACKS* pCallbacks, 
    void** ppvContext
);

// 2. 키 입력 처리
typedef JAMOTONG_PLUGIN_RESULT (WINAPI *PFN_JamoPlugin_ProcessKey)(
    void* pvContext, 
    WPARAM wParam, 
    LPARAM lParam, 
    const BYTE* pbKeyState, 
    const RECT* prcCursor
);

// 3. 강제 초기화 (조합 취소/확정)
typedef JAMOTONG_PLUGIN_RESULT (WINAPI *PFN_JamoPlugin_Flush)(
    void* pvContext
);

// 4. 유연한 확장 통신
typedef HRESULT (WINAPI *PFN_JamoPlugin_Command)(
    void* pvContext, 
    UINT uCmd, 
    void* pData
);

// 5. 종료
typedef HRESULT (WINAPI *PFN_JamoPlugin_Uninitialize)(
    void* pvContext
);

#ifdef __cplusplus
}
#endif
