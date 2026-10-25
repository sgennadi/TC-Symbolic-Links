@echo off
setlocal EnableExtensions
rem Build x64 and package the TC plugin with Visual Studio 2026.
for %%I in ("%~dp0.") do set "TCROOT=%%~fI"
rem Requires CMake 4.2+ and the VS 2026 v145 C++ tools. No edition-specific paths.
cmake -S "%TCROOT%" -B "%TCROOT%\build-win" -G "Visual Studio 18 2026" -A x64 -T v145
if errorlevel 1 goto failed
cmake --build "%TCROOT%\build-win" --config Release --parallel
if errorlevel 1 goto failed
ctest --test-dir "%TCROOT%\build-win" -C Release --output-on-failure
if errorlevel 1 goto failed
if not exist "%TCROOT%\dist" mkdir "%TCROOT%\dist"
copy /Y "%TCROOT%\build-win\Release\TCSymlinkMaker.exe" "%TCROOT%\dist\" >nul
if errorlevel 1 goto failed
copy /Y "%TCROOT%\build-win\Release\SymlinkInspector.wdx64" "%TCROOT%\dist\" >nul
if errorlevel 1 goto failed
copy /Y "%TCROOT%\pluginst.inf" "%TCROOT%\dist\" >nul
if errorlevel 1 goto failed
copy /Y "%TCROOT%\README.md" "%TCROOT%\dist\" >nul
if errorlevel 1 goto failed
copy /Y "%TCROOT%\docs\MANUAL-TESTS.md" "%TCROOT%\dist\" >nul
if errorlevel 1 goto failed
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Compress-Archive -Path '%TCROOT%\dist\*' -DestinationPath '%TCROOT%\TC-Symbolic-Links-win64.zip' -Force"
if errorlevel 1 goto failed
powershell -NoProfile -Command "$h=(Get-FileHash -LiteralPath '%TCROOT%\TC-Symbolic-Links-win64.zip' -Algorithm SHA256).Hash.ToLowerInvariant(); Set-Content -Path '%TCROOT%\TC-Symbolic-Links-win64.zip.sha256' -Value ($h+'  TC-Symbolic-Links-win64.zip') -Encoding ascii"
if errorlevel 1 goto failed
echo PASS: %TCROOT%\TC-Symbolic-Links-win64.zip
echo Windows tests passed. TC GUI acceptance remains manual.
exit /b 0
:failed
echo ERROR: Windows build, tests, or packaging failed. No stable release was created.
exit /b 1
