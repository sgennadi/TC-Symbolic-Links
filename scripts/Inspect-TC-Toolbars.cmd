@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Inspect-TC-Toolbars.ps1" %*
pause
