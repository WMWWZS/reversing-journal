@echo off
REM run.bat -- start the v3 target, then inject a keep-alive thread into it
setlocal
cd /d "%~dp0"

taskkill /f /im target.exe >nul 2>nul

echo [1/3] building
gcc -fexec-charset=GBK -g -O0 -no-pie -Wall -o target.exe target.c
if errorlevel 1 goto fail
gcc -fexec-charset=GBK -g -O0 -Wall -o inject.exe inject.c -lpsapi
if errorlevel 1 goto fail

echo [2/3] starting target.exe (keep it open, hp is falling)
start "lab06 target v3 (close when done)" target.exe
ping -n 3 127.0.0.1 >nul

echo [3/3] injecting
inject.exe

echo.
echo ------------------------------------------------------------------
echo look at the target window: hp stays at 9999 while frames keep counting.
echo press Enter to close this window (close the target yourself to stop the thread).
set /p DUMMY=
exit /b 0

:fail
echo *** BUILD FAILED ***  scroll up, find the line with "error"
set /p DUMMY=press Enter to close.
exit /b 1
