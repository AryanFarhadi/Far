@echo off
setlocal
cd /d "%~dp0"

if not exist far-lang-0.2.1.vsix (
  if not exist far-lang-0.2.0.vsix (
    if not exist far-lang-0.1.0.vsix (
      echo VSIX not found. Running package-extension.bat first...
      call "%~dp0package-extension.bat"
      if %ERRORLEVEL% neq 0 exit /b 1
    )
  )
)

set VSIX=
for /f "delims=" %%f in ('dir /b /o-d far-lang-*.vsix 2^>nul') do (
  if not defined VSIX set "VSIX=%~dp0%%f"
)
if "%VSIX%"=="" (
  echo ERROR: no far-lang-*.vsix found in vscode\
  exit /b 1
)

echo Installing %VSIX% ...
code --install-extension "%VSIX%" --force
if %ERRORLEVEL% neq 0 (
  echo.
  echo If 'code' is not on PATH, install manually:
  echo   1. Open VS Code
  echo   2. Extensions ^> ... ^> Install from VSIX
  echo   3. Select: %VSIX%
  exit /b 1
)

echo.
echo Registering Windows .far file icon (orange F, same as VS Code)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\register-far-windows.ps1" -SourceDir "%~dp0"
if %ERRORLEVEL% neq 0 (
  echo WARNING: Windows file icon registration failed.
) else (
  echo Windows Explorer will show .far files with the orange Far icon.
)

echo.
echo Far Language extension installed.
echo Reload VS Code, then run: Far: Setup Compiler
exit /b 0
