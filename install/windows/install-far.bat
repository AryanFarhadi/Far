@echo off
setlocal
cd /d "%~dp0..\.."

echo.
echo  ========================================
echo   Far Language - Windows Installer
echo  ========================================
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-far.ps1" %*
if %ERRORLEVEL% neq 0 (
  echo.
  echo Install failed. See messages above.
  pause
  exit /b 1
)

echo.
pause
exit /b 0
