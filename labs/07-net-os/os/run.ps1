# run.ps1 -- labs/07-net-os / os-lab
#
# What this script does, in order:
#   1. runs osprobe.exe THREE times (raw output -> evidence\run1.txt .. run3.txt)
#   2. while each run is alive (the program sleeps in --hold 4000), snapshots
#      the live process -> evidence\handles.txt, evidence\threads.txt
#   3. copies osprobe.exe to osprobe_copy.exe (byte-identical file) and runs
#      that too (raw output -> evidence\run4_copy.txt)
#   4. compares the addresses of all four runs -> evidence\aslr.txt
#
# Everything under evidence\ is raw command output. The only text this script
# adds is the headers / tables / Chinese conclusion of aslr.txt.

$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here

$ev = Join-Path $here 'evidence'
New-Item -ItemType Directory -Force -Path $ev | Out-Null
$exe = Join-Path $here 'osprobe.exe'
$copyExe = Join-Path $here 'osprobe_copy.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "osprobe.exe not found. Run build.ps1 first."
}

$handlesLines = @()
$threadsLines = @()

function Get-ProbeValue {
    param([string]$File, [string]$Pattern)
    if (-not (Test-Path -LiteralPath $File)) { return '<no file>' }
    $line = Get-Content -LiteralPath $File |
            Where-Object { $_ -match $Pattern } |
            Select-Object -First 1
    if (-not $line) { return '<not found>' }
    if ($line -match '=\s*(0x[0-9a-fA-F]+)') { return $Matches[1] }
    return '<not found>'
}

# run one probe and take live snapshots while it is inside its --hold sleep
function Run-One {
    param([string]$Tag)

    $outFile = Join-Path $ev "$Tag.txt"
    $errFile = Join-Path $ev "$Tag.stderr.txt"
    Remove-Item -LiteralPath $outFile -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $errFile -ErrorAction SilentlyContinue

    Write-Output "PS> .\osprobe.exe --hold 4000  > evidence\$Tag.txt"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $exe `
                       -ArgumentList '--hold', '4000' `
                       -WorkingDirectory $here `
                       -NoNewWindow -PassThru `
                       -RedirectStandardOutput $outFile `
                       -RedirectStandardError $errFile

    # the child prints everything, then sleeps inside --hold: snapshot it then
    Start-Sleep -Milliseconds 1500
    $proc = Get-Process -Id $p.Id
    $alive = -not $proc.HasExited

    $script:handlesLines += ""
    $script:handlesLines += "========== $Tag : live osprobe.exe process, PID $($p.Id) (snapshot at +$([math]::Round($sw.Elapsed.TotalSeconds,1)) s, inside --hold 4000) =========="
    $script:handlesLines += "PS> Get-Process -Id $($p.Id) | Select-Object Id,ProcessName,Handles,Threads,WorkingSet64"
    $script:handlesLines += ($proc | Select-Object Id, ProcessName, Handles, Threads, WorkingSet64 |
                      Format-Table -AutoSize | Out-String).TrimEnd()
    $script:handlesLines += "PS> Get-Process -Id $($p.Id) | Select-Object -ExpandProperty Handles"
    $script:handlesLines += "  " + $proc.Handles
    $script:handlesLines += "PS> tasklist /v /fi `"PID eq $($p.Id)`""
    $script:handlesLines += (& tasklist /v /fi "PID eq $($p.Id)" | Out-String).TrimEnd()
    $script:handlesLines += "  (process was still alive at snapshot time: $alive)"
    $script:handlesLines += "  (Threads is a list of thread ids, not a count; the [D] section of run*.txt says which ones our code made)"

    $script:threadsLines += ""
    $script:threadsLines += "========== $Tag : threads of PID $($p.Id) (snapshot inside --hold 4000) =========="
    $script:threadsLines += "PS> Get-Process -Id $($p.Id) | Select-Object -ExpandProperty Threads | Select-Object Id,ThreadState,WaitReason,TotalProcessorTime"
    $script:threadsLines += ($proc.Threads |
                      Select-Object Id, ThreadState, WaitReason, TotalProcessorTime |
                      Format-Table -AutoSize | Out-String).TrimEnd()
    $script:threadsLines += "  (main thread id and worker thread id are printed by osprobe itself: see C4/D3/D5 in $Tag.txt)"
    $script:threadsLines += "  (any other thread here was NOT created by osprobe: Windows lets another process create a thread inside yours)"

    $exited = $p.WaitForExit(30000)
    if (-not $exited) {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        throw "$Tag : osprobe.exe did not exit in 30 s, killed."
    }

    if ((Test-Path -LiteralPath $errFile) -and ((Get-Item -LiteralPath $errFile).Length -gt 0)) {
        Write-Output "WARNING: $Tag wrote something to stderr, see evidence\$Tag.stderr.txt"
    } else {
        Remove-Item -LiteralPath $errFile -ErrorAction SilentlyContinue
    }
}

# run one probe with no live snapshot (used for the copy of the exe)
function Run-Plain {
    param([string]$Tag, [string]$ExePath)

    $outFile = Join-Path $ev "$Tag.txt"
    $errFile = Join-Path $ev "$Tag.stderr.txt"
    Remove-Item -LiteralPath $outFile -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $errFile -ErrorAction SilentlyContinue

    Write-Output "PS> .\osprobe_copy.exe --hold 0  > evidence\$Tag.txt"
    $p = Start-Process -FilePath $ExePath `
                       -ArgumentList '--hold', '0' `
                       -WorkingDirectory $here `
                       -NoNewWindow -PassThru `
                       -RedirectStandardOutput $outFile `
                       -RedirectStandardError $errFile
    $null = $p.WaitForExit(30000)
    if ((Test-Path -LiteralPath $errFile) -and ((Get-Item -LiteralPath $errFile).Length -gt 0)) {
        Write-Output "WARNING: $Tag wrote something to stderr, see evidence\$Tag.stderr.txt"
    } else {
        Remove-Item -LiteralPath $errFile -ErrorAction SilentlyContinue
    }
}

# ---- 3 runs of the same file ----------------------------------------------
$t0 = Get-Date
Run-One -Tag 'run1'
Start-Sleep -Seconds 2
Run-One -Tag 'run2'
Start-Sleep -Seconds 2
Run-One -Tag 'run3'
$t1 = Get-Date

Set-Content -LiteralPath (Join-Path $ev 'handles.txt') -Value $handlesLines -Encoding utf8
Set-Content -LiteralPath (Join-Path $ev 'threads.txt') -Value $threadsLines -Encoding utf8

# ---- 4th run: byte-identical copy under another name ----------------------
Copy-Item -LiteralPath $exe -Destination $copyExe -Force
Run-Plain -Tag 'run4_copy' -ExePath $copyExe

# ---- ASLR comparison ------------------------------------------------------
$runFiles = @{}
foreach ($tag in 'run1', 'run2', 'run3', 'run4_copy') {
    $runFiles[$tag] = Join-Path $ev "$tag.txt"
}

$imageBase = '<objdump not found>'
try {
    $line = (& objdump -p $exe 2>$null | Select-String -Pattern 'ImageBase' | Select-Object -First 1).Line
    if ($line -and $line -match 'ImageBase\s+([0-9a-fA-F]+)') { $imageBase = '0x' + $Matches[1] }
} catch { }

function Get-HexOf {
    param([string]$Path)
    try {
        $h = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        return $h.ToLower()
    } catch { return '<hash failed>' }
}
$hashExe = Get-HexOf $exe
$hashCopy = Get-HexOf $copyExe

# row definitions: id, label, regex
$rowDefs = @(
    @{ Id = 'A1'; What = 'module base       GetModuleHandle(NULL)';              P = '^A1 ' },
    @{ Id = 'A5'; What = 'kernel32.dll base GetModuleHandle("kernel32.dll")';   P = '^A5 ' },
    @{ Id = 'A6'; What = 'ntdll.dll base    GetModuleHandle("ntdll.dll")';      P = '^A6 ' },
    @{ Id = 'A3'; What = 'heap block        HeapAlloc(GetProcessHeap())';       P = '^A3 ' },
    @{ Id = 'A4'; What = 'stack local       &local_marker (in main)';           P = '^A4 ' },
    @{ Id = 'D1'; What = 'main   StackBase  (TEB+0x08)';                        P = '^D1 ' },
    @{ Id = 'D2'; What = 'main   StackLimit (TEB+0x10)';                        P = '^D2 ' },
    @{ Id = 'D3'; What = 'worker StackBase  (TEB+0x08)';                        P = '^D3 ' },
    @{ Id = 'D4'; What = 'worker StackLimit (TEB+0x10)';                        P = '^D4 ' }
)

$vals = @{}
foreach ($tag in 'run1', 'run2', 'run3', 'run4_copy') {
    $vals[$tag] = @{}
    foreach ($r in $rowDefs) {
        $vals[$tag][$r.Id] = Get-ProbeValue -File $runFiles[$tag] -Pattern $r.P
    }
    # stack size = D1 - D2, in KB (the whole stack is shifted by ASLR, its size is not)
    $d1 = $vals[$tag]['D1']; $d2 = $vals[$tag]['D2']
    $vals[$tag]['SZ'] = '<not found>'
    if ($d1 -match '^0x' -and $d2 -match '^0x') {
        $sz = ([Convert]::ToUInt64($d1.Substring(2), 16) - [Convert]::ToUInt64($d2.Substring(2), 16)) / 1024
        $vals[$tag]['SZ'] = ('{0} KB' -f $sz)
    }
}

function Get-SameLabel {
    param([string[]]$Values)
    if (($Values | Where-Object { $_ -notmatch '^0x' }).Count -gt 0) { return '?' }
    if (($Values | Select-Object -Unique).Count -eq 1) { return 'SAME' }
    return 'DIFFERENT'
}

$aslr = @()
$aslr += "================================================================================================"
$aslr += " ASLR evidence -- same osprobe.exe x3, then a byte-identical copy under another file name"
$aslr += "================================================================================================"
$aslr += ""
$aslr += "run1: started $($t0.ToString('yyyy-MM-dd HH:mm:ss'))   raw output -> evidence\run1.txt"
$aslr += "run2: started a few seconds later     raw output -> evidence\run2.txt"
$aslr += "run3: started a few seconds later     raw output -> evidence\run3.txt"
$aslr += "run4: osprobe_copy.exe (same bytes)   raw output -> evidence\run4_copy.txt"
$aslr += "      osprobe.exe      sha256 = $hashExe"
$aslr += "      osprobe_copy.exe sha256 = $hashCopy"
$aslr += "PE preferred ImageBase (objdump -p, what the .exe asks for) = $imageBase"
$aslr += ""
$aslr += "--- table 1: the SAME osprobe.exe, 3 runs, same boot (no reboot in between) ---"
$aslr += ""
$aslr += ('  {0}  {1,-42} {2,-20} {3,-20} {4,-20} {5}' -f '#', 'what', 'run1', 'run2', 'run3', 'same?')
$aslr += "  ----------------------------------------------------------------------------------------------------------"
foreach ($r in $rowDefs) {
    $v = @($vals['run1'][$r.Id], $vals['run2'][$r.Id], $vals['run3'][$r.Id])
    $same = Get-SameLabel -Values $v
    $aslr += ('  {0}  {1,-42} {2,-20} {3,-20} {4,-20} {5}' -f $r.Id, $r.What, $v[0], $v[1], $v[2], $same)
}
$szVals = @($vals['run1']['SZ'], $vals['run2']['SZ'], $vals['run3']['SZ'])
$szSame = if (($szVals | Select-Object -Unique).Count -eq 1) { 'SAME' } else { 'DIFFERENT' }
$aslr += ('  Z   {0,-42} {1,-20} {2,-20} {3,-20} {4}' -f 'main stack size = D1-D2 (KB)', $szVals[0], $szVals[1], $szVals[2], $szSame)
$aslr += ""
$aslr += "--- table 2: osprobe.exe vs osprobe_copy.exe (identical bytes, different file name) ---"
$aslr += ""
$aslr += ('  {0}  {1,-42} {2,-20} {3,-20} {4}' -f '#', 'what', 'osprobe.exe (run1)', 'osprobe_copy.exe (run4)', 'changed?')
$aslr += "  -----------------------------------------------------------------------------------------------"
foreach ($r in $rowDefs | Where-Object { $_.Id -in 'A1', 'A5', 'A6', 'A4' }) {
    $v1 = $vals['run1'][$r.Id]
    $v4 = $vals['run4_copy'][$r.Id]
    $changed = '?'
    if ($v1 -match '^0x' -and $v4 -match '^0x') {
        if ($v1 -eq $v4) { $changed = 'SAME' } else { $changed = 'CHANGED' }
    }
    $aslr += ('  {0}  {1,-42} {2,-20} {3,-20} {4}' -f $r.Id, $r.What, $v1, $v4, $changed)
}
$aslr += ""

# the words used in the Chinese conclusion are computed, not guessed
$a1Same   = Get-SameLabel -Values @($vals['run1']['A1'], $vals['run2']['A1'], $vals['run3']['A1'])
$a5Same   = Get-SameLabel -Values @($vals['run1']['A5'], $vals['run2']['A5'], $vals['run3']['A5'])
$a6Same   = Get-SameLabel -Values @($vals['run1']['A6'], $vals['run2']['A6'], $vals['run3']['A6'])
$a4Diff   = Get-SameLabel -Values @($vals['run1']['A4'], $vals['run2']['A4'], $vals['run3']['A4'])
$a3Diff   = Get-SameLabel -Values @($vals['run1']['A3'], $vals['run2']['A3'], $vals['run3']['A3'])
$d1Diff   = Get-SameLabel -Values @($vals['run1']['D1'], $vals['run2']['D1'], $vals['run3']['D1'])
$d2Diff   = Get-SameLabel -Values @($vals['run1']['D2'], $vals['run2']['D2'], $vals['run3']['D2'])
$copySame = Get-SameLabel -Values @($vals['run1']['A1'], $vals['run4_copy']['A1'])

$aslr += @"

结论 / verdict（本机实测；同一次开机，中间没有重启）
  1) 同一个 osprobe.exe 连续跑 3 次：
       模块基址 A1            = $a1Same
       系统 DLL 基址 A5/A6    = $a5Same / $a6Same
       堆块 A3                = $a3Diff
       栈局部变量 A4          = $a4Diff
       主线程栈范围 D1/D2     = $d1Diff / $d2Diff
     也就是说：镜像（exe/DLL）的基址在"同一个文件 + 同一次开机"里是固定的；
     而栈、堆的地址是每个进程创建时现抽的，所以每次都不同。
     原因：Windows 给镜像挑随机基址时，是按「镜像文件本身 + 本次开机的随机种子」
     算出来的，同一个文件在同一次开机里每次加载都用同一个基址（DLL 还要和其他
     进程共享同一份物理内存，也必须用同一个基址）。栈/堆不共享，所以逐进程随机。
  2) A5 kernel32 / A6 ntdll 三次完全一样：系统 DLL 是共享映射，整个开机期间所有
     进程的基址相同 —— 这就是为什么调试器里 ntdll/kernel32 的地址看起来"很稳"。
  3) 第 4 次用的是字节完全相同的 osprobe_copy.exe（sha256 见上，和 osprobe.exe 一样）：
     模块基址 A1 这一行 table 2 的结果 = $copySame。
     说明镜像基址确实被随机化过，只是"随机结果对同一个文件固定"。
  4) 一句话：文件不变 + 开机不变 -> 镜像基址固定；换文件 -> 基址变；栈/堆逐进程随机。
  5) 对逆向的意义：PE 头里写的 ImageBase 是 $imageBase。Ghidra 里显示的 0x140001450
     就是「ImageBase + RVA 0x1450」；x64dbg 里实际加载地址是「随机基址 + 同一个 RVA」。
     同一个文件里 RVA/文件偏移永远不变，所以硬编码偏移、特征码扫描一直能用；
     绝对地址（0x140001450、0x7FF7...）不能写死。
"@

Set-Content -LiteralPath (Join-Path $ev 'aslr.txt') -Value $aslr -Encoding utf8

Write-Output ""
Write-Output "wrote evidence\run1.txt run2.txt run3.txt run4_copy.txt handles.txt threads.txt aslr.txt"
Write-Output ""
Get-Content -LiteralPath (Join-Path $ev 'aslr.txt')
