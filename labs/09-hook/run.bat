@echo off
REM run.bat -- start the target, find the decrement instruction, patch it, then restore
setlocal
cd /d "%~dp0"

taskkill /f /im target.exe >nul 2>nul

echo [1/3] building
gcc -fexec-charset=GBK -g -O0 -no-pie -fno-inline -Wall -o target.exe target.c
if errorlevel 1 goto fail
gcc -fexec-charset=GBK -g -O0 -Wall -o hook.exe hook.c -lpsapi
if errorlevel 1 goto fail

echo [2/3] starting target.exe (keep it open)
start "lab09 target (close when done)" target.exe
ping -n 3 127.0.0.1 >nul

echo [3/3] scanning, patching, verifying, restoring
hook.exe

echo.
echo ------------------------------------------------------------------
echo hp freezes while patched, then drops again after restore.
echo press Enter to close (close the target yourself).
set /p DUMMY=
exit /b 0

:fail
echo *** BUILD FAILED ***  scroll up, find the line with "error"
set /p DUMMY=press Enter to close.
exit /b 1
