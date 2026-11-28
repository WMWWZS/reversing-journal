@echo off
REM run.bat -- start the 32-bit-layout simulator, then walk its chain with ptr_size=4
setlocal
cd /d "%~dp0"

taskkill /f /im target32sim.exe >nul 2>nul

echo [1/3] building
gcc -fexec-charset=GBK -g -O0 -no-pie -Wall -o target32sim.exe target32sim.c
if errorlevel 1 goto fail
gcc -fexec-charset=GBK -g -O0 -Wall -o chaincfg.exe chaincfg.c -lpsapi
if errorlevel 1 goto fail

echo [2/3] starting target32sim.exe
start "lab08 32-bit layout sim (close when done)" target32sim.exe
ping -n 3 127.0.0.1 >nul

echo [3/3] reading with ptr_size=4, then with the wrong width
chaincfg.exe read config-32.txt
echo.
echo --- now on purpose with ptr_size=8 (should fail): ---
chaincfg.exe read config-32-bad.txt

echo.
echo ------------------------------------------------------------------
echo same exe, two configs. press Enter to close (close the target yourself).
set /p DUMMY=
exit /b 0

:fail
echo *** BUILD FAILED ***  scroll up, find the line with "error"
set /p DUMMY=press Enter to close.
exit /b 1
