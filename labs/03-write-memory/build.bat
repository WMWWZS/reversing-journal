@echo off
REM build.bat -- compile target.exe (the subject) and memtool.exe (the tool)
REM (ASCII only on purpose: cmd.exe reads .bat in OEM codepage, UTF-8 Chinese breaks it)
setlocal
cd /d "%~dp0"

echo [1/2] building target.exe  (the live sample)
gcc -g -O0 -no-pie -Wall -o target.exe target.c
if errorlevel 1 goto fail

echo [2/2] building memtool.exe  (the reader/writer)
gcc -g -O0 -Wall -o memtool.exe memtool.c -lpsapi
if errorlevel 1 goto fail

echo.
echo done. next: run.bat
pause
exit /b 0

:fail
echo.
echo *** BUILD FAILED ***  scroll up, find the line with "error"
pause
exit /b 1
