@echo off
setlocal EnableExtensions
title Jamotong IME - Install
chcp 65001 >nul 2>&1

echo ================================================================
echo   Jamotong IME - Install  (TSF text service)
echo ================================================================
echo.
echo  This folder becomes the install location - the IME loads its
echo  files from here. Keep it in a permanent place, for example:
echo    C:\Program Files\Jamotong
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

REM ---- 1b) Log next to this file + show where we are -------------
set "LOG=%~dp0install-log.txt"
> "%LOG%" echo Jamotong install log %DATE% %TIME%
>>"%LOG%" echo folder: %~dp0
echo [*] Install folder: %~dp0
echo [*] Log file      : %LOG%
echo.
echo %~dp0 | find /I "\Temp\" >nul && (
  echo [!] This looks like a TEMP folder. If you double-clicked install.bat INSIDE the zip,
  echo     Explorer extracted only this file. Extract the WHOLE zip to a permanent folder
  echo     ^(e.g. C:\Jamotong^) first, then run install.bat from there.
  >>"%LOG%" echo ERROR: running from TEMP folder ^(zip not extracted^)
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
  echo     Files here:
  dir /B "%~dp0"
  >>"%LOG%" echo ERROR: jamotong.dll not found
  pause
  exit /B 1
)

REM ---- 3b) Sweep *.old.* leftovers from a previous version ---------
REM  uninstall.bat moves locked binaries aside instead of requiring a
REM  sign-out; whatever is no longer mapped gets deleted here.
del /F /Q "%~dp0*.old.*" >nul 2>&1

REM ---- 4) Unblock downloaded files (Mark-of-the-Web) --------------
echo [*] Unblocking downloaded files ...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -LiteralPath '%~dp0' -Recurse -File | Unblock-File" >nul 2>&1

REM ---- 5) Register 64-bit TSF DLL ---------------------------------
echo [*] Registering 64-bit:  regsvr32 jamotong.dll
regsvr32 /s "%~dp0jamotong.dll"
set "RC=%errorlevel%"
>>"%LOG%" echo regsvr32 x64 exit=%RC%
if not "%RC%"=="0" (
  echo.
  echo [FAIL] 64-bit registration failed ^(code %RC%^).
  if "%RC%"=="3" echo   code 3 = LoadLibrary failed: file blocked/quarantined, or a dependency is missing
  if "%RC%"=="4" echo   code 4 = DllRegisterServer entry point not found: wrong/corrupt file
  if "%RC%"=="5" echo   code 5 = DllRegisterServer returned an error: registry/TSF profile registration denied
  echo   Showing the exact Windows message now ^(close the dialog to continue^)...
  regsvr32 "%~dp0jamotong.dll"
  echo   - Check antivirus quarantine / file unblock, then retry.
  echo.
  pause
  exit /B 1
)
echo    [OK] 64-bit registered.
>>"%LOG%" echo OK x64

REM ---- 6) Register 32-bit TSF DLL (for 32-bit apps) ---------------
if exist "%~dp0jamotong32.dll" (
  echo [*] Registering 32-bit:  SysWOW64\regsvr32 jamotong32.dll
  "%SystemRoot%\SysWOW64\regsvr32.exe" /s "%~dp0jamotong32.dll"
  >>"%LOG%" echo regsvr32 x86 exit=%errorlevel%
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
echo.
echo  NEXT STEPS
echo   1) Press Win+Space and select "Jamotong IME".
echo      If it is NOT in the list: the profile was registered machine-wide but your account
echo      list was not updated. Run the tsfdoctor "2-fix-enable.bat" WITHOUT admin, or sign out/in.
echo      - New app windows use this build right away. Apps that were
echo        already running keep the previous copy until you restart
echo        them ^(sign out/in only if the IME is missing from the list^).
echo.
echo  DEFAULT LAYOUTS  ^(enabled out of the box^)
echo   - English QWERTY
echo   - Korean Dubeolsik ^(2-beolsik^)
echo   Dvorak and Sebeolsik-final are included but turned off.
echo   Enable them in Settings ^> Layouts.
echo.
echo  DEFAULT KEYS  ^(all changeable, multiple bindings per function^)
echo   - Switch layout ^(Kor/Eng^) : Hangul key / Right Alt / Shift+Space
echo   - Hanja / symbols          : Hanja key ^(on a composing syllable,
echo                                or on selected text^)
echo   - Unicode codepoint input  : Ctrl+Alt+U
echo   - Open settings            : Ctrl+Alt+K  ^(or run jamotong.exe^)
echo.
echo  Settings file:  %%APPDATA%%\Jamotong\config.ini
echo  Full manual:    README.md ^(English^) / README.ko.md ^(Korean^)
echo.

REM ---- Optional: restart Explorer so tray/profile icons refresh ----
REM  De-elevated via runas /trustlevel so the new shell does not
REM  inherit administrator rights from this script.
choice /C YN /N /T 20 /D N /M "Restart Explorer now to refresh the tray/icons? [Y/N] (auto-N in 20s) "
if "%errorlevel%"=="1" (
  echo [*] Restarting Explorer ...
  taskkill /F /IM explorer.exe >nul 2>&1
  runas /trustlevel:0x20000 "%SystemRoot%\explorer.exe" >nul 2>&1 || start "" "%SystemRoot%\explorer.exe"
)
pause
