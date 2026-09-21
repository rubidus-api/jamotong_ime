@echo off
setlocal EnableExtensions
title Jamotong IME - Per-user upgrade (no admin)
set "DEST=%LocalAppData%\Programs\Jamotong"
set "SRC=%~dp0"
set "CLSIDKEY=HKCR\CLSID\{C471BCF2-343F-4187-A103-24151C3E20B9}\InprocServer32"

if "%~1"=="--register-only" goto :register
if "%~1"=="--copy" goto :copyonly

echo ================================================================
echo   Jamotong IME - Per-user upgrade  (no admin needed)
echo ================================================================
echo  Copies new files over %DEST%. Registration stays valid because
echo  the path does not change. Sign out and in so running apps load
echo  the new DLL.
echo.
if not exist "%SRC%jamotong.dll" (
  echo [!] jamotong.dll not found next to this script. Run this from the NEW extracted zip.
  pause
  exit /B 1
)
if not exist "%DEST%\jamotong.dll" (
  echo [!] No per-user installation found at %DEST%. Run install-user.bat first.
  pause
  exit /B 1
)
if /I "%SRC%"=="%DEST%\" (
  echo [!] This is the installed copy. Run upgrade-user.bat from the NEW extracted zip instead.
  pause
  exit /B 1
)
call :copyall
if errorlevel 1 (
  echo.
  echo [FAIL] Upgrade failed at %FAILED% - the previous version was restored.
  echo        Close programs that may hold the file, or sign out and in, then run this again.
  pause
  exit /B 1
)
echo.
echo Done. Sign out and in to load the new build everywhere.
pause
exit /B 0

:copyonly
rem Called by install-user.bat after it created %DEST%.
call :copyall
exit /B %errorlevel%

rem ---- RFC-0008 W1-05: replace the three program files as one unit -------------------
rem  A loaded DLL cannot be overwritten, but it can be renamed. Each file is moved aside to
rem  <name>.old.<tag>, the new one is copied in and compared byte for byte. If any step fails,
rem  every file already replaced goes back, so the folder never holds a mix of versions.
rem  Leftover *.old.* files that are still loaded are removed on the next run.
:copyall
set "FAILED="
set "TAG=%RANDOM%%RANDOM%"
del /F /Q "%DEST%\*.old.*" >nul 2>&1
for %%F in (jamotong.dll jamotong32.dll jamotong.exe) do (
  if not defined FAILED call :swap %%F
)
if defined FAILED goto :rollback
for %%F in (hanja.txt hanja_hunum.txt example.jmt example-artsey.jmt example-dvorak.jmt UNICODE-LICENSE.txt README.md README.ko.md LICENSE COPYRIGHT.md uninstall-user.bat upgrade-user.bat) do (
  if exist "%SRC%%%F" if /I not "%SRC%%%F"=="%DEST%\%%F" copy /Y "%SRC%%%F" "%DEST%\" >nul
)
del /F /Q "%DEST%\*.old.*" >nul 2>&1
exit /B 0

:swap
if not exist "%SRC%%1" exit /B 0
if exist "%DEST%\%1" (
  move /Y "%DEST%\%1" "%DEST%\%1.old.%TAG%" >nul 2>&1 || (set "FAILED=%1" & exit /B 1)
)
copy /Y "%SRC%%1" "%DEST%\%1" >nul 2>&1 || (set "FAILED=%1" & exit /B 1)
fc /B "%SRC%%1" "%DEST%\%1" >nul 2>&1 || (set "FAILED=%1" & exit /B 1)
echo   %1 updated
exit /B 0

:rollback
for %%F in (jamotong.dll jamotong32.dll jamotong.exe) do (
  if exist "%DEST%\%%F.old.%TAG%" (
    del /F /Q "%DEST%\%%F" >nul 2>&1
    move /Y "%DEST%\%%F.old.%TAG%" "%DEST%\%%F" >nul 2>&1
  )
)
exit /B 1

:register
rem Called elevated by install-user.bat - register the per-user copies machine-wide.
rem RFC-0008 W1-05: check both results and that the registration points at this folder.
regsvr32 /s "%DEST%\jamotong.dll"
if errorlevel 1 (
  echo [FAIL] 64-bit registration failed.
  exit /B 1
)
reg query "%CLSIDKEY%" /ve 2>nul | find /I "%DEST%\jamotong.dll" >nul
if errorlevel 1 (
  echo [FAIL] Registration does not point at %DEST%\jamotong.dll.
  exit /B 1
)
if exist "%SystemRoot%\SysWOW64\regsvr32.exe" if exist "%DEST%\jamotong32.dll" (
  "%SystemRoot%\SysWOW64\regsvr32.exe" /s "%DEST%\jamotong32.dll"
  if errorlevel 1 (
    echo [warn] 32-bit registration failed - 64-bit apps still work.
    exit /B 2
  )
)
exit /B 0
