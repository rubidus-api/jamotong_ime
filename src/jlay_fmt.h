#pragma once
// jlay_fmt.h — 구운 자판 파일의 자리와 읽기/쓰기 원시 함수 (jlay.c 와 jlay_build.c 가 함께 쓴다).
//   한 곳에만 적어 두어, 쓰는 순서와 읽는 순서가 갈라지지 않게 한다.
#include <string.h>

#define JLAY_MAGIC       "JMTLAY\0\0"
#define JLAY_HEADER      64
#define JLAY_OFF_VERSION  8
#define JLAY_OFF_KIND    12
#define JLAY_OFF_BODY    16
#define JLAY_OFF_CRC     20
#define JLAY_OFF_SIZE    24
#define JLAY_OFF_FLAGS   28
#define JLAY_OFF_SRCSIZE 32
#define JLAY_OFF_MTIMEL  36
#define JLAY_OFF_MTIMEH  40
#define JLAY_OFF_BUILTBY 44

static inline unsigned JLayRd32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static inline unsigned JLayRd16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static inline void JLayWr32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}
static inline void JLayWr16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
