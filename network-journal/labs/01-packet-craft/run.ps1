# Lab 01 一键运行
# 用法（任选其一）：
#   powershell -ExecutionPolicy Bypass -File .\run.ps1
#   pwsh -File .\run.ps1
#
# 解释器说明：本机 `python` 可能是 Microsoft Store 的占位 stub，
# 运行为 EXIT=9009 且没有任何输出。因此优先用 `py` 启动器。
# 文件已带 UTF-8 BOM，Windows PowerShell 5.1 也能正确读中文。

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot

try {
    $launcher = $null
    if (Get-Command py -ErrorAction SilentlyContinue) {
        $launcher = 'py'
    }
    elseif (Get-Command python -ErrorAction SilentlyContinue) {
        $launcher = 'python'
    }
    else {
        throw '找不到 Python 3。请安装 Python 3 并确保 py 或 python 在 PATH 中。'
    }

    # 让 Python 输出 UTF-8，避免中文在控制台乱码
    $env:PYTHONIOENCODING = 'utf-8'
    try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

    Write-Host "=== 解释器：$launcher ===" -ForegroundColor Cyan
    & $launcher craft.py
    if ($LASTEXITCODE -ne 0) {
        throw "craft.py 退出码 $LASTEXITCODE（自测失败）"
    }
    Write-Host ''
    Write-Host '运行完成：所有 assert 自测通过。' -ForegroundColor Green
}
finally {
    Pop-Location
}
