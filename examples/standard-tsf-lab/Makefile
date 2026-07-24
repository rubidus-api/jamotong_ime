# RFC-0009 표준 TSF 실험체 — 제품 빌드와 완전히 분리 (RFC-0009 §3)
#   make test    : portable test (Windows 불필요)
#   make         : dist/jamotong-standard-lab.dll   (x64)
#   make win32   : dist32/jamotong-standard-lab.dll (x86)
#   make trace   : dist-trace/jamotong-tsf-trace-lab.dll   (x64, separate identity)
#   make trace32 : dist32-trace/jamotong-tsf-trace-lab.dll (x86, separate identity)
#   make akel-suite : four isolated x64 AkelPad diagnostic variants
#   make akel-metadata-suite : four isolated x64 composition-metadata variants
#   make akel-metadata-r2-suite : corrected four-profile x64 metadata suite
#   make akel-metadata-r3-suite : property-order/first-update interaction suite
#   make akel-metadata-r4-probe : same-identity trace/read-back observer probe
#   make all     : standard x64+x86 (diagnostic suites are explicit targets above)
CC      = x86_64-w64-mingw32-gcc
CC32    = i686-w64-mingw32-gcc
HOSTCC  = cc

# warning-as-error (RFC-0009 §15 완료 gate)
CFLAGS  = -Wall -Wextra -Werror -std=c11 -D_UNICODE -DUNICODE -O2 -Iinclude
TRACE_CFLAGS = $(CFLAGS) -DLAB_TRACE_BUILD
AKEL_CONTROL_CFLAGS = $(CFLAGS) -DLAB_AKEL_CONTROL_BUILD
AKEL_AE_NONE_CFLAGS = $(CFLAGS) -DLAB_AKEL_AE_NONE_BUILD
AKEL_INSERT_FIRST_CFLAGS = $(CFLAGS) -DLAB_AKEL_INSERT_FIRST_BUILD
AKEL_NO_SELECTION_CFLAGS = $(CFLAGS) -DLAB_AKEL_NO_SELECTION_BUILD
AKEL_META_CONTROL_CFLAGS = $(CFLAGS) -DLAB_AKEL_META_CONTROL_BUILD
AKEL_META_LANGID_CFLAGS = $(CFLAGS) -DLAB_AKEL_META_LANGID_BUILD
AKEL_META_READING_CFLAGS = $(CFLAGS) -DLAB_AKEL_META_READING_BUILD
AKEL_META_BOTH_CFLAGS = $(CFLAGS) -DLAB_AKEL_META_BOTH_BUILD
AKEL_META_R2_CONTROL_CFLAGS = $(CFLAGS) -DLAB_AKEL_META_R2_CONTROL_BUILD
AKEL_META_R2_LANGID_CFLAGS = $(CFLAGS) -DLAB_AKEL_META_R2_LANGID_BUILD
AKEL_META_R2_READING_CFLAGS = $(CFLAGS) -DLAB_AKEL_META_R2_READING_BUILD
AKEL_META_R2_BOTH_CFLAGS = $(CFLAGS) -DLAB_AKEL_META_R2_BOTH_BUILD
AKEL_META_R3_READING_CFLAGS = $(CFLAGS) -DLAB_AKEL_META_R3_READING_BUILD
AKEL_META_R3_LANGID_READING_CFLAGS = $(CFLAGS) -DLAB_AKEL_META_R3_LANGID_READING_BUILD
AKEL_META_R3_READING_LANGID_CFLAGS = $(CFLAGS) -DLAB_AKEL_META_R3_READING_LANGID_BUILD
AKEL_META_R3_LANGID_ONCE_READING_CFLAGS = $(CFLAGS) \
	-DLAB_AKEL_META_R3_LANGID_ONCE_READING_BUILD
AKEL_META_R4_READING_PROBE_CFLAGS = $(CFLAGS) -DLAB_AKEL_META_R4_READING_PROBE_BUILD
LDFLAGS = -shared -static -static-libgcc -s -Wl,--enable-stdcall-fixup \
          -lole32 -loleaut32 -luuid

SRCS = src/dllmain.c src/text_service.c src/context_state.c src/register.c \
       src/composition.c src/display_attribute.c src/diagnostics.c src/key_policy.c src/hangul2.c
HEADERS = $(wildcard include/*.h)
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

# Side-by-side x64 protocol experiments. Every DLL has a distinct COM/profile identity.
akel-suite: \
	dist-akel/jamotong-tsf-test-0-control.dll \
	dist-akel/jamotong-tsf-test-1-ae-none.dll \
	dist-akel/jamotong-tsf-test-2-insert-first.dll \
	dist-akel/jamotong-tsf-test-3-no-selection.dll

dist-akel/jamotong-tsf-test-0-control.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel
	$(CC) $(AKEL_CONTROL_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

dist-akel/jamotong-tsf-test-1-ae-none.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel
	$(CC) $(AKEL_AE_NONE_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

dist-akel/jamotong-tsf-test-2-insert-first.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel
	$(CC) $(AKEL_INSERT_FIRST_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

dist-akel/jamotong-tsf-test-3-no-selection.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel
	$(CC) $(AKEL_NO_SELECTION_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

# Side-by-side x64 metadata experiments. The control follows the normal composition path.
akel-metadata-suite: \
	dist-akel-metadata/jamotong-tsf-meta-0-control.dll \
	dist-akel-metadata/jamotong-tsf-meta-1-langid.dll \
	dist-akel-metadata/jamotong-tsf-meta-2-reading.dll \
	dist-akel-metadata/jamotong-tsf-meta-3-langid-reading.dll

dist-akel-metadata/jamotong-tsf-meta-0-control.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel-metadata
	$(CC) $(AKEL_META_CONTROL_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

dist-akel-metadata/jamotong-tsf-meta-1-langid.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel-metadata
	$(CC) $(AKEL_META_LANGID_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

dist-akel-metadata/jamotong-tsf-meta-2-reading.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel-metadata
	$(CC) $(AKEL_META_READING_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

dist-akel-metadata/jamotong-tsf-meta-3-langid-reading.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel-metadata
	$(CC) $(AKEL_META_BOTH_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

# Corrected side-by-side x64 metadata experiments. These identities never overlap v1.
akel-metadata-r2-suite: \
	dist-akel-metadata-r2/jamotong-tsf-meta-r2-0-control.dll \
	dist-akel-metadata-r2/jamotong-tsf-meta-r2-1-langid.dll \
	dist-akel-metadata-r2/jamotong-tsf-meta-r2-2-reading.dll \
	dist-akel-metadata-r2/jamotong-tsf-meta-r2-3-langid-reading.dll

dist-akel-metadata-r2/jamotong-tsf-meta-r2-0-control.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel-metadata-r2
	$(CC) $(AKEL_META_R2_CONTROL_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

dist-akel-metadata-r2/jamotong-tsf-meta-r2-1-langid.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel-metadata-r2
	$(CC) $(AKEL_META_R2_LANGID_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

dist-akel-metadata-r2/jamotong-tsf-meta-r2-2-reading.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel-metadata-r2
	$(CC) $(AKEL_META_R2_READING_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

dist-akel-metadata-r2/jamotong-tsf-meta-r2-3-langid-reading.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel-metadata-r2
	$(CC) $(AKEL_META_R2_BOTH_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

# R3 keeps the R2 positive/negative controls and isolates order plus repeated LANGID writes.
akel-metadata-r3-suite: \
	dist-akel-metadata-r3/jamotong-tsf-meta-r3-0-reading.dll \
	dist-akel-metadata-r3/jamotong-tsf-meta-r3-1-langid-reading.dll \
	dist-akel-metadata-r3/jamotong-tsf-meta-r3-2-reading-langid.dll \
	dist-akel-metadata-r3/jamotong-tsf-meta-r3-3-langid-once-reading.dll

dist-akel-metadata-r3/jamotong-tsf-meta-r3-0-reading.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel-metadata-r3
	$(CC) $(AKEL_META_R3_READING_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

dist-akel-metadata-r3/jamotong-tsf-meta-r3-1-langid-reading.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel-metadata-r3
	$(CC) $(AKEL_META_R3_LANGID_READING_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

dist-akel-metadata-r3/jamotong-tsf-meta-r3-2-reading-langid.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel-metadata-r3
	$(CC) $(AKEL_META_R3_READING_LANGID_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

dist-akel-metadata-r3/jamotong-tsf-meta-r3-3-langid-once-reading.dll: $(SRCS) $(DEF)
	@mkdir -p dist-akel-metadata-r3
	$(CC) $(AKEL_META_R3_LANGID_ONCE_READING_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

# R4 uses one binary and one identity. An inherited process-local mode changes only
# synchronous probe instrumentation around the same Reading SetValue path.
akel-metadata-r4-probe: \
	dist-akel-metadata-r4/jamotong-tsf-meta-r4-reading-probe.dll

dist-akel-metadata-r4/jamotong-tsf-meta-r4-reading-probe.dll: $(SRCS) $(HEADERS) $(DEF)
	@mkdir -p dist-akel-metadata-r4
	$(CC) $(AKEL_META_R4_READING_PROBE_CFLAGS) -o $@ $(SRCS) $(DEF) $(LDFLAGS)

# portable test — Windows 없이 리눅스에서도 돈다 (RFC-0009 §21)
test:
	$(HOSTCC) $(TEST_FLAGS) -o /tmp/lab_hangul2_test tests/hangul2_test.c src/hangul2.c
	/tmp/lab_hangul2_test
	$(HOSTCC) $(TEST_FLAGS) -o /tmp/lab_key_policy_test tests/key_policy_test.c $(PORTABLE_SRCS)
	/tmp/lab_key_policy_test
	$(HOSTCC) $(TEST_FLAGS) -o /tmp/lab_tsf_property_guids_test tests/tsf_property_guids_test.c
	/tmp/lab_tsf_property_guids_test

guid-test:
	$(HOSTCC) $(TEST_FLAGS) -o /tmp/lab_tsf_property_guids_test tests/tsf_property_guids_test.c
	/tmp/lab_tsf_property_guids_test

# 같은 테스트를 sanitizer로 한 번 더 (RFC-0009 §14.1)
test-san:
	$(HOSTCC) $(TEST_FLAGS) -fsanitize=address,undefined -o /tmp/lab_h2_san tests/hangul2_test.c src/hangul2.c
	/tmp/lab_h2_san
	$(HOSTCC) $(TEST_FLAGS) -fsanitize=address,undefined -o /tmp/lab_kp_san tests/key_policy_test.c $(PORTABLE_SRCS)
	/tmp/lab_kp_san
	$(HOSTCC) $(TEST_FLAGS) -fsanitize=address,undefined -o /tmp/lab_guid_san tests/tsf_property_guids_test.c
	/tmp/lab_guid_san

clean:
	rm -rf dist dist32 dist-trace dist32-trace dist-akel dist-akel-metadata \
	       dist-akel-metadata-r2 dist-akel-metadata-r3 dist-akel-metadata-r4

.PHONY: all dll win32 trace trace32 trace-all akel-suite akel-metadata-suite \
        akel-metadata-r2-suite akel-metadata-r3-suite akel-metadata-r4-probe \
        test guid-test test-san clean
