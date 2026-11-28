# 打印当前学习进度。README 里展示的就是这个脚本的真实输出。
# 用法：powershell -ExecutionPolicy Bypass -File tools/status.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$phases = @(
    [pscustomobject]@{ Id = '01-02'; Topic = 'kernel-boundary / syscall'; Deliverable = '01-syscall-cost' }
    [pscustomobject]@{ Id = '03-04'; Topic = 'process-thread / sched';    Deliverable = '02-address-space-probe' }
    [pscustomobject]@{ Id = '05-06'; Topic = 'virt-mem / phys-mem';       Deliverable = '-' }
    [pscustomobject]@{ Id = '07-08'; Topic = 'fs / io';                   Deliverable = '-' }
    [pscustomobject]@{ Id = '09-10'; Topic = 'sync / ipc';                Deliverable = '-' }
    [pscustomobject]@{ Id = '11-12'; Topic = 'observability / lab';       Deliverable = '-' }
)

$docsDir  = Join-Path $root 'docs'
$labsDir  = Join-Path $root 'labs'
$notesDir = Join-Path $root 'notes'

$docCount  = @(Get-ChildItem $docsDir -Filter '*.md' -File -ErrorAction SilentlyContinue).Count
$labDirs   = @(Get-ChildItem $labsDir -Directory -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -match '^\d{2}-' })
$noteFiles = @(Get-ChildItem $notesDir -Filter '*.md' -File -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -notin @('README.md', '_TEMPLATE.md') })

'{0,-7} {1,-35} {2,-22} {3}' -f 'phase', 'topic', 'deliverable', 'state'
foreach ($p in $phases) {
    $state = 'todo'
    if ($p.Deliverable -ne '-' -and
        (Test-Path (Join-Path $labsDir (Join-Path $p.Deliverable 'README.md')))) {
        $state = 'done'
    }
    '{0,-7} {1,-35} {2,-22} {3}' -f $p.Id, $p.Topic, $p.Deliverable, $state
}

'-' * 78
'summary  docs {0}/{1}   labs {2}/{2}   notes {3}' -f $docCount, 11, $labDirs.Count, $noteFiles.Count
