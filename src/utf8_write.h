#pragma once
// utf8_write.h — 와이드 글을 UTF-8 로 파일·버퍼에 적는다 (가져오기 도구가 쓰는 자판·사전 원본은 UTF-8 이다).
//   fprintf("%ls") 는 C 로케일에서 ASCII 밖 글자(한글·가나)를 못 바꿔 줄이 거기서 끊기고 '\n' 이 사라진다.
//   wchar_t 가 16비트(Windows, 서러게이트 쌍)든 32비트(호스트 시험)든 같게 동작한다.
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

// w 를 UTF-8 로 out 에 쓴다 (cap 안에서 글자 단위로 자르고, 항상 NUL 로 끝낸다). 짝 없는 서러게이트는 U+FFFD.
void Utf8_FromWide(const wchar_t *w, char *out, size_t cap);
// vswprintf 로 만든 한 줄(길이 제한 없음 — 버퍼를 키운다)을 UTF-8 로 f 에 쓴다. 다 썼으면 true, 아니면 false(호출부가 확인한다).
bool Utf8_Fprintf(FILE *f, const wchar_t *fmt, ...);
