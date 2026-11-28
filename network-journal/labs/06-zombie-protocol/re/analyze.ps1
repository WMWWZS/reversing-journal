# analyze.ps1 - static RE evidence for the ZOMBIE client exe. Three steps:
#   1) import table   -> "networking" is just calling ws2_32.dll functions
#   2) disassembly    -> which IAT slot (send / recv) is referenced from where
#   3) Ghidra headless -> find the caller of the import and decompile it
#
# usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\analyze.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\analyze.ps1 -Exe ..\src\bin\zombie_cli_O2s.exe
#
# Requirements: objdump on PATH (MinGW binutils), plus Ghidra 12.x and a JDK 21+
# for step 3. Paths below are this machine's; override them with the parameters.
# The output that the README quotes is committed under evidence\.
#
# ASCII only on purpose: this box runs Windows PowerShell 5.1, which misreads
# UTF-8 .ps1 files that have no BOM.

param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\src\bin\zombie_cli_O2s.exe'),

    [string]$GhidraDir = 'D:\tools\ghidra\ghidra_12.2_PUBLIC',

    [string]$JdkHome = 'D:\tools\jdk-25\jdk-25.0.5+8',

    [string]$ProjectDir = 'D:\tools\ghidra-projects',

    [string]$ProjectName = 'network-journal-lab06'
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Exe)) {
    throw ("exe not found: " + $Exe + "  (run ..\src\build.ps1 or ..\src\run.ps1 first)")
}
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$outDir = Join-Path $PSScriptRoot 'evidence'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$name = [IO.Path]::GetFileNameWithoutExtension($Exe)
Write-Host "== analyzing $Exe"

# ---------------------------------------------------------------- 1. imports
Write-Host "== 1. import table (ws2_32 related)"
$imports = & objdump -p $Exe
$imports | Set-Content (Join-Path $outDir "$name-objdump-p.txt") -Encoding utf8

$ws2Block = @()
$inWs2 = $false
foreach ($line in $imports) {
    if ($line -match 'DLL Name:\s*WS2_32') { $inWs2 = $true; continue }
    if ($inWs2) {
        if (([string]$line).Trim() -eq '') { break }
        $ws2Block += [string]$line
    }
}
foreach ($line in ($imports | Select-String 'DLL Name')) { Write-Host ('  ' + $line.Line.Trim()) }
foreach ($line in $ws2Block) { Write-Host ('  ' + $line.Trim()) }
Write-Host ("  -> " + @($ws2Block | Where-Object { $_ -match '<none>' }).Count + " functions imported from WS2_32.dll")

# image base + IAT virtual addresses of send / recv (RVA + ImageBase)
$imageBase = 0
foreach ($line in $imports) {
    if ($line -match '^\s*ImageBase\s+([0-9a-fA-F]+)') { $imageBase = [Convert]::ToInt64($matches[1], 16); break }
}
$iatVa = @{}
foreach ($line in $ws2Block) {
    if ($line -match '^\s*([0-9a-fA-F]{8})\s+<none>\s+\S+\s+(send|recv)\s*$') {
        $iatVa[$matches[2]] = $imageBase + [Convert]::ToInt64($matches[1], 16)
    }
}

# --------------------------------------------------------- 2. call sites
Write-Host ""
Write-Host "== 2. disassembly: who references the send/recv IAT slots"
$dis = & objdump -d -M intel --no-show-raw-insn $Exe
foreach ($fn in @('send', 'recv')) {
    if (-not $iatVa.ContainsKey($fn)) { Write-Host ("  " + $fn + ": no IAT slot found"); continue }
    $va = ('0x' + $iatVa[$fn].ToString('x'))
    Write-Host ("  " + $fn + " IAT slot = " + $va)
    $hits = @($dis | Select-String -SimpleMatch ('# ' + $va))
    if ($hits.Count -eq 0) { Write-Host "    (no instruction references it; check the disassembly by hand)" }
    foreach ($h in $hits) { Write-Host ('    ' + $h.Line.Trim()) }
}
Write-Host "  (Windows imports are called indirectly: FF 15 -> call qword ptr [rip+..] / a register."
Write-Host "   Ghidra labels that COMPUTED_CALL, which is why step 3 finds the caller for you.)"

# ------------------------------------------------- 3. Ghidra: decompile callers
Write-Host ""
Write-Host "== 3. Ghidra headless: decompile the callers of the imports"
if (-not (Test-Path -LiteralPath (Join-Path $GhidraDir 'support\analyzeHeadless.bat'))) {
    Write-Host ("  SKIP: analyzeHeadless.bat not found under " + $GhidraDir)
    Write-Host ("        (evidence\$name-ghidra-callers.txt is the run that produced the README)")
    exit 0
}
$env:JAVA_HOME = $JdkHome
$env:PATH = "$JdkHome\bin;$env:PATH"

# analyzeHeadless refuses to import the same file twice into one project, so drop
# the previous run's project files first (Ghidra stores a project as <name>.gpr +
# <name>.rep; it is this lab's own scratch project only).
foreach ($stale in @(
        (Join-Path $ProjectDir ($ProjectName + '.gpr')),
        (Join-Path $ProjectDir ($ProjectName + '.rep')),
        (Join-Path $ProjectDir ($ProjectName + '.lock')),
        (Join-Path $ProjectDir ($ProjectName + '.lock~')))) {
    if (Test-Path -LiteralPath $stale) {
        Write-Host ("  removing the previous Ghidra project file: " + $stale)
        Remove-Item -LiteralPath $stale -Recurse -Force
    }
}

# Capture the whole headless run, then write it as UTF-8 without BOM. (Do NOT use
# Tee-Object -FilePath here: Windows PowerShell 5.1 writes UTF-16LE, which makes
# the evidence file look like binary to git and to every text tool.)
$ghidraLog = & (Join-Path $GhidraDir 'support\analyzeHeadless.bat') $ProjectDir $ProjectName `
    -import $Exe -scriptPath $PSScriptRoot `
    -postScript DecompileCallersOf.java send recv 2>&1
$ghidraLines = @($ghidraLog | ForEach-Object { [string]$_ })
[IO.File]::WriteAllLines((Join-Path $outDir "$name-ghidra-callers.txt"), $ghidraLines,
    (New-Object System.Text.UTF8Encoding($false)))
$ghidraLines | Select-String 'DecompileCallersOf.java>' | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "== evidence written to $outDir"
