@echo off
REM run.bat -- start the v2 target, then scan for the instruction that touches g_world
setlocal
cd /d "%~dp0"

taskkill /f /im target.exe >nul 2>nul

echo [1/3] building
gcc -fexec-charset=GBK -g -O0 -no-pie -Wall -o target.exe target.c
if errorlevel 1 goto fail
gcc -fexec-charset=GBK -g -O0 -Wall -o aob.exe aob.c -lpsapi
if errorlevel 1 goto fail

echo [2/3] starting target.exe (keep it open)
start "lab05 target v2 (close when done)" target.exe
ping -n 3 127.0.0.1 >nul

echo [3/3] scanning
aob.exe

echo.
echo ------------------------------------------------------------------
echo old offset 0x7030 is dead; the scan recomputed a new one from the instruction.
echo press Enter to close this window (close the target window yourself).
set /p DUMMY=
exit /b 0

:fail
echo *** BUILD FAILED ***  scroll up, find the line with "error"
set /p DUMMY=press Enter to close.
exit /b 1
