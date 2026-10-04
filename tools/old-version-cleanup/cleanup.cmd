@echo off
rem Removes what older Jamotong versions left on this PC - see README.txt.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0jamotong-cleanup.ps1" %*
