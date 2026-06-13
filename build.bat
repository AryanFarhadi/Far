@echo off
clang++ -std=c++17 -O2 -Isrc src\error.cpp src\lexer.cpp src\parser.cpp src\modules.cpp src\comptime.cpp src\macros.cpp src\types.cpp src\type_desc.cpp src\functions.cpp src\generics.cpp src\aggregate.cpp src\aggregate_codegen.cpp src\geom_class.cpp src\collections.cpp src\collection_codegen.cpp src\string_methods.cpp src\string_codegen.cpp src\object_model.cpp src\object_codegen.cpp src\memory.cpp src\memory_codegen.cpp src\concurrency.cpp src\concurrency_codegen.cpp src\errors.cpp src\errors_codegen.cpp src\pattern.cpp src\pattern_codegen.cpp src\typecheck.cpp src\builtins.cpp src\far_stdlib.cpp src\far_stdlib_modules.cpp src\far_science.cpp src\far_net.cpp src\far_modern.cpp src\far_security.cpp src\far_perf.cpp src\far_io.cpp src\codegen.cpp src\target.cpp src\main.cpp -o far.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
echo Built far.exe

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install\windows\sync-far-install.ps1"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
