# Lab 01 一键构建 + 运行
# 用法（推荐）：
#   powershell -ExecutionPolicy Bypass -File .\build.ps1
# 文件带 UTF-8 BOM，Windows PowerShell 5.1 也能正确读中文。

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot

try {
    Write-Host '=== 编译 -O2 ===' -ForegroundColor Cyan
    gcc -O2 -std=c11 -Wall -Wextra -o syscall_cost.exe syscall_cost.c
    if ($LASTEXITCODE -ne 0) { throw "gcc failed with exit code $LASTEXITCODE" }

    Write-Host ''
    Write-Host '=== 运行 syscall_cost.exe ===' -ForegroundColor Green
    .\syscall_cost.exe
    if ($LASTEXITCODE -ne 0) { throw "run failed with exit code $LASTEXITCODE" }

    Write-Host ''
    Write-Host '构建完成。' -ForegroundColor Green
    Write-Host ''
    Write-Host '自己改一改（改完重跑）：' -ForegroundColor Yellow
    Write-Host '  - 把 memcpy 的 size 改成 128 MiB，看带宽还上不上得去'
    Write-Host '  - 把 [2] 的 n 改成 1000000，看 min 是否还稳定'
    Write-Host '  - 把 [5] 的 pages 改成 65536（256 MiB），看缺页是否还线性'
}
finally {
    Pop-Location
}
