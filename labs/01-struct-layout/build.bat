@echo off
REM build.bat -- compile only
REM (ASCII only on purpose: cmd.exe reads .bat in OEM codepage, UTF-8 Chinese breaks it)
setlocal
cd /d "%~dp0"

set CFLAGS=-g -O0 -no-pie -Wall

echo [1/2] building offsets.exe
gcc %CFLAGS% -o offsets.exe offsets.c
if errorlevel 1 goto fail

echo [2/2] building game.exe
gcc %CFLAGS% -o game.exe game.c
if errorlevel 1 goto fail

echo.
echo done. next: run.bat  (or: offsets.exe)
pause
exit /b 0

:fail
echo.
echo *** BUILD FAILED ***  scroll up and find the line with "error"
pause
exit /b 1
