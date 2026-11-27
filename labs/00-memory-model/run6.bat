@echo off
REM run6.bat -- start the lab04 target, then walk its pointer chain by hand (ASCII, CRLF)
setlocal
cd /d "%~dp0"

taskkill /f /im target.exe >nul 2>nul

where gcc >nul 2>nul
if errorlevel 1 goto nogcc

echo [1/2] building step6.exe
gcc -fexec-charset=GBK -g -O0 -Wall -o step6.exe step6.c -lpsapi
if errorlevel 1 goto fail

echo [2/2] starting the lab04 target, then walking the chain
start "lab04 target (close when done)" ..\04-pointer-chain\target.exe
ping -n 3 127.0.0.1 >nul

echo.
step6.exe
echo.
echo ----------------------------------------------------------------
echo three reads, two hops, one offset. that is the whole pointer chain.
echo the target window is still open. press Enter to close this one.
set /p DUMMY=
exit /b 0

:nogcc
echo *** gcc not found in PATH ***
set /p DUMMY=press Enter to close.
exit /b 1

:fail
echo *** BUILD FAILED ***  scroll up, find the line with "error"
set /p DUMMY=press Enter to close.
exit /b 1
