<#
.SYNOPSIS
    ZOMBIE 抓包脚本：抓 127.0.0.1:47000/47001 的 loopback 流量，抓完自动展开。
    第一次使用请用【管理员】PowerShell 运行。

.DESCRIPTION
    流程：
      1. 检查是不是管理员，不是就直接退出（dumpcap 抓包必须要管理员）。
      2. dumpcap 抓 \Device\NPF_Loopback，过滤器 "tcp port 47000 or tcp port 47001"，
         写 evidence\zombie.pcapng。
      3. 提示你到另一个窗口跑 net\run.ps1（server + tap + client 三方）。
      4. 回到本窗口按回车，停掉 dumpcap。
      5. 自动调 expand.ps1 生成 evidence\dissect.txt / hex.txt / streams.txt。

.PARAMETER WiresharkDir
    Wireshark 安装目录，默认 E:\Wireshark。

.PARAMETER Interface
    抓包网卡设备名，默认 \Device\NPF_Loopback（本机回环网卡，Npcap 提供）。
    用 dumpcap -D 可以看到全部网卡设备名。

.PARAMETER Filter
    dumpcap 的 BPF 过滤器，默认 "tcp port 47000 or tcp port 47001"。

.PARAMETER OutDir
    输出目录，默认是本脚本同目录的 evidence\。

.PARAMETER CaptureFile
    抓包文件名，默认 evidence\zombie.pcapng。

.PARAMETER MaxSeconds
    保险上限：dumpcap 最多抓这么多秒（默认 300 秒），防止你忘了按回车。

.PARAMETER ExistingPcap
    跳过抓包，直接对已有 pcapng 生成三个文本（这个不需要管理员）。
    例：.\capture.ps1 -ExistingPcap .\demo.pcapng

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\capture.ps1

.EXAMPLE
    # 非管理员也能跑：用已有的 demo.pcapng 重新生成解析文本
    powershell -ExecutionPolicy Bypass -File .\capture.ps1 -ExistingPcap .\demo.pcapng
#>
[CmdletBinding()]
param(
    [string]$WiresharkDir = 'E:\Wireshark',

    [string]$Interface = '\Device\NPF_Loopback',

    [string]$Filter = 'tcp port 47000 or tcp port 47001',

    [string]$OutDir = '',

    [string]$CaptureFile = '',

    [int]$MaxSeconds = 300,

    [string]$ExistingPcap = ''
)

$ErrorActionPreference = 'Stop'
$scriptDir = $PSScriptRoot
$expandScript = Join-Path $scriptDir 'expand.ps1'
$luaScript = Join-Path $scriptDir 'zombie.lua'

function Resolve-Tool {
    param([string]$Dir, [string]$Name)
    $candidate = Join-Path $Dir $Name
    if (Test-Path -LiteralPath $candidate) {
        return (Resolve-Path -LiteralPath $candidate).Path
    }
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "找不到 $Name ：$candidate 不存在，PATH 里也没有。可以用 -WiresharkDir 指定 Wireshark 目录。"
}

function Test-IsAdmin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# ---- 0. 输出目录和抓包文件名 ---------------------------------------------
if ($OutDir -eq '') { $OutDir = Join-Path $scriptDir 'evidence' }
if (-not (Test-Path -LiteralPath $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path

if ($CaptureFile -eq '') {
    $CaptureFile = Join-Path $OutDir 'zombie.pcapng'
} elseif (-not [System.IO.Path]::IsPathRooted($CaptureFile)) {
    $CaptureFile = Join-Path $OutDir $CaptureFile
}

# ==========================================================================
# A. 直接展开已有 pcapng（不需要管理员）
# ==========================================================================
if ($ExistingPcap -ne '') {
    if (-not (Test-Path -LiteralPath $ExistingPcap)) {
        throw "找不到 -ExistingPcap 指定的文件：$ExistingPcap"
    }
    $ExistingPcap = (Resolve-Path -LiteralPath $ExistingPcap).Path
    Write-Host '== 跳过抓包，直接展开已有 pcapng =='
    Write-Host "   pcap : $ExistingPcap"
    if (-not (Test-Path -LiteralPath $expandScript)) {
        throw "找不到 expand.ps1：$expandScript"
    }
    & $expandScript -Pcap $ExistingPcap -OutDir $OutDir -WiresharkDir $WiresharkDir
    exit $LASTEXITCODE
}

# ==========================================================================
# B. 真抓包（必须是管理员）
# ==========================================================================
Write-Host '== 检查管理员权限 =='
if (-not (Test-IsAdmin)) {
    Write-Host ''
    Write-Host '这个脚本要在【管理员】PowerShell 里跑，因为 dumpcap 抓网卡需要管理员权限。' -ForegroundColor Red
    Write-Host '当前不是管理员。请这样做：'
    Write-Host '  1) 右键“开始”按钮 -> 终端(管理员)；或者 Win+X -> Windows PowerShell (管理员)'
    Write-Host '  2) cd E:\harness\reversing-journal\labs\07-net-os\capture'
    Write-Host '  3) powershell -ExecutionPolicy Bypass -File .\capture.ps1'
    Write-Host ''
    Write-Host '如果你只是想用已有的 pcapng 生成解析文本，那个不需要管理员：' -ForegroundColor Yellow
    Write-Host '  powershell -ExecutionPolicy Bypass -File .\capture.ps1 -ExistingPcap .\demo.pcapng' -ForegroundColor Yellow
    exit 1
}
Write-Host '   是管理员，继续。'

$dumpcap  = Resolve-Tool $WiresharkDir 'dumpcap.exe'
$capinfos = Resolve-Tool $WiresharkDir 'capinfos.exe'

if (Test-Path -LiteralPath $CaptureFile) {
    Remove-Item -LiteralPath $CaptureFile -Force
}

Write-Host ''
Write-Host '== 启动 dumpcap =='
Write-Host ("   网卡      : {0}" -f $Interface)
Write-Host ("   过滤器    : {0}" -f $Filter)
Write-Host ("   写入      : {0}" -f $CaptureFile)
Write-Host ("   最长秒数  : {0}（到点会自动停）" -f $MaxSeconds)
Write-Host ''
Write-Host '   等价的手工命令（脚本出错时你可以自己敲）：'
Write-Host ('   "{0}" -i "{1}" -f "{2}" -w "{3}"' -f $dumpcap, $Interface, $Filter, $CaptureFile)
Write-Host ''

$dumpcapArgs = @(
    '-i', $Interface,
    '-f', $Filter,
    '-w', $CaptureFile,
    '-a', "duration:$MaxSeconds"
)

try {
    $dumpcapProc = Start-Process -FilePath $dumpcap -ArgumentList $dumpcapArgs -PassThru -NoNewWindow
} catch {
    throw "dumpcap 启动失败：$($_.Exception.Message)"
}

Start-Sleep -Milliseconds 1200

if ($dumpcapProc.HasExited) {
    Write-Host ''
    Write-Host 'dumpcap 启动后马上退出了。最常见的原因：' -ForegroundColor Red
    Write-Host '  1) 不是管理员（就算窗口标题写着管理员，也可能是另一个普通窗口）' -ForegroundColor Red
    Write-Host '  2) Npcap 没装 / 网卡名不对（先跑 dumpcap -D 看 \Device\NPF_Loopback 在不在）' -ForegroundColor Red
    Write-Host ("   dumpcap 退出码：{0}" -f $dumpcapProc.ExitCode)
    exit 1
}

Write-Host '   抓包已经在跑了。'
Write-Host ''
Write-Host '== 现在去另一个窗口，把靶场跑起来 =='
Write-Host '   1) 新开一个 PowerShell（普通权限就够）'
Write-Host '   2) cd E:\harness\reversing-journal\labs\07-net-os\net'
Write-Host '   3) powershell -ExecutionPolicy Bypass -File .\run.ps1'
Write-Host '      （它会拉起 server(47001) + tap(47000) + client，客户端打 5 次）'
Write-Host ''
Write-Host '等客户端跑完、窗口回到提示符后，再回到本窗口按回车停止抓包。'
[void](Read-Host '按回车停止抓包')

Write-Host ''
Write-Host '== 停止 dumpcap =='
if (-not $dumpcapProc.HasExited) {
    Stop-Process -Id $dumpcapProc.Id -Force -ErrorAction SilentlyContinue
    [void]$dumpcapProc.WaitForExit(5000)
}
Start-Sleep -Milliseconds 500

if (-not (Test-Path -LiteralPath $CaptureFile)) {
    throw "没有生成抓包文件：$CaptureFile。上面应该能看到 dumpcap 的报错。"
}
$size = (Get-Item -LiteralPath $CaptureFile).Length
Write-Host ("   文件：{0}（{1} 字节）" -f $CaptureFile, $size)
if ($size -eq 0) {
    throw '抓到的文件是 0 字节：dumpcap 没抓到任何东西，或者被强杀得太早。请重试。'
}

Write-Host ''
Write-Host '== 用 capinfos 看一眼抓到了什么 =='
& $capinfos -c -M $CaptureFile
if ($LASTEXITCODE -ne 0) {
    Write-Host 'capinfos 报错了，但文件可能仍可读；下一步 tshark 会告诉我们。' -ForegroundColor Yellow
}

# ---- 展开 ----------------------------------------------------------------
Write-Host ''
Write-Host '== 展开成 dissect.txt / hex.txt / streams.txt =='
if (-not (Test-Path -LiteralPath $expandScript)) {
    throw "找不到 expand.ps1：$expandScript"
}
& $expandScript -Pcap $CaptureFile -OutDir $OutDir -WiresharkDir $WiresharkDir

Write-Host ''
Write-Host '== 抓完了。接下来看什么 =='
Write-Host ("   {0}\dissect.txt  —— 搜 'ZOMBIE HELLO' / 'ZOMBIE ATTACK' / 'ZOMBIE HP_SYNC'" -f $OutDir)
Write-Host ("   {0}\hex.txt      —— 前 3 帧的原始字节（TCP 握手）" -f $OutDir)
Write-Host ("   {0}\streams.txt  —— 每个包的 seq/len/flags，看 TCP 怎么分段" -f $OutDir)
Write-Host ''
Write-Host '  只列 ZOMBIE 帧（命令行版）：'
Write-Host ('   & "{0}" -r "{1}" -X lua_script:"{2}" -Y zombie' -f (Resolve-Tool $WiresharkDir 'tshark.exe'), $CaptureFile, $luaScript)
Write-Host '   在 Wireshark GUI 里打开同一个 pcapng，过滤栏输入：zombie'
exit 0
