@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-diagnostic.ps1"
if errorlevel 1 echo Diagnostic collection failed. Take a screenshot of this window.
echo.
pause
