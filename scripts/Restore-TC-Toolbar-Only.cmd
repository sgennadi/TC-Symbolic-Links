@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Restore-TC-Toolbar-Only.ps1" %*
if errorlevel 1 pause
