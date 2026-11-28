@echo off
REM build.bat -- compile the hook target and the patcher (ASCII + CRLF)
setlocal
cd /d "%~dp0"

echo [1/2] building target.exe
gcc -fexec-charset=GBK -g -O0 -no-pie -fno-inline -Wall -o target.exe target.c
if errorlevel 1 goto fail

echo [2/2] building hook.exe
gcc -fexec-charset=GBK -g -O0 -Wall -o hook.exe hook.c -lpsapi
if errorlevel 1 goto fail

echo.
echo done. next: run.bat
set /p DUMMY=press Enter to close.
exit /b 0

:fail
echo *** BUILD FAILED ***  scroll up, find the line with "error"
set /p DUMMY=press Enter to close.
exit /b 1
