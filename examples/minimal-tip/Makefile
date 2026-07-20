# 최소 TSF IME 예제 — MinGW-w64 크로스 빌드
#   make        : minimal.dll   (x64)
#   make win32  : minimal32.dll (x86 — 32비트 앱에서 쓰려면 이것도 등록)
CC   = x86_64-w64-mingw32-gcc
CC32 = i686-w64-mingw32-gcc
CFLAGS  = -Wall -Wextra -std=c2x -D_UNICODE -DUNICODE -O2
LDFLAGS = -shared -static -static-libgcc -lole32 -loleaut32 -luuid

all: minimal.dll
minimal.dll: minimal.c minimal.def
	$(CC) $(CFLAGS) -o $@ minimal.c minimal.def $(LDFLAGS)
win32: minimal.c minimal.def
	$(CC32) $(CFLAGS) -o minimal32.dll minimal.c minimal.def $(LDFLAGS) -Wl,--enable-stdcall-fixup
clean:
	rm -f minimal.dll minimal32.dll
.PHONY: all win32 clean
