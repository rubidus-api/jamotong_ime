# tsf-conformance-lab — RFC-0012 Phase 0 시험대
CC64 = x86_64-w64-mingw32-gcc
CC32 = i686-w64-mingw32-gcc
CFLAGS = -std=c11 -O1 -Wall -Wextra -municode -DUNICODE -D_UNICODE
LIBS = -lole32 -loleaut32 -luuid

all: probe_host.exe conf_tip.dll

probe_host.exe: src/probe_host.c src/tsf_missing_guids.h
	$(CC64) -std=c11 -O1 -Wall -Wextra -o $@ src/probe_host.c $(LIBS)

win32: probe_host32.exe conf_tip32.dll

probe_host32.exe: src/probe_host.c src/tsf_missing_guids.h
	$(CC32) -std=c11 -O1 -Wall -Wextra -o $@ src/probe_host.c $(LIBS)

wine: probe_host.exe
	sh scripts/run-wine.sh

conf_tip.dll: src/conf_tip.c src/conf_tip.def src/tsf_missing_guids.h
	$(CC64) -std=c11 -O1 -Wall -Wextra -shared -o $@ src/conf_tip.c src/conf_tip.def $(LIBS) -static-libgcc

conf_tip32.dll: src/conf_tip.c src/conf_tip.def src/tsf_missing_guids.h
	$(CC32) -std=c11 -O1 -Wall -Wextra -shared -o $@ src/conf_tip.c src/conf_tip.def $(LIBS) -static-libgcc

clean:
	rm -f probe_host.exe probe_host32.exe conf_tip.dll conf_tip32.dll

.PHONY: all win32 wine clean
