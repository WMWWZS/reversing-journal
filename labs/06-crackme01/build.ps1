# Lab 06 一键构建
# 用法（推荐）：
#   pwsh -File .\build.ps1
# Windows PowerShell 5.1 也支持（文件已带 UTF-8 BOM）。

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot

try {
    Write-Host '=== 编译 -O0 -g（带符号，入门难度） ===' -ForegroundColor Cyan
    gcc -O0 -g -fno-asynchronous-unwind-tables -o crackme_O0.exe crackme01.c

    Write-Host '=== 编译 -O2（无符号，真实游戏的形态） ===' -ForegroundColor Cyan
    gcc -O2 -fno-asynchronous-unwind-tables -o crackme_O2.exe crackme01.c

    Write-Host '=== 编译 -O2 -s（剥符号，困难模式） ===' -ForegroundColor Cyan
    gcc -O2 -s -fno-asynchronous-unwind-tables -o crackme_O2s.exe crackme01.c

    Write-Host ''
    Write-Host '=== 三个文件的体积对比 ===' -ForegroundColor Green
    Get-ChildItem *.exe | Select-Object Name, Length | Format-Table -AutoSize

    Write-Host '=== 导入表（-O2 -s 版） ===' -ForegroundColor Green
    objdump -p crackme_O2s.exe | Select-String 'DLL Name|GetEnvironment|strlen|fgets|printf|puts'

    Write-Host ''
    Write-Host '构建完成。' -ForegroundColor Green
    Write-Host ''
    Write-Host '自己试一把：' -ForegroundColor Yellow
    Write-Host '  echo 12345678 | .\crackme_O2s.exe'
    Write-Host ''
    Write-Host '看反汇编：' -ForegroundColor Yellow
    Write-Host '  objdump -d -M intel --no-show-raw-insn crackme_O0.exe'
}
finally {
    Pop-Location
}
