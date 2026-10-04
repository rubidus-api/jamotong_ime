Jamotong cleanup - removes what older Jamotong versions left on this PC
=======================================================================

Since 0.70.0 Jamotong is one installer: the Chinese and Japanese languages are features you tick
during setup. This tool removes what earlier versions left behind:

  - the separate language pack installers of 0.62-0.69 ("Jamotong Chinese Simplified/Traditional",
    "Jamotong Chinese") and a Jamotong installation older than 0.70.0 (0.70.0 and later is kept);
  - the old zip packs in your profile (%APPDATA%\Jamotong): the Japanese pack of 0.44-0.49 and the
    Chinese pack of 0.48-0.61 - recognised by their layout file and moved to a backup folder
    (%APPDATA%\Jamotong\old-pack-backup-<date>), not deleted;
  - language data an old pack left in the install folder that nothing owns any more.

Run cleanup.cmd. It lists what it found and asks before it changes anything; removing installed
programs asks for administrator rights once. Nothing is restarted. A log is written to
%TEMP%\jamotong-cleanup.log. "cleanup.cmd -List" only lists.

If it removed an old Jamotong, install the latest from
https://github.com/rubidus-api/jamotong_ime/releases/latest


자모통 정리 도구 - 예전 판이 이 PC 에 남긴 것을 지웁니다
=========================================================

0.70.0 부터 자모통은 설치 파일 하나이고, 중국어·일본어는 설치할 때 체크하는 구성 요소입니다.
이 도구는 예전 판이 남긴 것을 지웁니다:

  - 0.62~0.69 의 따로 된 언어 팩 설치본("Jamotong Chinese Simplified/Traditional", "Jamotong Chinese")과
    0.70.0 보다 낮은 자모통 설치본 (0.70.0 이후는 그대로 둡니다);
  - 사용자 폴더(%APPDATA%\Jamotong)의 예전 zip 팩: 0.44~0.49 일본어 팩, 0.48~0.61 중국어 팩 - 자판 파일로
    알아보고, 지우지 않고 백업 폴더(%APPDATA%\Jamotong\old-pack-backup-<날짜>)로 옮깁니다;
  - 예전 팩이 설치 폴더에 남긴, 이제 아무 제품도 갖지 않은 언어 자료.

cleanup.cmd 를 실행하세요. 찾은 것을 보여 주고 바꾸기 전에 묻습니다. 설치된 프로그램을 지울 때 관리자
권한을 한 번 묻습니다. 재부팅은 하지 않습니다. 기록은 %TEMP%\jamotong-cleanup.log 에 남습니다.
"cleanup.cmd -List" 는 보여 주기만 합니다.

예전 자모통을 지웠다면 최신 판을 설치하세요:
https://github.com/rubidus-api/jamotong_ime/releases/latest
