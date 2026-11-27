# Lab 01 一键构建
# 用法（推荐）：
#   pwsh -File .\build.ps1
# Windows PowerShell 5.1 也支持（文件已带 UTF-8 BOM）。

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot

try {
    Write-Host '=== 编译 -O0（未优化，适合读汇编） ===' -ForegroundColor Cyan
    gcc -O0 -g -fno-asynchronous-unwind-tables -o player_O0.exe player.c

    Write-Host '=== 编译 -O2（优化后，真实游戏形态） ===' -ForegroundColor Cyan
    gcc -O2 -fno-asynchronous-unwind-tables -o player_O2.exe player.c

    Write-Host '=== 编译 offsets（查看结构体布局） ===' -ForegroundColor Cyan
    gcc -o offsets.exe offsets.c

    Write-Host ''
    Write-Host '=== 结构体布局 ===' -ForegroundColor Green
    .\offsets.exe

    Write-Host ''
    Write-Host '=== 运行结果 ===' -ForegroundColor Green
    .\player_O0.exe

    Write-Host ''
    Write-Host '构建完成。' -ForegroundColor Green
    Write-Host ''
    Write-Host '查看反汇编：' -ForegroundColor Yellow
    Write-Host '  objdump -d -M intel --no-show-raw-insn player_O0.exe'
    Write-Host '调试：' -ForegroundColor Yellow
    Write-Host '  gdb -q -batch -x demo.gdb player_O0.exe'
}
finally {
    Pop-Location
}
