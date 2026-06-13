@echo off
setlocal
cd /d "%~dp0.."

echo === Refreshing Far core API for IntelliSense ===
node vscode\scripts\generate-core-api.mjs
if %ERRORLEVEL% neq 0 exit /b 1

echo.
echo === Building VSIX (offline, no npm) ===
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-vsix.ps1"
if %ERRORLEVEL% neq 0 exit /b 1

echo.
echo === Done ===
echo VSIX: vscode\far-lang-0.2.4.vsix
echo Install: vscode\install-extension.bat
exit /b 0
