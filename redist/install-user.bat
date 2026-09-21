@echo off
setlocal EnableExtensions
title Jamotong IME - Per-user install
echo ================================================================
echo   Jamotong IME - Per-user install
echo ================================================================
echo.
echo  Files go to %%LocalAppData%%\Programs\Jamotong for THIS account.
echo  One UAC prompt is still needed the FIRST time, because Windows
echo  keeps the TSF text-service registration machine-wide. Upgrades
echo  after that need NO admin rights - just run upgrade-user.bat.
echo  Best suited when you are the only user of this PC.
echo.
set "DEST=%LocalAppData%\Programs\Jamotong"
set "LOG=%~dp0install-user-log.txt"
> "%LOG%" echo Jamotong per-user install %DATE% %TIME%
>>"%LOG%" echo src: %~dp0
>>"%LOG%" echo dest: %DEST%

if not exist "%~dp0jamotong.dll" (
  echo [!] jamotong.dll not found next to this script. Extract the WHOLE zip first.
  >>"%LOG%" echo ERROR: jamotong.dll missing
  pause
  exit /B 1
)

echo [1/3] Copying files to %DEST% ...
if not exist "%DEST%" mkdir "%DEST%"
rem RFC-0008 W1-05: the three program files are replaced as one unit (upgrade-user.bat :copyall).
call "%~dp0upgrade-user.bat" --copy
if errorlevel 1 (
  echo [FAIL] Copying failed - nothing was changed. Close programs that may hold the files, then retry.
  >>"%LOG%" echo ERROR: copy failed
  pause
  exit /B 1
)
>>"%LOG%" echo files copied

net session >nul 2>&1
if "%errorlevel%"=="0" goto :doreg
echo [2/3] Registering - Windows will ask for administrator approval once...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c','\"%DEST%\upgrade-user.bat\" --register-only' -Verb RunAs -Wait -PassThru; exit $p.ExitCode" >nul 2>&1
goto :after

:doreg
echo [2/3] Registering (already elevated)...
call "%DEST%\upgrade-user.bat" --register-only

:after
rem RFC-0008 W1-05: 0 = OK, 2 = 64-bit OK but 32-bit failed, anything else = not registered.
set "RRC=%errorlevel%"
>>"%LOG%" echo register exit=%RRC%
if "%RRC%"=="2" echo [warn] 32-bit registration failed - 64-bit apps still work.
if not "%RRC%"=="0" if not "%RRC%"=="2" (
  echo [FAIL] Registration failed or was cancelled ^(code %RRC%^). The files are in %DEST%;
  echo        run install-user.bat again and approve the administrator prompt.
  pause
  exit /B 1
)
echo [3/3] Done.
echo.
echo  Press Win+Space and pick "Jamotong IME". If it is not listed, sign out and in.
echo  Upgrades: extract the new zip anywhere and run upgrade-user.bat - NO admin needed.
echo  Removal:  uninstall-user.bat  (one admin prompt; never delete the folder while registered)
>>"%LOG%" echo done
pause
