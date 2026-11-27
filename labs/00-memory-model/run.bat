@echo off
REM run.bat -- build and run step1..step4 (ASCII only, CRLF line endings)
setlocal
cd /d "%~dp0"

where gcc >nul 2>nul
if errorlevel 1 goto nogcc

echo [1/4] building step1.exe
gcc -fexec-charset=GBK -g -O0 -Wall -o step1.exe step1.c
if errorlevel 1 goto fail

echo [2/4] building step2.exe
gcc -fexec-charset=GBK -g -O0 -Wall -o step2.exe step2.c
if errorlevel 1 goto fail

echo [3/4] building step3.exe
gcc -fexec-charset=GBK -g -O0 -Wall -o step3.exe step3.c
if errorlevel 1 goto fail

echo [4/4] building step4.exe
gcc -fexec-charset=GBK -g -O0 -Wall -o step4.exe step4.c
if errorlevel 1 goto fail

echo.
echo ================ step1: address / pointer / bytes ================
step1.exe
echo.
echo ================ step2: offset is a sum =========================
step2.exe
echo.
echo ================ step3: p = and-hp copies a number ==============
step3.exe
echo.
echo ================ step4: what a float looks like =================
step4.exe
echo.
echo ----------------------------------------------------------------
echo done. press Enter to close this window.
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
