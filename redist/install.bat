@echo off
setlocal EnableExtensions
title Jamotong IME - Install / Upgrade
chcp 65001 >nul 2>&1

rem ---------------------------------------------------------------------------------------------
rem  RFC-0008 W1-05 / owner decision A5 (2026-09-22): one install model.
rem   Program files go to a fixed machine-wide folder, %ProgramFiles%\Jamotong, which every app
rem   (including Store/UWP apps) can read. Settings stay per user in %APPDATA%\Jamotong.
rem   The install is one transaction: stage -> verify -> swap -> register x64 AND x86 -> check the
rem   registration points here -> commit. Any failure puts the previous files and registration back.
rem   Running this again from a newer zip is the upgrade.
rem ---------------------------------------------------------------------------------------------

set "SRC=%~dp0"
set "DEST=%ProgramFiles%\Jamotong"
set "STAGE=%DEST%\.staging"
set "CLSIDKEY=HKCR\CLSID\{C471BCF2-343F-4187-A103-24151C3E20B9}\InprocServer32"
set "BINS=jamotong.dll jamotong32.dll jamotong.exe"
set "DATA=hanja.txt hanja_hunum.txt example.jmt example-artsey.jmt example-dvorak.jmt layout-ko-2bul.jmt layout-ko-3bul-final.jmt layout-ko-3bul-sunarae.jmt layout-ko-3bul-390.jmt layout-ko-3bul-2011.jmt layout-ko-3bul-2012.jmt UNICODE-LICENSE.txt README.md README.ko.md jmt-format.md jmt-format.ko.md LICENSE COPYRIGHT.md uninstall.bat"

echo ================================================================
echo   Jamotong IME - Install / Upgrade
echo ================================================================
echo.
echo  Installs to: %DEST%
echo  To upgrade later, run install.bat from the new zip.
echo.

rem ---- 1) Checks --------------------------------------------------------------------------------
net session >nul 2>&1
if not "%errorlevel%"=="0" (
  echo [!] Administrator privileges required.
  echo     Right-click install.bat and choose "Run as administrator".
  pause
  exit /B 1
)
set "ARCH=%PROCESSOR_ARCHITECTURE%"
if defined PROCESSOR_ARCHITEW6432 set "ARCH=%PROCESSOR_ARCHITEW6432%"
if /I not "%ARCH%"=="AMD64" (
  echo [!] 64-bit Windows is required.
  pause
  exit /B 1
)
echo %SRC% | find /I "\Temp\" >nul && (
  echo [!] This looks like a TEMP folder: Explorer extracted only this file from the zip.
  echo     Extract the WHOLE zip first, then run install.bat from there.
  pause
  exit /B 1
)
if /I "%SRC%"=="%DEST%\" (
  echo [!] This is the installed copy. Run install.bat from the NEW extracted zip instead.
  pause
  exit /B 1
)
for %%F in (%BINS% %DATA%) do (
  if not exist "%SRC%%%F" (
    echo [!] %%F is missing next to install.bat. Extract the whole zip and try again.
    pause
    exit /B 1
  )
)

set "LOG=%TEMP%\jamotong-install-log.txt"
> "%LOG%" echo Jamotong install %DATE% %TIME%
>>"%LOG%" echo src: %SRC%
>>"%LOG%" echo dest: %DEST%
echo [*] Log: %LOG%

rem ---- 2) Remember the current registration (for rollback and migration) ------------------------
set "OLD64="
set "OLD32="
for /f "tokens=2,*" %%A in ('reg query "%CLSIDKEY%" /ve /reg:64 2^>nul ^| find "REG_SZ"') do set "OLD64=%%B"
for /f "tokens=2,*" %%A in ('reg query "%CLSIDKEY%" /ve /reg:32 2^>nul ^| find "REG_SZ"') do set "OLD32=%%B"
>>"%LOG%" echo previous x64: %OLD64%
>>"%LOG%" echo previous x86: %OLD32%

rem ---- 3) Stage and verify ----------------------------------------------------------------------
echo [1/5] Unblocking and staging files ...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -LiteralPath '%SRC%' -File | Unblock-File" >nul 2>&1
if not exist "%DEST%" mkdir "%DEST%" || goto :fail_nochange
if exist "%STAGE%" rd /S /Q "%STAGE%"
mkdir "%STAGE%" || goto :fail_nochange
for %%F in (%BINS% %DATA%) do (
  copy /Y "%SRC%%%F" "%STAGE%\%%F" >nul 2>&1 || (set "FAILED=%%F" & goto :fail_nochange)
  fc /B "%SRC%%%F" "%STAGE%\%%F" >nul 2>&1 || (set "FAILED=%%F" & goto :fail_nochange)
)

rem ---- 4) Swap: every file moves aside as <name>.old.<tag>, the staged one moves in --------------
echo [2/5] Replacing files ...
set "TAG=%RANDOM%%RANDOM%"
del /F /Q "%DEST%\*.old.*" >nul 2>&1
set "FAILED="
for %%F in (%BINS% %DATA%) do if not defined FAILED call :swap %%F
if defined FAILED goto :rollback

rem ---- 5) Register both architectures and check they point here ---------------------------------
echo [3/5] Registering 64-bit ...
regsvr32 /s "%DEST%\jamotong.dll"
set "RC=%errorlevel%"
>>"%LOG%" echo regsvr32 x64 exit=%RC%
if not "%RC%"=="0" (set "FAILED=64-bit registration (code %RC%)" & goto :rollback)
reg query "%CLSIDKEY%" /ve /reg:64 2>nul | find /I "%DEST%\jamotong.dll" >nul || (set "FAILED=64-bit registration path" & goto :rollback)
echo [4/5] Registering 32-bit ...
"%SystemRoot%\SysWOW64\regsvr32.exe" /s "%DEST%\jamotong32.dll"
set "RC=%errorlevel%"
>>"%LOG%" echo regsvr32 x86 exit=%RC%
if not "%RC%"=="0" (set "FAILED=32-bit registration (code %RC%)" & goto :rollback)
reg query "%CLSIDKEY%" /ve /reg:32 2>nul | find /I "%DEST%\jamotong32.dll" >nul || (set "FAILED=32-bit registration path" & goto :rollback)

rem ---- 6) Commit --------------------------------------------------------------------------------
echo [5/5] Finishing ...
del /F /Q "%DEST%\*.old.*" >nul 2>&1
rd /S /Q "%STAGE%" >nul 2>&1
rem Files of the former install models that no longer belong here
for %%F in (install.bat install-user.bat upgrade-user.bat uninstall-user.bat install-log.txt) do del /F /Q "%DEST%\%%F" >nul 2>&1
rem A pending clean-up from an earlier uninstall must not touch this new install
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\RunOnce" /v JamotongCleanup /f >nul 2>&1
>>"%LOG%" echo committed

rem ---- 7) Build the layouts ---------------------------------------------------------------------
rem The IME reads only built layouts (.jmb). Compile the sources that ship here and the ones already
rem in the machine-wide and this user's layout folders. The manager app rebuilds the rest when it runs.
echo [5/5] Building layouts ...
"%DEST%\jamotong.exe" --build-dir "%DEST%" >>"%LOG%" 2>&1
if exist "%ProgramData%\Jamotong\layouts" "%DEST%\jamotong.exe" --build-dir "%ProgramData%\Jamotong\layouts" >>"%LOG%" 2>&1
if exist "%APPDATA%\Jamotong\layouts" "%DEST%\jamotong.exe" --build-dir "%APPDATA%\Jamotong\layouts" >>"%LOG%" 2>&1

call :migrate

echo.
echo ================================================================
echo   [OK] Installed in %DEST%
echo ================================================================
echo.
echo  NEXT STEPS
echo   1) Press Win+Space and select "Jamotong IME".
echo      If it is not in the list, sign out and in once.
echo      New app windows use this build right away; apps that were already
echo      running keep the previous copy until you restart them.
echo.
echo  DEFAULT LAYOUTS: English QWERTY, Korean Dubeolsik ^(Dvorak and Sebeolsik are
echo   included but off - enable them in Settings ^> Layouts^).
echo  DEFAULT KEYS: Hangul key / Right Alt / Shift+Space switch layout, Hanja key for
echo   hanja and symbols, Ctrl+Alt+U Unicode input, Ctrl+Alt+K settings.
echo.
echo  Settings file:  %%APPDATA%%\Jamotong\config.ini
echo  Remove:         run uninstall.bat as administrator ^(a copy is in %DEST%^)
echo  Full manual:    README.md / README.ko.md
echo.
choice /C YN /N /T 20 /D N /M "Restart Explorer now to refresh the tray/icons? [Y/N] (auto-N in 20s) "
if "%errorlevel%"=="1" (
  taskkill /F /IM explorer.exe >nul 2>&1
  runas /trustlevel:0x20000 "%SystemRoot%\explorer.exe" >nul 2>&1 || start "" "%SystemRoot%\explorer.exe"
)
pause
exit /B 0

rem ================================================================================================
:swap
if exist "%DEST%\%1" (
  move /Y "%DEST%\%1" "%DEST%\%1.old.%TAG%" >nul 2>&1 || (set "FAILED=%1 (in use and cannot be moved)" & exit /B 1)
)
move /Y "%STAGE%\%1" "%DEST%\%1" >nul 2>&1 || (set "FAILED=%1" & exit /B 1)
exit /B 0

:rollback
echo.
echo [FAIL] %FAILED% - putting the previous installation back ...
>>"%LOG%" echo FAILED: %FAILED% - rollback
regsvr32 /s /u "%DEST%\jamotong.dll" >nul 2>&1
"%SystemRoot%\SysWOW64\regsvr32.exe" /s /u "%DEST%\jamotong32.dll" >nul 2>&1
for %%F in (%BINS% %DATA%) do call :restore %%F
if defined OLD64 if exist "%OLD64%" regsvr32 /s "%OLD64%"
if defined OLD32 if exist "%OLD32%" "%SystemRoot%\SysWOW64\regsvr32.exe" /s "%OLD32%"
rd /S /Q "%STAGE%" >nul 2>&1
>>"%LOG%" echo rollback done
echo        The previous state is restored. Close programs that may hold the files
echo        ^(or sign out and in^), then run install.bat again. Log: %LOG%
pause
exit /B 1

:restore
if exist "%DEST%\%1.old.%TAG%" (
  del /F /Q "%DEST%\%1" >nul 2>&1
  move /Y "%DEST%\%1.old.%TAG%" "%DEST%\%1" >nul 2>&1
) else (
  del /F /Q "%DEST%\%1" >nul 2>&1
)
exit /B 0

:fail_nochange
echo.
echo [FAIL] Could not stage %FAILED% in %DEST% - nothing was changed. Log: %LOG%
>>"%LOG%" echo FAILED staging %FAILED%
rd /S /Q "%STAGE%" >nul 2>&1
pause
exit /B 1

rem ---- An older install registered from another folder (extracted zip or the former per-user
rem      folder) is no longer used. The former per-user copy is removed; any other folder is the
rem      user's own, so it is only reported.
:migrate
if not defined OLD64 exit /B 0
for %%P in ("%OLD64%") do set "OLDDIR=%%~dpP"
if /I "%OLDDIR%"=="%DEST%\" exit /B 0
>>"%LOG%" echo migrated from %OLDDIR%
echo %OLDDIR% | find /I "\AppData\Local\Programs\Jamotong\" >nul
if errorlevel 1 (
  echo  [i] The previous installation in %OLDDIR% is no longer used - you may delete that folder.
  exit /B 0
)
echo  [i] Removing the former per-user copy in %OLDDIR% ...
for %%F in (%BINS%) do (
  if exist "%OLDDIR%%%F" del /F /Q "%OLDDIR%%%F" >nul 2>&1
  if exist "%OLDDIR%%%F" ren "%OLDDIR%%%F" "%%F.old.%TAG%" >nul 2>&1
)
for %%F in (%DATA% install-user.bat upgrade-user.bat uninstall-user.bat) do del /F /Q "%OLDDIR%%%F" >nul 2>&1
rd "%OLDDIR%" >nul 2>&1
if exist "%OLDDIR%" (
  reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\RunOnce" /v JamotongOldCopy /t REG_SZ /f /d "cmd /c del /f /q \"%OLDDIR%*.old.*\" & rd \"%OLDDIR:~0,-1%\"" >nul 2>&1
  echo      Some files are still in use; they are removed at the next sign-in.
)
exit /B 0
