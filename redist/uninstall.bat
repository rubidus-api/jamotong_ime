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

REM ---- 4) Remove binaries (no sign-out needed) ---------------------
REM  The IME DLL stays memory-mapped in every running app that used text
REM  input (explorer, ctfmon, browsers, ...), so plain deletion can be
REM  blocked. NTFS still allows RENAMING a mapped DLL, so anything locked
REM  is moved aside as *.old.<n>: the folder is immediately ready for a
REM  new version, and the leftovers become deletable once those apps
REM  exit (this script and install.bat sweep them on the next run).
echo [*] Sweeping leftovers from previous runs ...
del /F /Q "%~dp0*.old.*" >nul 2>&1
echo [*] Removing binaries ...
set "MOVED="
set "LOCKED="
for %%F in (jamotong.dll jamotong32.dll jamotong.exe) do (
  if exist "%~dp0%%F" (
    del /F /Q "%~dp0%%F" >nul 2>&1
    if exist "%~dp0%%F" (
      ren "%~dp0%%F" "%%F.old.%RANDOM%%RANDOM%" >nul 2>&1
      if exist "%~dp0%%F" (
        set "LOCKED=1"
        echo    [locked] %%F  - could not delete or rename
      ) else (
        set "MOVED=1"
        echo    [moved aside] %%F  - swept automatically on the next run
      )
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
  echo   A file could be neither deleted nor renamed ^(unusual - likely
  echo   an antivirus hold^). Sign out and back in, then run this again.
) else if defined MOVED (
  echo ================================================================
  echo   [OK] Uninstalled - no sign-out needed
  echo ================================================================
  echo   Locked binaries were moved aside as *.old.* files. Running
  echo   apps keep using those copies until they exit; delete the
  echo   leftovers later ^(or let install/uninstall sweep them^).
  echo   You can put a new version into this folder right away.
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

REM ---- 5) Optional: restart Explorer to clear the tray icon --------
REM  Runs de-elevated via runas /trustlevel so the new shell does not
REM  inherit administrator rights from this script.
choice /C YN /N /T 20 /D N /M "Restart Explorer now to refresh the tray/icons? [Y/N] (auto-N in 20s) "
if "%errorlevel%"=="1" (
  echo [*] Restarting Explorer ...
  taskkill /F /IM explorer.exe >nul 2>&1
  runas /trustlevel:0x20000 "%SystemRoot%\explorer.exe" >nul 2>&1 || start "" "%SystemRoot%\explorer.exe"
)
pause
