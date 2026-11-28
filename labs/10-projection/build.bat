@echo off
REM build.bat -- compile the camera target and the projection tool (ASCII + CRLF)
setlocal
cd /d "%~dp0"

echo [1/2] building target.exe
gcc -fexec-charset=GBK -g -O0 -no-pie -Wall -o target.exe target.c -lm
if errorlevel 1 goto fail

echo [2/2] building w2s.exe
gcc -fexec-charset=GBK -g -O0 -Wall -o w2s.exe w2s.c -lpsapi -lm
if errorlevel 1 goto fail

echo.
echo done. next: run.bat
set /p DUMMY=press Enter to close.
exit /b 0

:fail
echo *** BUILD FAILED ***  scroll up, find the line with "error"
set /p DUMMY=press Enter to close.
exit /b 1
