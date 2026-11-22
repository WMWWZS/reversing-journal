# Static RE evidence for the ZOMBIE lab exe (Lead use only). ASCII-only on purpose:
# this box runs Windows PowerShell 5.1, which misreads UTF-8 .ps1 without BOM.
#   1) import table  -> "networking" is just calling ws2_32.dll functions
#   2) disassembly   -> where send/recv are called
#   3) Ghidra headless -> find the caller of the import and decompile it
#
# usage: powershell -NoProfile -ExecutionPolicy Bypass -File .\analyze.ps1 [-Exe path]

param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\net\bin\zombie_cli_O2s.exe')
)

$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$outDir = Join-Path $PSScriptRoot 'evidence'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$name = [IO.Path]::GetFileNameWithoutExtension($Exe)
Write-Host "== analyzing $Exe"

Write-Host "== 1. import table (ws2_32 related)"
$imports = & objdump -p $Exe
$imports | Set-Content (Join-Path $outDir "$name-objdump-p.txt") -Encoding utf8
$imports | Select-String 'DLL Name|WSAStartup|socket|connect|send|recv|closesocket|htons|inet_' | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "== 2. disassembly: call sites of send / recv"
$dis = & objdump -d -M intel --no-show-raw-insn $Exe
$dis | Set-Content (Join-Path $outDir "$name-objdump-d.txt") -Encoding utf8
$dis | Select-String 'call.*<(send|recv|WSASend|WSARecv)>' | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "== 3. Ghidra headless: decompile the callers of the imports"
$env:JAVA_HOME = 'D:\tools\jdk-25\jdk-25.0.5+8'
$env:PATH = "$env:JAVA_HOME\bin;$env:PATH"
$gh = 'D:\tools\ghidra\ghidra_12.2_PUBLIC'

& "$gh\support\analyzeHeadless.bat" 'D:\tools\ghidra-projects' 'netlab' `
    -import $Exe -scriptPath $PSScriptRoot `
    -postScript DecompileCallersOf.java send recv 2>&1 |
    Tee-Object -FilePath (Join-Path $outDir "$name-ghidra-callers.txt") |
    Select-String 'DecompileCallersOf.java>' | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "== evidence written to $outDir"
