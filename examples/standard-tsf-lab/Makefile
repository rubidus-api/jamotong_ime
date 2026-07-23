# RFC-0009 표준 TSF 실험체 — 제품 빌드와 완전히 분리 (RFC-0009 §3)
#   make test    : portable test (Windows 불필요)
#   make         : dist/jamotong-standard-lab.dll   (x64)
#   make win32   : dist32/jamotong-standard-lab.dll (x86)
#   make trace   : dist-trace/jamotong-tsf-trace-lab.dll   (x64, separate identity)
#   make trace32 : dist32-trace/jamotong-tsf-trace-lab.dll (x86, separate identity)
#   make all     : 위 전부
CC      = x86_64-w64-mingw32-gcc
CC32    = i686-w64-mingw32-gcc
HOSTCC  = cc

# warning-as-error (RFC-0009 §15 완료 gate)
CFLAGS  = -Wall -Wextra -Werror -std=c11 -D_UNICODE -DUNICODE -O2 -Iinclude
TRACE_CFLAGS = $(CFLAGS) -DLAB_TRACE_BUILD
LDFLAGS = -shared -static -static-libgcc -s -Wl,--enable-stdcall-fixup \
          -lole32 -loleaut32 -luuid

SRCS = src/dllmain.c src/text_service.c src/context_state.c src/register.c \
       src/composition.c src/display_attribute.c src/diagnostics.c src/key_policy.c src/hangul2.c
DEF  = standard-lab.def

PORTABLE_SRCS = src/hangul2.c src/key_policy.c
TEST_FLAGS    = -Wall -Wextra -Werror -std=c11 -Iinclude -g

all: dll win32

dll: dist/jamotong-standard-lab.dll
dist/jamotong-standard-lab.dll: $(SRCS) $(DEF)
	@mkdir -p dist
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

win32: dist32/jamotong-standard-lab.dll
dist32/jamotong-standard-lab.dll: $(SRCS) $(DEF)
	@mkdir -p dist32
	$(CC32) $(CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

# Always-on, separately registered diagnostic variant. It does not log text or key values.
trace: dist-trace/jamotong-tsf-trace-lab.dll
dist-trace/jamotong-tsf-trace-lab.dll: $(SRCS) $(DEF)
	@mkdir -p dist-trace
	$(CC) $(TRACE_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

trace32: dist32-trace/jamotong-tsf-trace-lab.dll
dist32-trace/jamotong-tsf-trace-lab.dll: $(SRCS) $(DEF)
	@mkdir -p dist32-trace
	$(CC32) $(TRACE_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

trace-all: trace trace32

# portable test — Windows 없이 리눅스에서도 돈다 (RFC-0009 §21)
test:
	$(HOSTCC) $(TEST_FLAGS) -o /tmp/lab_hangul2_test tests/hangul2_test.c src/hangul2.c
	/tmp/lab_hangul2_test
	$(HOSTCC) $(TEST_FLAGS) -o /tmp/lab_key_policy_test tests/key_policy_test.c $(PORTABLE_SRCS)
	/tmp/lab_key_policy_test

# 같은 테스트를 sanitizer로 한 번 더 (RFC-0009 §14.1)
test-san:
	$(HOSTCC) $(TEST_FLAGS) -fsanitize=address,undefined -o /tmp/lab_h2_san tests/hangul2_test.c src/hangul2.c
	/tmp/lab_h2_san
	$(HOSTCC) $(TEST_FLAGS) -fsanitize=address,undefined -o /tmp/lab_kp_san tests/key_policy_test.c $(PORTABLE_SRCS)
	/tmp/lab_kp_san

clean:
	rm -rf dist dist32 dist-trace dist32-trace

.PHONY: all dll win32 trace trace32 trace-all test test-san clean
