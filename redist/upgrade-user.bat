@echo off
setlocal EnableExtensions
title Jamotong IME - Per-user upgrade (no admin)
set "DEST=%LocalAppData%\Programs\Jamotong"

if "%~1"=="--register-only" goto :register

echo ================================================================
echo   Jamotong IME - Per-user upgrade  (no admin needed)
echo ================================================================
echo  Copies new files over %DEST%. Registration stays valid because
echo  the path does not change. Sign out and in so running apps load
echo  the new DLL.
echo.
if not exist "%~dp0jamotong.dll" (
  echo [!] jamotong.dll not found next to this script. Run this from the NEW extracted zip.
  pause
  exit /B 1
)
if not exist "%DEST%\jamotong.dll" (
  echo [!] No per-user installation found at %DEST%. Run install-user.bat first.
  pause
  exit /B 1
)
copy /Y "%~dp0jamotong.dll"   "%DEST%\" >nul && echo   jamotong.dll updated
copy /Y "%~dp0jamotong32.dll" "%DEST%\" >nul && echo   jamotong32.dll updated
copy /Y "%~dp0jamotong.exe"   "%DEST%\" >nul && echo   jamotong.exe updated
for %%F in (hanja.txt hanja_hunum.txt example.jmt example-artsey.jmt example-dvorak.jmt UNICODE-LICENSE.txt README.md README.ko.md LICENSE COPYRIGHT.md uninstall-user.bat upgrade-user.bat) do (
  if exist "%~dp0%%F" copy /Y "%~dp0%%F" "%DEST%\" >nul
)
echo.
echo Done. Sign out and in to load the new build everywhere.
pause
exit /B 0

:register
rem Called elevated by install-user.bat - register the per-user copies machine-wide.
regsvr32 /s "%DEST%\jamotong.dll"
if exist "%SystemRoot%\SysWOW64\regsvr32.exe" "%SystemRoot%\SysWOW64\regsvr32.exe" /s "%DEST%\jamotong32.dll"
exit /B 0
