@echo off
setlocal EnableExtensions
title Jamotong IME - Uninstall
chcp 65001 >nul 2>&1

echo ================================================================
echo   Jamotong IME - Uninstall
echo ================================================================
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

echo  * Switch to another IME first (e.g. Microsoft IME).
echo    A DLL that is in use cannot be unregistered cleanly.
echo.

REM ---- 1) Unregister TSF DLLs --------------------------------------
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
    echo   - Switch to another IME, sign out/in, then retry.
    echo.
    pause
    exit /B 1
  )
)

REM ---- 2) Clean up legacy IMM32 leftovers (old builds only) --------
REM  Cleans up IMM32 (.ime) leftovers installed by old builds. Current builds never install it.
if exist "%~dp0jamotong.exe" (
  "%~dp0jamotong.exe" /uninstallime >nul 2>&1
)

echo.
echo ================================================================
echo   [OK] Uninstalled
echo ================================================================
echo   Sign out and back in to remove it from the language list.
echo   User settings remain at %%APPDATA%%\Jamotong ^(delete manually if desired^).
echo   You may now delete this folder.
echo.
pause
