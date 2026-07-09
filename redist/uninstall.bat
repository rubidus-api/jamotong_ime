@echo off
setlocal EnableExtensions
title Jamotong IME - Uninstall
chcp 65001 >nul 2>&1

echo ================================================================
echo   Jamotong IME - Uninstall
echo ================================================================
echo.
echo  Tip: switch to another IME first (e.g. Microsoft IME, Win+Space)
echo       so this IME is not the active text service.
echo.

REM ---- Administrator required --------------------------------------
net session >nul 2>&1
if not "%errorlevel%"=="0" (
  echo [!] Administrator privileges required.
  echo     Right-click this file and choose "Run as administrator".
  echo.
  pause
  exit /B 1
)

REM ---- 1) Close jamotong.exe if running (releases the exe) ---------
taskkill /F /IM jamotong.exe >nul 2>&1

REM ---- 2) Unregister TSF DLLs --------------------------------------
if exist "%~dp0jamotong32.dll" (
  echo [*] Unregistering 32-bit ...
  "%SystemRoot%\SysWOW64\regsvr32.exe" /s /u "%~dp0jamotong32.dll"
)
if exist "%~dp0jamotong.dll" (
  echo [*] Unregistering 64-bit ...
  regsvr32 /s /u "%~dp0jamotong.dll"
  if not "%errorlevel%"=="0" (
    echo.
    echo [FAIL] Unregister failed ^(code %errorlevel%^).
    echo   - Switch to another IME ^(Win+Space^), then run this again.
    echo.
    pause
    exit /B 1
  )
)
echo    [OK] Unregistered ^(removed from the language list after sign-out^).

REM ---- 3) Clean up legacy IMM32 leftovers (old builds only) --------
if exist "%~dp0jamotong.exe" (
  "%~dp0jamotong.exe" /uninstallime >nul 2>&1
)

REM ---- 4) Try to delete the binaries right away --------------------
REM  The IME DLL stays memory-mapped in every running app that used text
REM  input (explorer, ctfmon, ...), so deletion may be blocked until you
REM  sign out. Anything that is not locked is removed now.
echo [*] Deleting binaries ...
set "LOCKED="
for %%F in (jamotong.dll jamotong32.dll jamotong.exe) do (
  if exist "%~dp0%%F" (
    del /F /Q "%~dp0%%F" >nul 2>&1
    if exist "%~dp0%%F" (
      set "LOCKED=1"
      echo    [locked] %%F  - still loaded by running apps
    ) else (
      echo    [deleted] %%F
    )
  )
)

echo.
if defined LOCKED (
  echo ================================================================
  echo   [OK] Unregistered - one more step to finish
  echo ================================================================
  echo   Some files are still mapped into running programs. That is
  echo   normal for an IME DLL; no reboot is needed:
  echo.
  echo    1) Sign out of Windows and sign back in.
  echo    2) Run uninstall.bat once more - it will delete the rest.
  echo       ^(Or simply delete this folder after signing back in.^)
) else (
  echo ================================================================
  echo   [OK] Uninstalled
  echo ================================================================
  echo   All binaries are removed. You may delete this folder now.
)
echo.
echo   Your settings remain at %%APPDATA%%\Jamotong
echo   ^(delete that folder too if you do not plan to reinstall^).
echo.
pause
