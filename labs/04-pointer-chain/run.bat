@echo off
REM run.bat -- build, start the heap target, find the chain, read and hold
REM ASCII only + CRLF. The .c files print Chinese -> -fexec-charset=GBK.
setlocal enabledelayedexpansion
cd /d "%~dp0"

taskkill /f /im target.exe >nul 2>nul

echo [1/3] building
gcc -fexec-charset=GBK -g -O0 -no-pie -Wall -o target.exe target.c
if errorlevel 1 goto fail
gcc -fexec-charset=GBK -g -O0 -Wall -o chain.exe chain.c -lpsapi
if errorlevel 1 goto fail

echo [2/3] starting target.exe (keep it open, hp is falling)
start "lab04 target (close when done)" target.exe
ping -n 3 127.0.0.1 >nul

echo [3/3] finding the chain, then using it immediately
chain.exe find > find.txt
type find.txt
set "SLOT="
for /f "tokens=2 delims==" %%a in ('findstr /b /c:"SLOT=" find.txt') do set "SLOT=%%a"
del find.txt >nul 2>nul
if not defined SLOT goto noslot

echo.
echo ------------------------------------------------------------------
echo slot picked up automatically: %SLOT%
echo ------------------------------------------------------------------
chain.exe read %SLOT% 0
chain.exe read %SLOT% 3

echo.
echo holding hp[0] at 9999 for 6 s. watch the target window.
chain.exe hold 9999 6 %SLOT% 0

echo.
echo ------------------------------------------------------------------
echo the slot offset survives a restart; the heap addresses do not.
echo the target window is still open. press Enter to close this one.
set /p DUMMY=
exit /b 0

:noslot
echo *** could not read SLOT= from chain.exe find output ***
set /p DUMMY=press Enter to close.
exit /b 1

:fail
echo *** BUILD FAILED ***  scroll up, find the line with "error"
set /p DUMMY=press Enter to close.
exit /b 1
