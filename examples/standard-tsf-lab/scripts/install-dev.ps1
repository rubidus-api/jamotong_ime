# RFC-0009 실험체 개발용 설치 — 관리자 권한 PowerShell에서 실행
# 제품(jamotong)에는 손대지 않는다. 실험체 CLSID/profile/category만 등록한다.
param([switch]$X86)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dll  = if ($X86) { Join-Path $root "dist32\jamotong-standard-lab.dll" }
                 else { Join-Path $root "dist\jamotong-standard-lab.dll" }

if (-not (Test-Path $dll)) { throw "빌드 산출물이 없습니다: $dll  (make dll / make win32)" }

# 개발 중에는 반드시 먼저 해제한다 — 경로가 바뀌면 옛 DLL이 계속 쓰인다
$sys = if ($X86) { "$env:WINDIR\SysWOW64\regsvr32.exe" } else { "$env:WINDIR\System32\regsvr32.exe" }
& $sys /u /s $dll 2>$null
& $sys /s $dll
if ($LASTEXITCODE -ne 0) { throw "regsvr32 실패 ($LASTEXITCODE)" }

Write-Host "등록 완료: $dll"
Write-Host "설정 > 시간 및 언어 > 언어 및 지역 > 한국어 > 옵션 > 키보드 에서"
Write-Host "'Jamotong Standard TSF Lab' 을 추가하십시오."
