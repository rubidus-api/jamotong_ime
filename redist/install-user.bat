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
copy /Y "%~dp0jamotong.dll"   "%DEST%\" >nul && echo        jamotong.dll
copy /Y "%~dp0jamotong32.dll" "%DEST%\" >nul && echo        jamotong32.dll
copy /Y "%~dp0jamotong.exe"   "%DEST%\" >nul && echo        jamotong.exe
for %%F in (hanja.txt hanja_hunum.txt example.jmt example-artsey.jmt example-dvorak.jmt UNICODE-LICENSE.txt README.md README.ko.md LICENSE COPYRIGHT.md uninstall-user.bat upgrade-user.bat) do (
  if exist "%~dp0%%F" copy /Y "%~dp0%%F" "%DEST%\" >nul
)
>>"%LOG%" echo files copied

net session >nul 2>&1
if "%errorlevel%"=="0" goto :doreg
echo [2/3] Registering - Windows will ask for administrator approval once...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath 'cmd.exe' -ArgumentList '/c','\"%DEST%\upgrade-user.bat\" --register-only' -Verb RunAs -Wait" >nul 2>&1
goto :after

:doreg
echo [2/3] Registering (already elevated)...
call "%DEST%\upgrade-user.bat" --register-only

:after
echo [3/3] Done.
echo.
echo  Press Win+Space and pick "Jamotong IME". If it is not listed, sign out and in.
echo  Upgrades: extract the new zip anywhere and run upgrade-user.bat - NO admin needed.
echo  Removal:  uninstall-user.bat  (one admin prompt; never delete the folder while registered)
>>"%LOG%" echo done
pause
