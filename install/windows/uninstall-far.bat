@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall-far.ps1"
exit /b %ERRORLEVEL%
