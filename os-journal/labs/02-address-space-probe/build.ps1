# Lab 02 一键构建：编译 osprobe.exe 并把 PE 头里「申请」的地址打出来
# 用法（推荐）：
#   powershell -ExecutionPolicy Bypass -File .\build.ps1
# 文件带 UTF-8 BOM，Windows PowerShell 5.1 也能正确读中文。

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot

try {
    New-Item -ItemType Directory -Force -Path .\evidence | Out-Null

    Write-Host '=== 编译：gcc -O0 -g -Wall -Wextra ===' -ForegroundColor Cyan
    # -O0 -g：不优化 + 带调试信息。这样反汇编还能和 C 源码对得上，
    #          也方便你把同一个 exe 丢进 Ghidra / x64dbg 继续看。
    # 只链接 Win32 API（KERNEL32）和 gcc 自带的 C 运行时，没有第三方库。
    gcc -O0 -g -Wall -Wextra -o osprobe.exe osprobe.c
    if ($LASTEXITCODE -ne 0) { throw "gcc failed with exit code $LASTEXITCODE" }

    Write-Host ''
    Write-Host '=== 产物 ===' -ForegroundColor Green
    Get-Item .\osprobe.exe | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize

    Write-Host '=== PE 头：ImageBase 是「申请」，不是「实际」 ===' -ForegroundColor Green
    & objdump -p .\osprobe.exe | Select-String -Pattern 'ImageBase|SizeOfImage|DllCharacteristics' |
        ForEach-Object { $_.Line }
    Write-Host '  (DllCharacteristics 的 0x40 位 = DYNAMIC_BASE，允许加载器搬走这个镜像，'
    Write-Host '   这就是 ASLR 的前提。实际加载到哪，由 osprobe.exe [A] 的 A1 自己打印。)'

    Write-Host ''
    Write-Host '=== 导入表：只应有 KERNEL32 + C 运行时 ===' -ForegroundColor Green
    & objdump -p .\osprobe.exe | Select-String -Pattern 'DLL Name' | ForEach-Object { $_.Line }

    Write-Host ''
    Write-Host '下一步（推荐）：' -ForegroundColor Yellow
    Write-Host '  powershell -ExecutionPolicy Bypass -File .\run.ps1   # 跑 4 次并生成 evidence\'
    Write-Host '或者只看一次（不落盘、不改 evidence\）：'
    Write-Host '  .\osprobe.exe'
    Write-Host '或者留一个活进程给另一个窗口看：'
    Write-Host '  Start-Process .\osprobe.exe -ArgumentList ''--hold'',''30000'''
}
finally {
    Pop-Location
}
