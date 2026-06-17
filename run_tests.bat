@echo off

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run_tests.ps1"

exit /b %ERRORLEVEL%
