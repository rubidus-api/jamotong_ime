# Jamotong IME — 공개 저장소 빌드 (MinGW-w64 크로스 컴파일)
#   make            : dist/jamotong.dll   (x64 TSF IME)
#   make win32      : dist/jamotong32.dll (x86 — 32비트 앱용; TIP DLL은 호스트 비트수와 일치 필요)
#   make configapp  : dist/jamotong.exe   (트레이 모니터링/설정 앱)
# 내부 개발 타깃(네이티브 테스트·패키징 등)은 비공개 저장소의 private.mk 가 제공한다(없으면 무시).

CC = x86_64-w64-mingw32-gcc
CC32 = i686-w64-mingw32-gcc
CFLAGS = -Wall -Wextra -std=c2x -D_UNICODE -DUNICODE -O2
# -static/-static-libgcc: MinGW 런타임을 정적 포함 → IME DLL이 임의 호스트 프로세스에 자기완결 로드.
# -s: 배포용 심볼 스트립. --enable-stdcall-fixup: 32비트 .def 장식이름 별칭 경고 억제.
LDFLAGS = -shared -static -static-libgcc -s -Wl,--enable-stdcall-fixup -lole32 -loleaut32 -luuid -lcomctl32 -lcomdlg32 -lgdi32 -limm32

TARGET = dist/jamotong.dll
SRCS = src/dllmain.c src/text_service.c src/register.c src/fsm.c src/layout.c src/edit_session.c src/config.c src/langbar.c src/settings_ui.c src/plugin_loader.c src/hanja_dict.c src/candidate_ui.c src/display_attr.c src/special_char.c src/hangul_layout.c src/chord.c src/chord_layout.c src/klay.c src/func_configure.c src/preedit_overlay.c src/code_input.c

# src/jamotong.def: DllRegisterServer 등 진입점을 장식 없는 이름으로 export (32비트 regsvr32 필수)
DEF = src/jamotong.def
# 리소스: 프로파일 아이콘("자모통") + 버전(VERSIONINFO). windres로 COFF 오브젝트 생성 후 링크.
RC = src/jamotong.rc
RCDEP = src/jamotong.rc src/jamotong.ico
WINDRES64 = x86_64-w64-mingw32-windres
WINDRES32 = i686-w64-mingw32-windres

all: $(TARGET)

$(TARGET): $(SRCS) $(DEF) $(RCDEP)
	@mkdir -p dist
	$(WINDRES64) -I src $(RC) -O coff -o dist/jamotong_res64.o
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(DEF) dist/jamotong_res64.o $(LDFLAGS)

win32: dist/jamotong32.dll
dist/jamotong32.dll: $(SRCS) $(DEF) $(RCDEP)
	@mkdir -p dist
	$(WINDRES32) -I src $(RC) -O coff -o dist/jamotong_res32.o
	$(CC32) $(CFLAGS) -o $@ $(SRCS) $(DEF) dist/jamotong_res32.o $(LDFLAGS)

# 트레이 모니터링/설정 앱
APP_SRCS = src/tray_app.c src/config.c src/layout.c src/fsm.c src/hangul_layout.c \
           src/chord.c src/chord_layout.c src/klay.c src/plugin_loader.c src/settings_ui.c
configapp: dist/jamotong.exe
dist/jamotong.exe: $(APP_SRCS)
	@mkdir -p dist
	$(CC) $(CFLAGS) -municode -mwindows -o $@ $(APP_SRCS) -static -static-libgcc -s -lgdi32 -lcomdlg32 -lcomctl32 -limm32 -lole32 -luuid -lshell32

# 빌드 산출물 + 재배포 데이터(redist/: 한자 데이터·설치 스크립트·예제 자판)를 dist/에 모아
# '설치 가능한 폴더'를 만든다. 소스 빌드 사용자는 이 폴더에서 install.bat 실행.
stage: all win32 configapp
	cp redist/* dist/
	@echo "dist/ = installable folder (run install.bat as administrator)"

clean:
	rm -f $(TARGET) dist/jamotong32.dll dist/jamotong.exe dist/*_res*.o

.PHONY: all win32 configapp stage clean

# ── 내부 개발 타깃 (비공개 저장소가 옆에 있으면 활성화) ──────────────────────────────
-include ../jamotong-private/private.mk
