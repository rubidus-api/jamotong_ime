#pragma once
// klay_cli.h — .jmt 저작 도구 명령 (RFC-0011 P3). jamotong.exe 와 리눅스 네이티브 검사기가 같은 코드를 쓴다.
//   --check  <file> [--json]           검증만. 종료 코드 0 = 로드됨, 1 = 오류, 2 = 사용법
//   --export <@builtin|file> -o <out>  완전한 .jmt 로 저장 (Extends 는 펼친다 — D3)
//   --expand <file> -o <out>           Extends/Include 를 편 자립 파일
#include <wchar.h>
typedef void (*KlayCliOut)(const wchar_t *text, void *ctx);
int KlayCli_Run(int argc, const wchar_t *const *argv, KlayCliOut out, void *ctx);
// argv 에 저작 도구 명령이 있는가 (jamotong.exe 가 트레이 대신 CLI 로 갈지 정할 때)
int KlayCli_IsCommand(int argc, const wchar_t *const *argv);
