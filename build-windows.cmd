@echo off
setlocal EnableExtensions
rem Build both Total Commander WDX architectures using Visual Studio 2026.
rem No companion EXE is installed: x86 uses .wdx; x64 uses .wdx64.
for %%I in ("%~dp0.") do set "TCROOT=%%~fI"
rem Requires CMake 4.2+ and Visual Studio 2026 v145 C++ tools.
cmake -S "%TCROOT%" -B "%TCROOT%\build-win" -G "Visual Studio 18 2026" -A x64 -T v145
if errorlevel 1 goto failed
cmake --build "%TCROOT%\build-win" --config Release --parallel
if errorlevel 1 goto failed
ctest --test-dir "%TCROOT%\build-win" -C Release --output-on-failure
if errorlevel 1 goto failed
cmake -S "%TCROOT%" -B "%TCROOT%\build-win32" -G "Visual Studio 18 2026" -A Win32 -T v145
if errorlevel 1 goto failed
cmake --build "%TCROOT%\build-win32" --config Release --parallel
if errorlevel 1 goto failed
ctest --test-dir "%TCROOT%\build-win32" -C Release --output-on-failure
if errorlevel 1 goto failed

if exist "%TCROOT%\dist" rmdir /s /q "%TCROOT%\dist"
mkdir "%TCROOT%\dist"
if errorlevel 1 goto failed
copy /Y "%TCROOT%\build-win\Release\SymlinkInspector.wdx64" "%TCROOT%\dist\" >nul
if errorlevel 1 goto failed
copy /Y "%TCROOT%\build-win32\Release\SymlinkInspector.wdx" "%TCROOT%\dist\" >nul
if errorlevel 1 goto failed
copy /Y "%TCROOT%\pluginst.inf" "%TCROOT%\dist\" >nul
if errorlevel 1 goto failed
copy /Y "%TCROOT%\README.md" "%TCROOT%\dist\" >nul
if errorlevel 1 goto failed
copy /Y "%TCROOT%\docs\MANUAL-TESTS.md" "%TCROOT%\dist\" >nul
if errorlevel 1 goto failed
mkdir "%TCROOT%\dist\Recovery"
if errorlevel 1 goto failed
copy /Y "%TCROOT%\scripts\Restore-TC-*.cmd" "%TCROOT%\dist\Recovery\" >nul
if errorlevel 1 goto failed
copy /Y "%TCROOT%\scripts\Restore-TC-*.ps1" "%TCROOT%\dist\Recovery\" >nul
if errorlevel 1 goto failed
copy /Y "%TCROOT%\scripts\Inspect-TC-Toolbars.*" "%TCROOT%\dist\Recovery\" >nul
if errorlevel 1 goto failed
copy /Y "%TCROOT%\docs\REGRESSION-0.2.1.md" "%TCROOT%\dist\Recovery\" >nul
if errorlevel 1 goto failed
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Compress-Archive -Path '%TCROOT%\dist\*' -DestinationPath '%TCROOT%\TC-Symbolic-Links-win32-win64.zip' -Force"
if errorlevel 1 goto failed
powershell -NoProfile -Command "$h=(Get-FileHash -LiteralPath '%TCROOT%\TC-Symbolic-Links-win32-win64.zip' -Algorithm SHA256).Hash.ToLowerInvariant(); Set-Content -Path '%TCROOT%\TC-Symbolic-Links-win32-win64.zip.sha256' -Value ($h+'  TC-Symbolic-Links-win32-win64.zip') -Encoding ascii"
if errorlevel 1 goto failed
echo PASS: %TCROOT%\TC-Symbolic-Links-win32-win64.zip
echo Both Windows architectures and native tests passed.
echo Real Total Commander GUI and SMB acceptance must be verified separately.
exit /b 0
:failed
echo ERROR: Windows build, tests, or packaging failed. No stable release was created.
exit /b 1
