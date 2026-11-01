@echo off
setlocal
rem Detect the Total Commander process (TOTALCMD64.EXE or TOTALCMD.EXE)
rem and install only its WDX architecture. Run from an extracted whole ZIP.
set "TCS_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%TCS_PS%" (
  echo Windows PowerShell 5.1 was not found.
  exit /b 2
)
"%TCS_PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install_TC_Symbolic_Links.ps1" %*
exit /b %ERRORLEVEL%
