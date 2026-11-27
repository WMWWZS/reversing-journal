@echo off
REM run.bat -- double-click me: build + run + stay open
REM (ASCII only on purpose: cmd.exe reads .bat in OEM codepage, UTF-8 Chinese breaks it)
setlocal
cd /d "%~dp0"
chcp 65001 >nul

set CFLAGS=-g -O0 -no-pie -Wall

echo ================================
echo  [1/3] compiling offsets.exe
echo ================================
gcc %CFLAGS% -o offsets.exe offsets.c
if errorlevel 1 goto fail

echo.
echo ================================
echo  [2/3] compiling game.exe
echo ================================
gcc %CFLAGS% -o game.exe game.c
if errorlevel 1 goto fail

echo.
echo ================================
echo  [3/3] running offsets.exe
echo ================================
echo.
offsets.exe

echo.
echo --------------------------------
echo  done. copy or screenshot the output above.
echo --------------------------------
pause
exit /b 0

:fail
echo.
echo *** BUILD FAILED ***  scroll up and find the line with "error"
pause
exit /b 1
