@echo off
setlocal EnableExtensions
title Jamotong IME - Per-user uninstall
set "DEST=%LocalAppData%\Programs\Jamotong"

if "%~1"=="--unregister-only" goto :unregister

echo ================================================================
echo   Jamotong IME - Per-user uninstall
echo ================================================================
echo  Unregisters first (one admin prompt), then removes %DEST%.
echo  NEVER delete the folder without unregistering - that leaves a
echo  broken registration that destabilises text input system-wide.
echo.
net session >nul 2>&1
if "%errorlevel%"=="0" goto :dounreg
echo [1/2] Unregistering - Windows will ask for administrator approval...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath 'cmd.exe' -ArgumentList '/c','\"%~f0\" --unregister-only' -Verb RunAs -Wait" >nul 2>&1
goto :cleanup

:dounreg
echo [1/2] Unregistering (already elevated)...
call "%~f0" --unregister-only

:cleanup
echo [2/2] Removing files...
cd /d "%LocalAppData%"
rd /S /Q "%DEST%" 2>nul
if exist "%DEST%" (
  echo   Some files are still in use - sign out and in, then delete %DEST% by hand.
) else (
  echo   Removed %DEST%
)
pause
exit /B 0

:unregister
regsvr32 /s /u "%DEST%\jamotong.dll"
if exist "%SystemRoot%\SysWOW64\regsvr32.exe" "%SystemRoot%\SysWOW64\regsvr32.exe" /s /u "%DEST%\jamotong32.dll"
exit /B 0
