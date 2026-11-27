@echo off
REM build.bat -- compile the v3 target and the injector (ASCII + CRLF)
setlocal
cd /d "%~dp0"

echo [1/2] building target.exe
gcc -fexec-charset=GBK -g -O0 -no-pie -Wall -o target.exe target.c
if errorlevel 1 goto fail

echo [2/2] building inject.exe
gcc -fexec-charset=GBK -g -O0 -Wall -o inject.exe inject.c -lpsapi
if errorlevel 1 goto fail

echo.
echo done. next: run.bat
set /p DUMMY=press Enter to close.
exit /b 0

:fail
echo *** BUILD FAILED ***  scroll up, find the line with "error"
set /p DUMMY=press Enter to close.
exit /b 1
