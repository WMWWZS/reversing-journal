@echo off
rem ============================================================
rem  Launch Ghidra 12.2 with the JDK 25 it requires.
rem
rem  Why this file exists:
rem   - Ghidra 12.x refuses to start on JDK 21; JDK 25 is used.
rem   - Explorer runs .bat files with "cmd /K", so the console
rem     would otherwise stay open at a prompt after Ghidra starts.
rem     The final "exit" closes it.
rem ============================================================

set "JAVA_HOME=D:\tools\jdk-25\jdk-25.0.5+8"
set "GHIDRA_INSTALL_DIR=D:\tools\ghidra\ghidra_12.2_PUBLIC"

if not exist "%JAVA_HOME%\bin\java.exe" (
    echo [ERROR] JDK 25 not found: %JAVA_HOME%
    pause
    exit /b 1
)

if not exist "%GHIDRA_INSTALL_DIR%\ghidraRun.bat" (
    echo [ERROR] Ghidra not found: %GHIDRA_INSTALL_DIR%
    pause
    exit /b 1
)

call "%GHIDRA_INSTALL_DIR%\ghidraRun.bat"

rem Close the cmd /K wrapper instead of leaving a prompt behind.
exit