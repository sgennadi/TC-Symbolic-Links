@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Restore-TC-Buttons-0.2.1.ps1" %*
if errorlevel 1 (
  echo.
  echo Restore did not complete. Check the error above.
)
pause
