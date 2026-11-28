@echo off
REM build.bat -- compile the 32-bit-layout simulator and the config-driven walker
setlocal
cd /d "%~dp0"

echo [1/2] building target32sim.exe
gcc -fexec-charset=GBK -g -O0 -no-pie -Wall -o target32sim.exe target32sim.c
if errorlevel 1 goto fail

echo [2/2] building chaincfg.exe
gcc -fexec-charset=GBK -g -O0 -Wall -o chaincfg.exe chaincfg.c -lpsapi
if errorlevel 1 goto fail

echo.
echo done. next: run.bat
set /p DUMMY=press Enter to close.
exit /b 0

:fail
echo *** BUILD FAILED ***  scroll up, find the line with "error"
set /p DUMMY=press Enter to close.
exit /b 1
