@echo off
REM run5.bat -- two processes, one holds a float, the other copies it (ASCII only, CRLF)
setlocal
cd /d "%~dp0"

REM a leftover target locks its exe and blocks relinking; clear it first
taskkill /f /im step5-target.exe >nul 2>nul
taskkill /f /im step5-reader.exe >nul 2>nul
if exist where.txt del where.txt >nul 2>nul

where gcc >nul 2>nul
if errorlevel 1 goto nogcc

echo [1/3] building step5-target.exe
gcc -fexec-charset=GBK -g -O0 -no-pie -Wall -o step5-target.exe step5-target.c
if errorlevel 1 goto fail

echo [2/3] building step5-reader.exe
gcc -fexec-charset=GBK -g -O0 -Wall -o step5-reader.exe step5-reader.c -lpsapi
if errorlevel 1 goto fail

echo [3/3] starting the target, waiting for where.txt
start "step5 target (close it when done)" step5-target.exe
ping -n 3 127.0.0.1 >nul

if not exist where.txt goto nowhere

echo.
echo ============== reading 4 bytes from another process ==============
step5-reader.exe
echo.
echo ----------------------------------------------------------------
echo the target window is still open. press Enter to close this one.
set /p DUMMY=
exit /b 0

:nowhere
echo.
echo *** where.txt was not created ***
echo start step5-target.exe manually (another window), then run: step5-reader.exe
set /p DUMMY=press Enter to close.
exit /b 1

:nogcc
echo *** gcc not found in PATH ***
set /p DUMMY=press Enter to close.
exit /b 1

:fail
echo *** BUILD FAILED ***  scroll up, find the line with "error"
set /p DUMMY=press Enter to close.
exit /b 1
