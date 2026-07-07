# Jamotong (자모통)

**Windows용 순수 C23 + WinAPI 한글 IME** — 프레임워크·외부 라이브러리 없이
TSF(Text Services Framework) 텍스트 서비스로 구현한 한글 입력기.

## 특징

- **커밋 전용 입력 엔진**: 확정된 음절만 문서에 삽입. CUAS(레거시 IMM32 브리지) 앱을 포함한
  **모든 classic 앱에서 동일하게 동작** (감지·앱별 우회 없음). 근거: `winapi-c-ime-manual.md` §8.
- **조합 미리보기 오버레이**: 조합 중 음절을 캐럿 위치의 플로팅 칩으로 표시 (RFC-0002).
  글꼴 사용자 설정 가능.
- **자판**: 2벌식·세벌식(최종) 내장, `.jmt` 파일로 사용자 자판(정적 맵/한글 조합/코드 자판) 추가.
  자판 순환 단축키(기본: 한/영, 오른쪽 Alt, Shift+Space) 사용자 설정.
- **한자 변환**: 조합 중 음절 + 한자키 → 후보창(훈음 표시, ↑↓/PgUp/PgDn/숫자/Enter).
  독음 데이터 = Unicode Unihan(8,900+자, 교육용 기초한자 우선 정렬) + 단어 331개 + 훈음 1,784자.
- **특수문자**: 자음 + 한자키 (ㅁ=기호, ㅅ=그리스, ㅈ=로마숫자 등 관례).
- **유니코드 직접 입력**: `Ctrl+Alt+U` → 16진 코드포인트 입력 팝업(실시간 미리보기) → Enter 삽입.
- **설정**: `Ctrl+Alt+K` 또는 `jamotong.exe` — 자판 관리/단축키/옵션, `%APPDATA%\Jamotong\config.ini`
  저장, Import/Export.
- 32/64비트 앱 모두 지원(각각의 DLL), Win11 트레이 브랜딩 아이콘.

## 설치

배포 zip을 풀고 `install.bat`를 **관리자 권한**으로 실행 → 로그아웃/재로그인 →
Win+Space로 "Jamotong IME" 선택. 자세한 내용은 `README-install.txt`.

## 빌드

MinGW-w64 크로스 컴파일 (Linux에서):

```sh
make            # dist/jamotong.dll (x64)
make win32      # dist/jamotong32.dll (x86)
make configapp  # dist/jamotong.exe (설정/자판 편집 앱)
make zip        # 배포 패키지
make automata-test config-test sanitize-test   # 네이티브 테스트 (ASan/UBSan 포함)
```

## 문서

- **`winapi-c-ime-manual.md`** — WinAPI+C만으로 IME를 만드는 종합 매뉴얼
  (COM 기초부터, 시행착오와 결론 포함. 다른 IME를 만들려면 여기부터)
- `CHANGELOG.md` — 버전별 변경 이력
- 상세 설계 노트·RFC·데이터 출처 명세는 내부 저장소에서 관리 (배포 zip에는
  Unicode License 사본 등 필요한 고지가 동봉됨)

## 라이선스

코드: [MIT License](LICENSE). 한자 독음 데이터는 Unicode Unihan DB 파생(Unicode License v3,
배포 zip에 고지 동봉). GPL/LGPL/CC BY-SA 자료는 사용하지 않음.
