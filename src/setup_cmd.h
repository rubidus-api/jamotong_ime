#pragma once
#include <windows.h>

// ── 설치기가 부르는 동사 — `jamotong.exe --register` / `--unregister` (RFC-0019) ──────
// 등록의 뜻은 여기 한 곳에만 있다. MSI 는 지연·비가장 커스텀 액션으로 이 exe 를 부르고,
// 설치기 없는 zip 에서는 관리자가 직접 부른다 — 등록 경로는 이것 하나다.
//   · 먼저 설치 폴더를 본다(RFC-0019 §7): 폴더는 사용자가 고르므로, 그 위 폴더를 관리자 아닌 누가
//     바꿀 수 있으면 거절하고, 통과하면 설치 폴더를 관리자 전용 쓰기로 잠근다. 해제는 보지 않는다.
//   · 64비트: 옆에 있는 `jamotong.dll` 의 DllRegisterServer/DllUnregisterServer 를 부른다.
//   · 32비트: 같은 폴더의 `jamotong32.dll` 을 SysWOW64 의 regsvr32 로 등록한다(다른 비트다).
//   · 등록 뒤 설치 폴더와 자판 폴더의 자판 원본을 굽는다 — 입력기는 구운 자판만 읽는다.
// 승격되지 않았으면 HKLM 에 쓸 수 없으므로 **하기 전에** 그 사실을 적고 실패로 끝낸다.
// 돌려주는 값: 0 성공, 8 폴더 거절, 그 밖은 실패(MSI 는 0 아닌 값을 받으면 설치를 되돌린다).
int Setup_Register(bool unregister);
