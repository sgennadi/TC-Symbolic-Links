@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Restore-TC-Missing-Commands.ps1" %*
if errorlevel 1 (
  echo.
  echo Recovery failed; inspect the error above.
)
pause
