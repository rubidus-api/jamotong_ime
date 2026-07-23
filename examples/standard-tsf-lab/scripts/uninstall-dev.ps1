# RFC-0009 실험체 제거 — 관리자 권한 PowerShell
# 제품 등록은 건드리지 않는다.
param([switch]$X86)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dll  = if ($X86) { Join-Path $root "dist32\jamotong-standard-lab.dll" }
                 else { Join-Path $root "dist\jamotong-standard-lab.dll" }
$sys  = if ($X86) { "$env:WINDIR\SysWOW64\regsvr32.exe" } else { "$env:WINDIR\System32\regsvr32.exe" }
& $sys /u /s $dll
Write-Host "해제 완료: $dll"
