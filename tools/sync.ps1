param(
    [string]$Message = "study: update"
)

# Sync the learning journal: stage everything, commit if there is anything, push.
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\tools\sync.ps1
#   powershell -ExecutionPolicy Bypass -File .\tools\sync.ps1 -Message "lab02: calling convention"

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root

try {
    git add -A

    $staged = git diff --cached --name-only
    if (-not $staged) {
        Write-Host 'Nothing to commit, working tree clean.' -ForegroundColor Yellow
        return
    }

    Write-Host 'Staged files:' -ForegroundColor Cyan
    $staged | ForEach-Object { Write-Host "  $_" }

    git commit -m $Message
    if ($LASTEXITCODE -ne 0) { throw 'git commit failed' }

    git push
    if ($LASTEXITCODE -ne 0) { throw 'git push failed' }

    Write-Host 'Pushed.' -ForegroundColor Green
}
finally {
    Pop-Location
}
