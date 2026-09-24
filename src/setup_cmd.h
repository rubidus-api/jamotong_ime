#pragma once
#include <windows.h>

// ── 설치기가 부르는 동사 — `jamotong.exe --register` / `--unregister` (RFC-0019) ──────
// 등록의 뜻은 여기 한 곳에만 있다. MSI 는 지연·비가장 커스텀 액션으로 이 exe 를 부르고,
// `install.bat` 도 같은 경로를 쓰므로 두 설치기가 어긋나지 않는다.
//   · 64비트: 옆에 있는 `jamotong.dll` 의 DllRegisterServer/DllUnregisterServer 를 부른다.
//   · 32비트: 같은 폴더의 `jamotong32.dll` 을 SysWOW64 의 regsvr32 로 등록한다(다른 비트다).
//   · 등록 뒤 설치 폴더와 자판 폴더의 자판 원본을 굽는다 — 입력기는 구운 자판만 읽는다.
// 승격되지 않았으면 HKLM 에 쓸 수 없으므로 **하기 전에** 그 사실을 적고 실패로 끝낸다.
// 돌려주는 값: 0 성공, 그 밖은 실패(MSI 가 Return="check" 로 받아 되돌린다).
int Setup_Register(bool unregister);
