@echo off
REM run.bat -- build probe, launch the toy game, then probe it
REM (ASCII only: cmd.exe reads .bat in OEM codepage, UTF-8 Chinese breaks it)
setlocal
cd /d "%~dp0"

echo [1/3] building probe.exe
gcc -O0 -g -Wall -o probe.exe probe.c -lpsapi
if errorlevel 1 goto fail

echo [2/3] starting game.exe in its own window
start "toy-game  (close me when done)" ..\01-struct-layout\game.exe
timeout /t 2 >nul

echo [3/3] probing for value 92.5
echo.
probe.exe 92.5

echo.
echo --------------------------------
echo  read the dump above, then answer the 5 questions in README
echo --------------------------------
pause
exit /b 0

:fail
echo.
echo *** BUILD FAILED ***
pause
exit /b 1
