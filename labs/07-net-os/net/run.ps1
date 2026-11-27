# run.ps1 - one click: build, then run every ZOMBIE scenario and dump the raw
#           terminal output into evidence\*.txt
#
#   powershell -ExecutionPolicy Bypass -File run.ps1
#
# What it produces in evidence\:
#   build.txt        - gcc output + exe sizes            (written by build.ps1)
#   imports.txt      - ws2_32 imports, objdump -p        (written by build.ps1)
#   run.txt          - server + client logs, real hex frames, hp 100 -> 75
#   tap.txt          - every chunk the tap read: time, direction, size, hex
#   netstat.txt      - netstat -ano / Get-NetTCPConnection while the lab runs
#   trust-client.txt - --trust-client lets a modified claimed_hp decide the hp
#   big-frame.txt    - one 16006-byte SNAPSHOT frame and how recv() splits it
#
# ASCII only on purpose: this file is also read by Windows PowerShell 5.1, which
# decodes a BOM-less .ps1 as ANSI and would garble any non-ASCII character.
# So the Python path below is built from %USERPROFILE% instead of being typed out.
param(
    [int]$SrvPort = 47001,   # server listens here (BRIEF)
    [int]$TapPort = 47000    # tap listens here and forwards to $SrvPort
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$binDir = Join-Path $root 'bin'
$evDir = Join-Path $root 'evidence'
$tmpDir = Join-Path $root '_runtmp'

$python = Join-Path $env:USERPROFILE '.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe'
if (-not (Test-Path -LiteralPath $python)) {
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if (-not $cmd) { throw 'python.exe not found (edit $python in run.ps1)' }
    $python = $cmd.Source
}

New-Item -ItemType Directory -Force -Path $binDir, $evDir, $tmpDir | Out-Null
Remove-Item (Join-Path $tmpDir '*') -Recurse -Force -ErrorAction SilentlyContinue

# ----------------------------------------------------------------- helpers
function Read-Shared([string]$path) {
    # read a file another process is still writing (FileShare.ReadWrite)
    if (-not (Test-Path -LiteralPath $path)) { return '' }
    try {
        $fs = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        $sr = New-Object IO.StreamReader($fs)
        $text = $sr.ReadToEnd()
        $sr.Close()
        $fs.Close()
        return $text
    } catch {
        return ''
    }
}

function Test-PortTaken([int]$port, [string]$what) {
    # 47001/47000 must be free before we start. If not: print who owns it, how to
    # free it, how to move the lab to other ports - then stop. No stack trace.
    $conn = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -eq $conn) { return $false }
    $who = 'unknown'
    $proc = Get-Process -Id $conn.OwningProcess -ErrorAction SilentlyContinue
    if ($proc) { $who = $proc.ProcessName }
    Write-Host ('ERROR: the ' + $what + ' port ' + $port + ' is already LISTENING on this host.')
    Write-Host ('       owner : pid ' + $conn.OwningProcess + ' (' + $who + ')')
    Write-Host ('       fix 1 : close that program -> taskkill /PID ' + $conn.OwningProcess + ' /F')
    Write-Host ('       fix 2 : run the lab on other ports -> powershell -ExecutionPolicy Bypass -File run.ps1 -SrvPort 47101 -TapPort 47100')
    return $true
}

function Clear-LabProcesses {
    # Kill only what this lab started: the two exe names and python running tap.py.
    # Never touch other agents' processes.
    $me = $PID
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        ($_.ProcessId -ne $me) -and (
            $_.Name -like 'zombie_srv*' -or
            $_.Name -like 'zombie_cli*' -or
            ($_.Name -like 'python*' -and $_.CommandLine -like '*tap.py*')
        )
    } | ForEach-Object {
        Write-Host ('    cleanup leftover pid ' + $_.ProcessId + ' ' + $_.Name)
        Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 150
}

function Start-Logged {
    param([string]$Exe, [string[]]$Argv, [string]$Tag)
    $outFile = Join-Path $tmpDir ($Tag + '.out.txt')
    $errFile = Join-Path $tmpDir ($Tag + '.err.txt')
    Remove-Item $outFile, $errFile -Force -ErrorAction SilentlyContinue
    $proc = Start-Process -FilePath $Exe -ArgumentList $Argv -WorkingDirectory $root `
        -RedirectStandardOutput $outFile -RedirectStandardError $errFile `
        -PassThru -WindowStyle Hidden
    [pscustomobject]@{ Tag = $Tag; Proc = $proc; Out = $outFile; Err = $errFile }
}

function Stop-Logged {
    param($H)
    if ($null -eq $H) { return }
    try {
        if (-not $H.Proc.HasExited) {
            Stop-Process -Id $H.Proc.Id -Force -ErrorAction SilentlyContinue
            [void]$H.Proc.WaitForExit(3000)
        }
    } catch { }
}

function Wait-LogLine {
    param([string]$Path, [string]$Pattern, [int]$TimeoutMs = 8000)
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
        $text = Read-Shared $Path
        if ($text -match $Pattern) { return $true }
        Start-Sleep -Milliseconds 40
    }
    return $false
}

function Start-Server {
    param([string]$Exe, [string[]]$Argv, [string]$Tag)
    for ($try = 1; $try -le 3; $try++) {
        $h = Start-Logged -Exe $Exe -Argv $Argv -Tag $Tag
        if (Wait-LogLine -Path $h.Out -Pattern 'ok, waiting for client' -TimeoutMs 6000) { return $h }
        $text = Read-Shared $h.Out
        Stop-Logged $h
        if ($try -lt 3) {
            Start-Sleep -Milliseconds 500
        } else {
            throw ("server did not listen. Its log was:`n" + $text)
        }
    }
}

function Start-Tap {
    param([string[]]$Argv, [string]$Tag)
    for ($try = 1; $try -le 3; $try++) {
        $h = Start-Logged -Exe $python -Argv (@('-u', (Join-Path $root 'tap.py')) + $Argv) -Tag $Tag
        if (Wait-LogLine -Path $h.Out -Pattern 'tap pid=\d+ listen' -TimeoutMs 6000) { return $h }
        $text = Read-Shared $h.Out
        Stop-Logged $h
        if ($try -lt 3) {
            Start-Sleep -Milliseconds 500
        } else {
            throw ("tap did not listen. Its log was:`n" + $text)
        }
    }
}

function Invoke-Scenario {
    param(
        [string]$Tag,
        [string]$ServerExe,
        [string[]]$ServerArgs,
        [string[]]$TapArgs,
        [string]$ClientExe,
        [string[]]$ClientArgs
    )
    $srv = Start-Server -Exe $ServerExe -Argv $ServerArgs -Tag ($Tag + '-srv')
    $tap = $null
    if ($TapArgs -and $TapArgs.Count -gt 0) {
        $tap = Start-Tap -Argv $TapArgs -Tag ($Tag + '-tap')
    }
    $cli = Start-Logged -Exe $ClientExe -Argv $ClientArgs -Tag ($Tag + '-cli')
    [void]$cli.Proc.WaitForExit(90000)
    [void]$srv.Proc.WaitForExit(6000)
    if ($tap) { [void]$tap.Proc.WaitForExit(6000) }
    Start-Sleep -Milliseconds 200
    Stop-Logged $srv
    Stop-Logged $tap
    Stop-Logged $cli
    [pscustomobject]@{ Server = $srv; Tap = $tap; Client = $cli }
}

function New-EvidenceFile {
    param([string]$Path, [string]$Title, [object[]]$Sections)
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine('### ' + $Title)
    [void]$sb.AppendLine('### host: ' + $env:COMPUTERNAME + '   generated: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
    [void]$sb.AppendLine('### Every block below is the raw stdout/stderr of one process, copied unedited.')
    [void]$sb.AppendLine('')
    foreach ($s in $Sections) {
        [void]$sb.AppendLine('===== ' + $s['Name'] + ' =====')
        if ($s.ContainsKey('Text')) {
            $text = [string]$s['Text']
            [void]$sb.Append($text)
            if (-not $text.EndsWith("`n")) { [void]$sb.AppendLine('') }
        } elseif ($s.ContainsKey('Path')) {
            $text = Read-Shared ([string]$s['Path'])
            if ($text.Length -eq 0) {
                [void]$sb.AppendLine('(no output)')
            } else {
                [void]$sb.Append($text)
                if (-not $text.EndsWith("`n")) { [void]$sb.AppendLine('') }
            }
        }
        [void]$sb.AppendLine('')
    }
    [IO.File]::WriteAllText($Path, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
}

function Proc-Sections {
    param($H, [string]$Label)
    $list = New-Object System.Collections.Generic.List[object]
    $list.Add(@{ Name = ($Label + '  stdout'); Path = $H.Out })
    if ((Test-Path -LiteralPath $H.Err) -and ((Get-Item -LiteralPath $H.Err).Length -gt 0)) {
        $list.Add(@{ Name = ($Label + '  stderr'); Path = $H.Err })
    }
    return ,$list
}

$script:stepNo = 0
$script:stepTotal = 11
function Step([string]$msg) {
    $script:stepNo++
    Write-Host ('[' + $script:stepNo + '/' + $script:stepTotal + '] ' + $msg)
}

# ---------------------------------------------------------------- 0. clean up
Write-Host 'run.ps1: cleaning up leftover lab processes from a previous run'
Clear-LabProcesses

$srvO2s = Join-Path $binDir 'zombie_srv_O2s.exe'
$srvO0 = Join-Path $binDir 'zombie_srv_O0.exe'
$cliO2s = Join-Path $binDir 'zombie_cli_O2s.exe'
$cliO0 = Join-Path $binDir 'zombie_cli_O0.exe'

# ---------------------------------------------------------------- 1. build
Step 'build the 4 executables (build.ps1 -> evidence\build.txt + evidence\imports.txt)'
& (Join-Path $root 'build.ps1')
foreach ($exe in @($srvO2s, $srvO0, $cliO2s, $cliO0)) {
    if (-not (Test-Path -LiteralPath $exe)) { throw ('missing ' + $exe) }
}

# Ports per BRIEF: server 47001, tap 47000 -> 47001, client connects 47000, all on
# 127.0.0.1. (4000/4001 were tried first: on this host QQ.exe owns 127.0.0.1:4001 and
# our server's bind() returned WSA error 10013 - see NOTES.md "why 47000/47001".)
$srvPort = $SrvPort
$tapPort = $TapPort
$bindIp = '127.0.0.1'
$portNote = ('note: ports are 47000/47001 (BRIEF), not 4000/4001: on this host QQ.exe owns ' +
             '127.0.0.1:4001, so the server got "bind failed, WSA error 10013" there. See NOTES.md.')

$badPort = $false
if (Test-PortTaken -port $srvPort -what 'server') { $badPort = $true }
if (Test-PortTaken -port $tapPort -what 'tap') { $badPort = $true }
if ($badPort) {
    Write-Host ''
    Write-Host 'nothing was started: free the port(s) above (or pick other ones) and run again.'
    exit 2
}

$srvCommon = @('--port', [string]$srvPort, '--bind', $bindIp)
$tapArgs = @('--listen-port', [string]$tapPort, '--target-port', [string]$srvPort, '--target-host', $bindIp)
$cliViaTap = @('--host', '127.0.0.1', '--port', [string]$tapPort)
$cliDirect = @('--host', $bindIp, '--port', [string]$srvPort)

function Fmt-Args([string]$exe, [string[]]$argv) {
    return ($exe + ' ' + ($argv -join ' '))
}

# ------------------------------------------------- 2. scenario A: 100 -> 75
Step 'scenario A: server 47001 + tap 47000->47001 + client, 5 attacks of 5.0'
$secA = @{ Name = 'commands'; Text = (
    'server: ' + (Fmt-Args 'zombie_srv_O2s.exe' ($srvCommon + @('--authoritative'))) + "`n" +
    'tap   : python -u tap.py ' + ($tapArgs -join ' ') + "`n" +
    'client: ' + (Fmt-Args 'zombie_cli_O2s.exe' ($cliViaTap + @('--attacks', '5', '--dmg', '5'))) + "`n") }
$secA['Text'] = $portNote + "`n" + $secA['Text']
$a = Invoke-Scenario -Tag 'a' -ServerExe $srvO2s -ServerArgs ($srvCommon + @('--authoritative')) `
    -TapArgs $tapArgs -ClientExe $cliO2s -ClientArgs ($cliViaTap + @('--attacks', '5', '--dmg', '5'))

# ------------------------------------- 3. scenario A0: the -O0 -g build too
Step 'scenario A0: -O0 -g build, 2 attacks, client connects straight to 47001 (no tap)'
$a0 = Invoke-Scenario -Tag 'a0' -ServerExe $srvO0 -ServerArgs ($srvCommon + @('--authoritative')) `
    -TapArgs @() -ClientExe $cliO0 -ClientArgs ($cliDirect + @('--attacks', '2', '--dmg', '5'))

# ------------------------------------------- 4. scenario REJECT: dmg out of range
Step 'scenario REJECT: client sends --dmg 30, server must refuse and keep hp=100.00'
$rej = Invoke-Scenario -Tag 'rej' -ServerExe $srvO2s -ServerArgs ($srvCommon + @('--authoritative')) `
    -TapArgs $tapArgs -ClientExe $cliO2s -ClientArgs ($cliViaTap + @('--attacks', '1', '--dmg', '30'))

# ------------------------------------------------ 5. scenario netstat (mid-run)
Step 'scenario netstat: capture netstat/Get-NetTCPConnection while the client is connected'
$nsSrv = Start-Server -Exe $srvO2s -Argv ($srvCommon + @('--authoritative')) -Tag 'ns-srv'
$nsTap = Start-Tap -Argv $tapArgs -Tag 'ns-tap'
# 30 attacks = ~6s of connection: long enough that the netstat capture below cannot
# outlive the processes it is supposed to describe (netstat can be slow on Windows).
$nsAttacks = 30
$nsCli = Start-Logged -Exe $cliO2s -Argv ($cliViaTap + @('--attacks', [string]$nsAttacks, '--dmg', '5')) -Tag 'ns-cli'
$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.ElapsedMilliseconds -lt 5000) {
    $est = @(Get-NetTCPConnection -LocalPort $tapPort -State Established -ErrorAction SilentlyContinue)
    if ($est.Count -gt 0) { break }
    Start-Sleep -Milliseconds 60
}
Start-Sleep -Milliseconds 150
$nsPids = @($nsSrv.Proc.Id, $nsTap.Proc.Id, $nsCli.Proc.Id)
# Process table first, while all three are guaranteed alive (netstat below can be slow).
# ProcessName only: python.exe's full path contains the non-ASCII user name and the
# evidence files are kept pure ASCII so every editor/tool shows them correctly.
$nsProc = (Get-Process -Id $nsPids -ErrorAction SilentlyContinue |
    Select-Object Id, ProcessName, StartTime | Format-Table -AutoSize | Out-String -Width 220)
if (([string]$nsProc).Trim().Length -eq 0) {
    $nsProc = 'Get-Process could not see the spawned PIDs at this instant; the known PIDs line above maps them.'
}
$nsTapTxt = (cmd /c ('netstat -ano | findstr ' + $tapPort)) -join "`n"
$nsSrvTxt = (cmd /c ('netstat -ano | findstr ' + $srvPort)) -join "`n"
$nsConn = (Get-NetTCPConnection -LocalPort $tapPort, $srvPort -ErrorAction SilentlyContinue |
    Sort-Object LocalPort, State |
    Select-Object LocalAddress, LocalPort, RemoteAddress, RemotePort, State, OwningProcess |
    Format-Table -AutoSize | Out-String -Width 220)
[void]$nsCli.Proc.WaitForExit(90000)
[void]$nsSrv.Proc.WaitForExit(6000)
[void]$nsTap.Proc.WaitForExit(6000)
Start-Sleep -Milliseconds 200
Stop-Logged $nsSrv
Stop-Logged $nsTap
Stop-Logged $nsCli

# ------------------------------------------- 6/7/8. --trust-client scenarios
Step 'scenario T1: --trust-client + tap rewrites claimed_hp to 1.00 (server believes it)'
$t1 = Invoke-Scenario -Tag 't1' -ServerExe $srvO2s -ServerArgs ($srvCommon + @('--trust-client')) `
    -TapArgs ($tapArgs + @('--hack-claimed', '1.0')) -ClientExe $cliO2s `
    -ClientArgs ($cliViaTap + @('--attacks', '5', '--dmg', '5'))

Step 'scenario T2: --trust-client + tap rewrites claimed_hp to 100.00 (free full heal)'
$t2 = Invoke-Scenario -Tag 't2' -ServerExe $srvO2s -ServerArgs ($srvCommon + @('--trust-client')) `
    -TapArgs ($tapArgs + @('--hack-claimed', '100.0')) -ClientExe $cliO2s `
    -ClientArgs ($cliViaTap + @('--attacks', '5', '--dmg', '5'))

Step 'scenario T3: same rewrite but server is --authoritative (it ignores claimed_hp)'
$t3 = Invoke-Scenario -Tag 't3' -ServerExe $srvO2s -ServerArgs ($srvCommon + @('--authoritative')) `
    -TapArgs ($tapArgs + @('--hack-claimed', '100.0')) -ClientExe $cliO2s `
    -ClientArgs ($cliViaTap + @('--attacks', '5', '--dmg', '5'))

# --------------------------------------------------- 9/10. big frame scenarios
Step 'scenario D1: SNAPSHOT count=1000 (16006B frame), server sends 4x4096, client recv-buf 8192'
$d1 = Invoke-Scenario -Tag 'd1' -ServerExe $srvO2s `
    -ServerArgs ($srvCommon + @('--authoritative', '--snapshot-count', '1000', '--send-chunk', '4096')) `
    -TapArgs $tapArgs -ClientExe $cliO2s `
    -ClientArgs ($cliViaTap + @('--attacks', '1', '--recv-buf', '8192'))

Step 'scenario D2: SNAPSHOT count=1000 in one send(), client recv-buf 65536 (one recv gets it all)'
$d2 = Invoke-Scenario -Tag 'd2' -ServerExe $srvO2s `
    -ServerArgs ($srvCommon + @('--authoritative', '--snapshot-count', '1000')) `
    -TapArgs $tapArgs -ClientExe $cliO2s `
    -ClientArgs ($cliViaTap + @('--attacks', '1', '--recv-buf', '65536'))

# ------------------------------------------------------------ 11. write evidence
Step 'write evidence\*.txt'

# run.txt
$runSections = New-Object System.Collections.Generic.List[object]
$runSections.Add($secA)
foreach ($s in (Proc-Sections $a.Server 'A. server (zombie_srv_O2s.exe --authoritative)')) { $runSections.Add($s) }
foreach ($s in (Proc-Sections $a.Client 'A. client (zombie_cli_O2s.exe --attacks 5 --dmg 5, via tap)')) { $runSections.Add($s) }
$runSections.Add(@{ Name = 'A0. commands'; Text = (
    'server: ' + (Fmt-Args 'zombie_srv_O0.exe' ($srvCommon + @('--authoritative'))) + "`n" +
    'client: ' + (Fmt-Args 'zombie_cli_O0.exe' ($cliDirect + @('--attacks', '2', '--dmg', '5'))) + "`n") })
foreach ($s in (Proc-Sections $a0.Server 'A0. server (-O0 -g build, 2 attacks)')) { $runSections.Add($s) }
foreach ($s in (Proc-Sections $a0.Client 'A0. client (-O0 -g build, direct to 47001, 2 attacks)')) { $runSections.Add($s) }
$runSections.Add(@{ Name = 'REJECT. command'; Text = 'client: ' + (Fmt-Args 'zombie_cli_O2s.exe' ($cliViaTap + @('--attacks', '1', '--dmg', '30'))) + "`n" })
foreach ($s in (Proc-Sections $rej.Server 'REJECT. server (--authoritative, damage 30 is out of range)')) { $runSections.Add($s) }
foreach ($s in (Proc-Sections $rej.Client 'REJECT. client')) { $runSections.Add($s) }
New-EvidenceFile -Path (Join-Path $evDir 'run.txt') -Title 'run.txt - server + client full logs (real TCP, real hex frames)' -Sections $runSections.ToArray()

# tap.txt
$tapSections = @(
    @{ Name = 'command'; Text = ('python -u tap.py ' + ($tapArgs -join ' ') + "`n" + 'started for scenario A (5 attacks of 5.0)') },
    @{ Name = 'tap stdout (scenario A)'; Path = $a.Tap.Out }
)
if ((Test-Path -LiteralPath $a.Tap.Err) -and ((Get-Item -LiteralPath $a.Tap.Err).Length -gt 0)) {
    $tapSections += @{ Name = 'tap stderr'; Path = $a.Tap.Err }
}
New-EvidenceFile -Path (Join-Path $evDir 'tap.txt') -Title 'tap.txt - every chunk the tap saw: time, direction, length, first 32 bytes hex' -Sections $tapSections

# netstat.txt
$nsSections = @(
    @{ Name = 'what this is'; Text = ("Captured while server(" + $srvPort + ") + tap(" + $tapPort + ") + client were all alive.`n" +
        ("The client was started with --attacks " + $nsAttacks + " so it stays connected while we look.`n") +
        ('known PIDs: server=' + $nsSrv.Proc.Id + '  tap=' + $nsTap.Proc.Id + '  client=' + $nsCli.Proc.Id + "`n")) },
    @{ Name = ('netstat -ano | findstr ' + $tapPort); Text = ($nsTapTxt + "`n") },
    @{ Name = ('netstat -ano | findstr ' + $srvPort); Text = ($nsSrvTxt + "`n") },
    @{ Name = ('Get-NetTCPConnection -LocalPort ' + $tapPort + ',' + $srvPort + ' (LocalAddress,LocalPort,RemoteAddress,RemotePort,State,OwningProcess)'); Text = ($nsConn + "`n") },
    @{ Name = 'Get-Process -Id server,tap,client (Id,ProcessName,StartTime)'; Text = ($nsProc + "`n") },
    @{ Name = 'client stdout captured during the same run'; Path = $nsCli.Out },
    @{ Name = 'server stdout captured during the same run'; Path = $nsSrv.Out }
)
New-EvidenceFile -Path (Join-Path $evDir 'netstat.txt') -Title 'netstat.txt - LISTENING / ESTABLISHED + PIDs while the lab is running' -Sections $nsSections

# trust-client.txt
$trustSections = New-Object System.Collections.Generic.List[object]
$trustSections.Add(@{ Name = 'what this demonstrates'; Text = (
    'The client always sends ATTACK damage=5.00 and its own claimed_hp (95,90,85,80,75).' + "`n" +
    'tap.py --hack-claimed X rewrites those 4 payload bytes on the wire (frame length unchanged).' + "`n" +
    'With --trust-client the server stores that number as the real hp: the client decides its own hp.' + "`n" +
    'With --authoritative the same rewritten packet changes nothing: the server recomputes hp itself.' + "`n") })
$trustSections.Add(@{ Name = 'T1 command'; Text = (
    'server: ' + (Fmt-Args 'zombie_srv_O2s.exe' ($srvCommon + @('--trust-client'))) + "`n" +
    'tap   : python -u tap.py ' + (($tapArgs + @('--hack-claimed', '1.0')) -join ' ') + "`n" +
    'client: ' + (Fmt-Args 'zombie_cli_O2s.exe' ($cliViaTap + @('--attacks', '5', '--dmg', '5'))) + "`n") })
foreach ($s in (Proc-Sections $t1.Server 'T1. server (--trust-client, claimed_hp forced to 1.00)')) { $trustSections.Add($s) }
foreach ($s in (Proc-Sections $t1.Client 'T1. client')) { $trustSections.Add($s) }
foreach ($s in (Proc-Sections $t1.Tap 'T1. tap (the rewrite happens here)')) { $trustSections.Add($s) }
$trustSections.Add(@{ Name = 'T2 command'; Text = (
    'server: ' + (Fmt-Args 'zombie_srv_O2s.exe' ($srvCommon + @('--trust-client'))) + "`n" +
    'tap   : python -u tap.py ' + (($tapArgs + @('--hack-claimed', '100.0')) -join ' ') + "`n" +
    'client: ' + (Fmt-Args 'zombie_cli_O2s.exe' ($cliViaTap + @('--attacks', '5', '--dmg', '5'))) + "`n") })
foreach ($s in (Proc-Sections $t2.Server 'T2. server (--trust-client, claimed_hp forced to 100.00 = free heal)')) { $trustSections.Add($s) }
foreach ($s in (Proc-Sections $t2.Client 'T2. client')) { $trustSections.Add($s) }
$trustSections.Add(@{ Name = 'T3 command'; Text = (
    'server: ' + (Fmt-Args 'zombie_srv_O2s.exe' ($srvCommon + @('--authoritative'))) + "`n" +
    'tap   : python -u tap.py ' + (($tapArgs + @('--hack-claimed', '100.0')) -join ' ') + "`n" +
    'client: ' + (Fmt-Args 'zombie_cli_O2s.exe' ($cliViaTap + @('--attacks', '5', '--dmg', '5'))) + "`n") })
foreach ($s in (Proc-Sections $t3.Server 'T3. server (--authoritative, same rewritten packet, ignored)')) { $trustSections.Add($s) }
foreach ($s in (Proc-Sections $t3.Client 'T3. client')) { $trustSections.Add($s) }
New-EvidenceFile -Path (Join-Path $evDir 'trust-client.txt') -Title 'trust-client.txt - who decides your hp: the client or the server?' -Sections $trustSections.ToArray()

# big-frame.txt
$bigSections = New-Object System.Collections.Generic.List[object]
$bigSections.Add(@{ Name = 'what to look at'; Text = (
    'SNAPSHOT count=1000 is ONE frame of 16006 bytes (6-byte header + 16002-byte payload).' + "`n" +
    'D1: the server writes it with 4 send() calls (4096,4096,4096,3718). The client recv buffer' + "`n" +
    '    is 8192, so recv() has to be called several times to assemble that one frame.' + "`n" +
    'D2: the server writes it with ONE send(); the client recv buffer is 65536, so one recv()' + "`n" +
    '    returns all 16006 bytes at once.' + "`n" +
    'Both are legal: TCP gives you a byte stream, not messages. That is why the parser keeps a' + "`n" +
    'buffer and only cuts a frame once total_len bytes have arrived.' + "`n") })
$bigSections.Add(@{ Name = 'D1 command'; Text = (
    'server: ' + (Fmt-Args 'zombie_srv_O2s.exe' ($srvCommon + @('--authoritative', '--snapshot-count', '1000', '--send-chunk', '4096'))) + "`n" +
    'tap   : python -u tap.py ' + ($tapArgs -join ' ') + "`n" +
    'client: ' + (Fmt-Args 'zombie_cli_O2s.exe' ($cliViaTap + @('--attacks', '1', '--recv-buf', '8192'))) + "`n") })
foreach ($s in (Proc-Sections $d1.Server 'D1. server (4 send() calls)')) { $bigSections.Add($s) }
foreach ($s in (Proc-Sections $d1.Client 'D1. client (recv-buf 8192: how many recv() does the frame take?)')) { $bigSections.Add($s) }
foreach ($s in (Proc-Sections $d1.Tap 'D1. tap (how many chunks did it see?)')) { $bigSections.Add($s) }
$bigSections.Add(@{ Name = 'D2 command'; Text = (
    'server: ' + (Fmt-Args 'zombie_srv_O2s.exe' ($srvCommon + @('--authoritative', '--snapshot-count', '1000'))) + "`n" +
    'tap   : python -u tap.py ' + ($tapArgs -join ' ') + "`n" +
    'client: ' + (Fmt-Args 'zombie_cli_O2s.exe' ($cliViaTap + @('--attacks', '1', '--recv-buf', '65536'))) + "`n") })
foreach ($s in (Proc-Sections $d2.Server 'D2. server (one send() call)')) { $bigSections.Add($s) }
foreach ($s in (Proc-Sections $d2.Client 'D2. client (recv-buf 65536: one recv() gets 16006 bytes)')) { $bigSections.Add($s) }
foreach ($s in (Proc-Sections $d2.Tap 'D2. tap')) { $bigSections.Add($s) }
New-EvidenceFile -Path (Join-Path $evDir 'big-frame.txt') -Title 'big-frame.txt - one 16006-byte frame, and how recv() splits it' -Sections $bigSections.ToArray()

# --------------------------------------------------------------- verdict
$runTxt = Read-Shared (Join-Path $evDir 'run.txt')
$tapTxt = Read-Shared (Join-Path $evDir 'tap.txt')
$netTxt = Read-Shared (Join-Path $evDir 'netstat.txt')
$trustTxt = Read-Shared (Join-Path $evDir 'trust-client.txt')
$bigTxt = Read-Shared (Join-Path $evDir 'big-frame.txt')

$checks = New-Object System.Collections.Generic.List[string]
function Check([bool]$ok, [string]$what) {
    if ($ok) {
        Write-Host ('  PASS  ' + $what)
    } else {
        Write-Host ('  FAIL  ' + $what)
        $script:failed = $true
    }
}
$script:failed = $false

Check ($runTxt.Contains('FINAL hp=75.00')) 'run.txt: server ended at hp=75.00'
Check ($runTxt.Contains('match=YES')) 'run.txt: client predicted hp == server hp (match=YES)'
Check ($runTxt.Contains('HP_SYNC hp=75.00 max=100.00 tick=5')) 'run.txt: 5th HP_SYNC carries hp=75.00'
Check ($runTxt.Contains('REJECT seq=1 dmg=30.00 out of range')) 'run.txt: out-of-range damage was rejected'
Check ($tapTxt.Contains('HP_SYNC player=1 hp=75.00')) 'tap.txt: tap decoded the 24-byte HP_SYNC frame'
Check ($tapTxt.Contains('ATTACK dmg=5.00 seq=1 claimed=95.00')) 'tap.txt: tap decoded the 16-byte ATTACK frame'
Check ($netTxt.Contains('LISTENING')) 'netstat.txt: LISTENING captured'
Check ($netTxt.Contains('ESTABLISHED')) 'netstat.txt: ESTABLISHED captured'
Check ($trustTxt.Contains('trust-client: took claimed_hp=1.00 as truth')) 'trust-client.txt: server believed claimed_hp=1.00'
Check ($trustTxt.Contains('trust-client: took claimed_hp=100.00 as truth')) 'trust-client.txt: server accepted a full heal'
Check ($bigTxt -match 'recv_chunks=[2-9]') 'big-frame.txt: D1 frame needed 2+ recv() calls'
Check ($bigTxt.Contains('recv_chunks=1')) 'big-frame.txt: D2 frame arrived in 1 recv() call'
Check ($bigTxt.Contains('SNAPSHOT count=1000 payload=16002')) 'big-frame.txt: 16006-byte SNAPSHOT seen'

Remove-Item $tmpDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ''
Write-Host 'evidence written:'
foreach ($f in (Get-ChildItem (Join-Path $evDir '*.txt') | Sort-Object Name)) {
    Write-Host ('  ' + $f.Name.PadRight(20) + ('{0,8} bytes' -f $f.Length))
}
Write-Host ''
if ($script:failed) {
    Write-Host 'VERDICT: FAIL'
    exit 1
}
Write-Host 'VERDICT: PASS'
exit 0
