# 启动 Ghidra
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File .\tools\ghidra.ps1
#
# 说明：
#   Ghidra 12.x 要求 JDK 25+，本机 JDK 25 装在 D:\tools\jdk-25。
#   这里显式设置 JAVA_HOME，避免用到系统里其他的旧 JDK。

$ErrorActionPreference = 'Stop'

$GhidraHome = if ($env:GHIDRA_INSTALL_DIR) { $env:GHIDRA_INSTALL_DIR } else { 'D:\tools\ghidra\ghidra_12.2_PUBLIC' }
$JavaHome   = 'D:\tools\jdk-25\jdk-25.0.5+8'

$runBat = Join-Path $GhidraHome 'ghidraRun.bat'
if (-not (Test-Path $runBat)) {
    Write-Host "找不到 Ghidra: $runBat" -ForegroundColor Red
    Write-Host "请设置 GHIDRA_INSTALL_DIR 环境变量指向解压后的 Ghidra 目录。"
    exit 1
}
if (-not (Test-Path (Join-Path $JavaHome 'bin\java.exe'))) {
    Write-Host "找不到 JDK 25: $JavaHome" -ForegroundColor Red
    exit 1
}

$env:JAVA_HOME = $JavaHome
$env:PATH = "$JavaHome\bin;$env:PATH"

Write-Host "Ghidra : $GhidraHome"
Write-Host "JDK    : $JavaHome"
Write-Host "启动中..."
Start-Process -FilePath $runBat -WorkingDirectory $GhidraHome
