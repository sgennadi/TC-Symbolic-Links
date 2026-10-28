@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Restore-TC-Toolbar-Icons.ps1" %*
if errorlevel 1 (
  echo.
  echo Icon recovery failed. Review the message above.
)
pause
