@echo off
REM run.bat -- start the camera target, then project all units to screen
setlocal
cd /d "%~dp0"

taskkill /f /im target.exe >nul 2>nul

echo [1/3] building
gcc -fexec-charset=GBK -g -O0 -no-pie -Wall -o target.exe target.c -lm
if errorlevel 1 goto fail
gcc -fexec-charset=GBK -g -O0 -Wall -o w2s.exe w2s.c -lpsapi -lm
if errorlevel 1 goto fail

echo [2/3] starting target.exe (it rotates its camera 30 deg/s)
start "lab10 camera target (close when done)" target.exe
ping -n 3 127.0.0.1 >nul

echo [3/3] projecting (two independent algorithms must agree)
w2s.exe

echo.
echo ------------------------------------------------------------------
echo diff must be 0 or 1 px. press Enter to close (close the target yourself).
set /p DUMMY=
exit /b 0

:fail
echo *** BUILD FAILED ***  scroll up, find the line with "error"
set /p DUMMY=press Enter to close.
exit /b 1
