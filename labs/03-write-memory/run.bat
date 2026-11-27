@echo off
REM run.bat -- build, launch the live sample, then run three passes against it
REM (ASCII only: cmd.exe reads .bat in OEM codepage, UTF-8 Chinese breaks it)
setlocal
cd /d "%~dp0"

echo [1/4] building
gcc -g -O0 -no-pie -Wall -o target.exe target.c
if errorlevel 1 goto fail
gcc -g -O0 -Wall -o memtool.exe memtool.c -lpsapi
if errorlevel 1 goto fail

echo [2/4] starting target.exe in its own window -- KEEP AN EYE ON IT
start "live sample (watch the numbers)" target.exe
timeout /t 2 >nul

echo.
echo [3/4] pass 1: read only
memtool.exe read

echo.
echo [4/4] pass 2: write once, then read again 2 s later
memtool.exe write 9999
timeout /t 2 >nul
echo.
memtool.exe read
echo  ^^^ the value is back down. one write is not enough.

echo.
echo pass 3: hold for 10 s. watch the target window while it runs.
pause
memtool.exe hold 9999 10

echo.
echo ------------------------------------------------------------------
echo  done. press ESC in the target window to stop it.
echo ------------------------------------------------------------------
pause
exit /b 0

:fail
echo.
echo *** BUILD FAILED ***
pause
exit /b 1
