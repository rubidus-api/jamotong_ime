@echo off
setlocal EnableExtensions
title Jamotong IME - Install
chcp 65001 >nul 2>&1

echo ================================================================
echo   Jamotong IME - Install  (TSF text service)
echo ================================================================
echo.

REM ---- 1) Administrator required ----------------------------------
net session >nul 2>&1
if not "%errorlevel%"=="0" (
  echo [!] Administrator privileges required.
  echo     Right-click this file and choose "Run as administrator".
  echo.
  pause
  exit /B 1
)

REM ---- 2) 64-bit Windows required ---------------------------------
set "ARCH=%PROCESSOR_ARCHITECTURE%"
if defined PROCESSOR_ARCHITEW6432 set "ARCH=%PROCESSOR_ARCHITEW6432%"
if /I not "%ARCH%"=="AMD64" (
  echo [!] 64-bit Windows is required.
  echo.
  pause
  exit /B 1
)

REM ---- 3) Files present? ------------------------------------------
if not exist "%~dp0jamotong.dll" (
  echo [!] jamotong.dll not found in: %~dp0
  pause
  exit /B 1
)

REM ---- 4) Unblock downloaded files (Mark-of-the-Web) --------------
echo [*] Unblocking files ...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -LiteralPath '%~dp0' -Recurse -File | Unblock-File" >nul 2>&1

REM ---- 5) Register 64-bit TSF DLL ---------------------------------
echo [*] Registering 64-bit:  regsvr32 jamotong.dll
regsvr32 /s "%~dp0jamotong.dll"
if not "%errorlevel%"=="0" (
  echo.
  echo [FAIL] 64-bit registration failed ^(code %errorlevel%^).
  echo   - Check antivirus quarantine / file unblock, then retry.
  echo.
  pause
  exit /B 1
)
echo    [OK] 64-bit registered.

REM ---- 6) Register 32-bit TSF DLL (for 32-bit apps) ---------------
if exist "%~dp0jamotong32.dll" (
  echo [*] Registering 32-bit:  SysWOW64\regsvr32 jamotong32.dll
  "%SystemRoot%\SysWOW64\regsvr32.exe" /s "%~dp0jamotong32.dll"
  if not "%errorlevel%"=="0" (
    echo    [warn] 32-bit registration failed ^(64-bit apps still work^).
  ) else (
    echo    [OK] 32-bit registered.
  )
)

echo.
echo ================================================================
echo   [OK] Installed
echo ================================================================
echo  Next steps:
echo   1) Sign out and back in ^(or reboot^).
echo   2) Win+Space  -^>  select "Jamotong IME".
echo   3) Cycle layouts: Right Alt / Hangul key / Shift+Space.
echo.
echo  Tools:
echo   - jamotong.exe          : tray monitor + settings ^(run anytime^)
echo   - Ctrl+Alt+K            : open settings while typing
echo   - Ctrl+Alt+U            : Unicode codepoint input
echo   - Hanja key             : hanja/symbol candidates
echo.
echo  See README-install.txt for details and troubleshooting.
echo.
pause
