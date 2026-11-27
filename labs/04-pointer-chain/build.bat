@echo off
REM build.bat -- compile target.exe (heap sample) and chain.exe (pointer walker)
REM ASCII only + CRLF; the .c files print Chinese, so -fexec-charset=GBK is required.
setlocal
cd /d "%~dp0"

echo [1/2] building target.exe
gcc -fexec-charset=GBK -g -O0 -no-pie -Wall -o target.exe target.c
if errorlevel 1 goto fail

echo [2/2] building chain.exe
gcc -fexec-charset=GBK -g -O0 -Wall -o chain.exe chain.c -lpsapi
if errorlevel 1 goto fail

echo.
echo done. next: run.bat
set /p DUMMY=press Enter to close.
exit /b 0

:fail
echo *** BUILD FAILED ***  scroll up, find the line with "error"
set /p DUMMY=press Enter to close.
exit /b 1
