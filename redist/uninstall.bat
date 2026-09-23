@echo off
setlocal EnableExtensions
title Jamotong IME - Uninstall
chcp 65001 >nul 2>&1

rem  Removes the machine-wide install (%ProgramFiles%\Jamotong). It unregisters whatever path is
rem  actually registered, so an older install (extracted zip folder or the former per-user folder)
rem  is removed the same way. Owner decision A5 / RFC-0008 W1-05 (2026-09-22).

set "DEST=%ProgramFiles%\Jamotong"
set "CLSIDKEY=HKCR\CLSID\{C471BCF2-343F-4187-A103-24151C3E20B9}\InprocServer32"
set "BINS=jamotong.dll jamotong32.dll jamotong.exe"

rem A batch file cannot delete the folder it runs from: run a temporary copy instead.
if /I "%~dp0"=="%DEST%\" if /I not "%~1"=="--from-temp" (
  copy /Y "%~f0" "%TEMP%\jamotong-uninstall.bat" >nul
  "%TEMP%\jamotong-uninstall.bat" --from-temp
  exit /B %errorlevel%
)

echo ================================================================
echo   Jamotong IME - Uninstall
echo ================================================================
echo.
echo  Tip: switch to another input method first (Win+Space).
echo.
net session >nul 2>&1
if not "%errorlevel%"=="0" (
  echo [!] Administrator privileges required.
  echo     Right-click uninstall.bat and choose "Run as administrator".
  pause
  exit /B 1
)

set "REG64="
set "REG32="
for /f "tokens=2,*" %%A in ('reg query "%CLSIDKEY%" /ve /reg:64 2^>nul ^| find "REG_SZ"') do set "REG64=%%B"
for /f "tokens=2,*" %%A in ('reg query "%CLSIDKEY%" /ve /reg:32 2^>nul ^| find "REG_SZ"') do set "REG32=%%B"

taskkill /F /IM jamotong.exe >nul 2>&1

rem ---- 1) Unregister what is registered (and the machine-wide copy) ------------------------------
echo [1/3] Unregistering ...
if defined REG32 if exist "%REG32%" "%SystemRoot%\SysWOW64\regsvr32.exe" /s /u "%REG32%"
if exist "%DEST%\jamotong32.dll" "%SystemRoot%\SysWOW64\regsvr32.exe" /s /u "%DEST%\jamotong32.dll"
if defined REG64 if exist "%REG64%" regsvr32 /s /u "%REG64%"
if exist "%DEST%\jamotong.dll" regsvr32 /s /u "%DEST%\jamotong.dll"
reg query "%CLSIDKEY%" /ve /reg:64 >nul 2>&1
if not errorlevel 1 (
  echo [FAIL] The 64-bit registration is still there. Switch to another input method
  echo        ^(Win+Space^), then run this again.
  pause
  exit /B 1
)
reg query "%CLSIDKEY%" /ve /reg:32 >nul 2>&1
if not errorlevel 1 (
  echo [FAIL] The 32-bit registration is still there. Run this again.
  pause
  exit /B 1
)
echo      [OK] Unregistered ^(the language list updates after sign-out^).

rem Legacy IMM32 leftovers from very old builds
if exist "%DEST%\jamotong.exe" "%DEST%\jamotong.exe" /uninstallime >nul 2>&1

rem ---- 2) Remove files. A DLL still mapped in running apps cannot be deleted but can be renamed:
rem         such files are moved aside and removed after the next sign-in. ------------------------
echo [2/3] Removing files ...
set "LEFT="
call :clean "%DEST%\"
if defined REG64 for %%P in ("%REG64%") do (
  echo %%~dpP | find /I "\AppData\Local\Programs\Jamotong\" >nul && call :clean "%%~dpP"
)

rem ---- 3) Remove leftovers at the next sign-in. Only *.old.* files and an EMPTY folder are removed,
rem         so a reinstall before that sign-in is never touched (install.bat also drops this entry). --
echo [3/3] Finishing ...
if defined LEFT (
  reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\RunOnce" /v JamotongCleanup /t REG_SZ /f ^
    /d "cmd /c del /f /q \"%DEST%\*.old.*\" & rd \"%DEST%\"" >nul 2>&1
  echo      Some files are still in use by running apps; they are removed after the next sign-in.
)
echo.
echo ================================================================
echo   [OK] Uninstalled
echo ================================================================
echo   Your settings remain in %%APPDATA%%\Jamotong ^(delete that folder too if you
echo   do not plan to reinstall^).
echo.
choice /C YN /N /T 20 /D N /M "Restart Explorer now to refresh the tray/icons? [Y/N] (auto-N in 20s) "
if "%errorlevel%"=="1" (
  taskkill /F /IM explorer.exe >nul 2>&1
  runas /trustlevel:0x20000 "%SystemRoot%\explorer.exe" >nul 2>&1 || start "" "%SystemRoot%\explorer.exe"
)
pause
exit /B 0

:clean
set "D=%~1"
if not exist "%D%" exit /B 0
del /F /Q "%D%*.old.*" >nul 2>&1
for %%F in (%BINS%) do (
  if exist "%D%%%F" del /F /Q "%D%%%F" >nul 2>&1
  if exist "%D%%%F" ren "%D%%%F" "%%F.old.%RANDOM%%RANDOM%" >nul 2>&1
)
for %%F in (hanja.txt hanja_hunum.txt example.jmt example-artsey.jmt example-dvorak.jmt UNICODE-LICENSE.txt README.md README.ko.md LICENSE COPYRIGHT.md install-user.bat upgrade-user.bat uninstall-user.bat uninstall.bat) do del /F /Q "%D%%%F" >nul 2>&1
del /F /Q "%D%*.jmb" >nul 2>&1
rem built layouts (install.bat compiles the shipped .jmt files) - without this the folder never empties
if exist "%D%.staging" rd /S /Q "%D%.staging" >nul 2>&1
rd "%D%" >nul 2>&1
if exist "%D%" set "LEFT=1"
exit /B 0
