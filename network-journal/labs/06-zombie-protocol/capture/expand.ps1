<#
.SYNOPSIS
    把任意 pcapng 展开成三个文本：dissect.txt / hex.txt / streams.txt。
    不需要管理员权限（只是读文件 + 跑 tshark）。

.DESCRIPTION
    dissect.txt : tshark -V 的完整解析，加载 zombie.lua，所以每条 ZOMBIE 帧
                  都会展开成 zombie.* 字段。
    hex.txt     : 前 N 帧（默认 3 帧）的十六进制 + ASCII。
    streams.txt : 按包一行，列出 frame.number / ip / 端口 / tcp.seq / tcp.len /
                  tcp.flags / tcp.time_delta / zombie.opcode，用来看 TCP 怎么分段。

    默认输出到 pcap 同目录下的 expanded\ 文件夹。

.PARAMETER Pcap
    要展开的 pcapng / pcap 文件（必填）。

.PARAMETER OutDir
    输出目录，默认是 <pcap所在目录>\expanded。

.PARAMETER WiresharkDir
    Wireshark 安装目录，默认 E:\Wireshark。

.PARAMETER LuaScript
    ZOMBIE 解析器路径，默认是本脚本同目录的 zombie.lua。

.PARAMETER HexFrames
    hex.txt 里放前几帧，默认 3。

.EXAMPLE
    .\expand.ps1 -Pcap .\demo.pcapng

.EXAMPLE
    # 展开一次真抓包（capture.ps1 写出来的 evidence\zombie.pcapng）
    .\expand.ps1 -Pcap .\evidence\zombie.pcapng -OutDir .\evidence\live
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Pcap,

    [string]$OutDir = '',

    [string]$WiresharkDir = 'E:\Wireshark',

    [string]$LuaScript = '',

    [int]$HexFrames = 3
)

$ErrorActionPreference = 'Stop'

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

# ---- 1. 检查输入 ----------------------------------------------------------
if (-not (Test-Path -LiteralPath $Pcap)) {
    throw "pcap 文件不存在：$Pcap"
}
$pcapPath = (Resolve-Path -LiteralPath $Pcap).Path
if ($pcapPath -notmatch '\.(pcapng|pcap)$') {
    Write-Host "警告：$pcapPath 后缀不是 .pcapng/.pcap，仍然试着当 pcap 解析。" -ForegroundColor Yellow
}

# ---- 2. 准备输出目录 ------------------------------------------------------
if ($OutDir -eq '') {
    $OutDir = Join-Path ([System.IO.Path]::GetDirectoryName($pcapPath)) 'expanded'
}
if (-not (Test-Path -LiteralPath $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path

# ---- 3. 工具和 lua 解析器 -------------------------------------------------
$tshark = Resolve-Tool $WiresharkDir 'tshark.exe'

if ($LuaScript -eq '') {
    $LuaScript = Join-Path $PSScriptRoot 'zombie.lua'
}
$luaArgs = @()
if (Test-Path -LiteralPath $LuaScript) {
    $luaArgs = @('-X', "lua_script:$LuaScript")
    Write-Host "ZOMBIE 解析器：$LuaScript"
} else {
    Write-Host "警告：没找到 $LuaScript，dissect.txt 里不会有 zombie.* 字段。" -ForegroundColor Yellow
}

$dissectPath = Join-Path $OutDir 'dissect.txt'
$hexPath     = Join-Path $OutDir 'hex.txt'
$streamsPath = Join-Path $OutDir 'streams.txt'

Write-Host "输入 pcap ：$pcapPath"
Write-Host "输出目录 ：$OutDir"
Write-Host ''

# ---- 4. dissect.txt：-V 完整解析 -----------------------------------------
Write-Host "[1/3] tshark -V -> dissect.txt （会比较慢、文件比较大，正常）"
& $tshark -r $pcapPath @luaArgs -V 2>&1 | Set-Content -LiteralPath $dissectPath -Encoding UTF8
if ($LASTEXITCODE -ne 0) {
    throw "tshark -V 失败（exit $LASTEXITCODE），见上面的报错。"
}

# ---- 5. hex.txt：前 N 帧的 hex + ASCII -----------------------------------
Write-Host "[2/3] tshark -x -> hex.txt （前 $HexFrames 帧）"
& $tshark -r $pcapPath @luaArgs -x -c $HexFrames 2>&1 | Set-Content -LiteralPath $hexPath -Encoding UTF8
if ($LASTEXITCODE -ne 0) {
    throw "tshark -x 失败（exit $LASTEXITCODE）。"
}

# ---- 6. streams.txt：每包一行，看 TCP 分段 --------------------------------
Write-Host '[3/3] tshark -T fields -> streams.txt （TCP seq/len/flags）'
$fields = @(
    'frame.number', 'ip.src', 'tcp.srcport', 'tcp.dstport',
    'tcp.seq', 'tcp.len', 'tcp.flags', 'tcp.time_delta', 'zombie.opcode'
)
$fieldArgs = @()
foreach ($f in $fields) { $fieldArgs += @('-e', $f) }
& $tshark -r $pcapPath @luaArgs -T fields @fieldArgs -E header=y -E separator=',' -E quote=d 2>&1 |
    Set-Content -LiteralPath $streamsPath -Encoding UTF8
if ($LASTEXITCODE -ne 0) {
    throw "tshark -T fields 失败（exit $LASTEXITCODE）。"
}

# ---- 7. 汇总 --------------------------------------------------------------
function Get-LineCount {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return 0 }
    return @(Get-Content -LiteralPath $Path).Count
}

Write-Host ''
Write-Host '完成。'
Write-Host ("  {0}  ({1} 行)" -f $dissectPath, (Get-LineCount $dissectPath))
Write-Host ("  {0}  ({1} 行)" -f $hexPath, (Get-LineCount $hexPath))
Write-Host ("  {0}  ({1} 行)" -f $streamsPath, (Get-LineCount $streamsPath))
Write-Host ''
Write-Host '想只看 ZOMBIE 帧，可以跑：'
Write-Host ("  & '{0}' -r '{1}' -X lua_script:'{2}' -Y zombie" -f $tshark, $pcapPath, $LuaScript)
Write-Host '或者在 Wireshark GUI 里打开这个 pcapng，过滤栏输入：zombie'
