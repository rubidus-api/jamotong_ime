// disk_version.h — 지금 도는 코드가 디스크에 새로 놓인 파일과 다른지 (0.69.1).
//
// 설치기는 앱을 닫지 않는다(오너 2026-09-28, Restart Manager 끔): 쓰이던 DLL·exe 는 옆으로 치워지고(Config.Msi 의 .rbf)
// 새 파일이 제자리에 놓인다. 그래서 업그레이드 전부터 떠 있던 프로세스(작업 표시줄의 explorer, 열린 앱, UI 헬퍼)는 다시
// 열 때까지 옛 코드를 돈다. 실린 이미지가 매핑된 파일의 지금 이름이 모듈의 경로와 다르면 바뀐 것이다.
//   (판 자원을 DATAFILE 로 열어 견주는 길은 쓰지 않는다 — 이미 실린 모듈과 같은 경로면 Windows 가 실린 그 모듈을 돌려줘
//   자기 판을 읽는다. VM 2026-10-03.)
#pragma once
#include <windows.h>
#include <stdbool.h>
#include <stddef.h>

// mod(NULL = exe) 의 파일이 실린 뒤 치워지고 다른 파일이 그 자리에 놓였으면 true. 알 수 없으면 false.
bool DiskVersion_IsStale(HMODULE mod);

// About 글에 덧붙일 말: 바뀌었으면 "새 판이 깔렸다 — 다시 열면 그 판" (영어 한 문단, 앞에 빈 줄), 아니면 빈 문자열.
void DiskVersion_StaleNote(HMODULE mod, const wchar_t *what, wchar_t *out, size_t cap);
