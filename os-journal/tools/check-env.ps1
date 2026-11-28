# tools/check-env.ps1
# 打印本机「操作系统学习环境」的真实状态。
# 用法：powershell -ExecutionPolicy Bypass -File .\tools\check-env.ps1
# 设计原则：只探测、不安装、不改系统；每一项都给出 ok / --。
#
# 说明：Windows 注册表里的 ProductName 长期写着「Windows 10」，
# 真实版本要看 CurrentBuildNumber（22000+ 即 Windows 11）。

$ErrorActionPreference = 'SilentlyContinue'

function Write-Ok($name, $value) { Write-Host ("[{0}] {1,-18} {2}" -f 'ok', $name, $value) }
function Write-No($name, $value) { Write-Host ("[{0}] {1,-18} {2}" -f '--', $name, $value) }

# --- OS（用 build 号判断，不用 ProductName） ---
$os = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
if ($os -and $os.CurrentBuildNumber) {
    $build = [int]$os.CurrentBuildNumber
    if ($build -ge 22000) { $name = 'windows 11 x64' } else { $name = 'windows 10 x64' }
    Write-Ok 'windows' ("{0} (build {1})" -f $name, $build)
} else {
    Write-No 'windows' 'registry unreadable'
}

# --- 编译器 ---
$gcc = Get-Command gcc -ErrorAction SilentlyContinue
if ($gcc) {
    $gccVer = (& gcc --version 2>$null | Select-Object -First 1)
    if ($gccVer -match '(\d+\.\d+\.\d+)') { $gccVer = $Matches[1] } else { $gccVer = 'present' }
    Write-Ok 'mingw-w64' ("gcc {0}" -f $gccVer)
} else {
    Write-No 'mingw-w64' 'gcc not on PATH'
}

# --- Python（本机用 py 启动器，python 是 Store stub） ---
if (Get-Command py -ErrorAction SilentlyContinue) {
    $pyVer = (& py --version 2>&1 | Select-Object -First 1)
    Write-Ok 'python' ("{0} (launcher: py)" -f (($pyVer -replace '^Python\s+', '').Trim()))
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    Write-Ok 'python' 'python on PATH (launcher unknown)'
} else {
    Write-No 'python' 'neither py nor python found'
}

# --- WSL：wsl.exe 可能存在但无可用发行版 ---
if (Get-Command wsl -ErrorAction SilentlyContinue) {
    $distros = (& wsl --list --quiet 2>$null)
    if ($LASTEXITCODE -eq 0 -and $distros) {
        Write-Ok 'wsl' ("usable, {0} distro(s)" -f @($distros).Count)
    } else {
        Write-No 'wsl' 'wsl.exe present, no usable distro (access denied)'
    }
} else {
    Write-No 'wsl' 'not installed'
}

# --- ETW / 性能计数器 ---
$wpr = Get-Command wpr -ErrorAction SilentlyContinue
$typeperf = Get-Command typeperf -ErrorAction SilentlyContinue
if ($wpr -and $typeperf) {
    Write-Ok 'etw' 'wpr.exe / typeperf.exe / Get-Counter'
} else {
    Write-No 'etw' 'wpr.exe or typeperf.exe missing'
}

# --- 可选第三方工具（本仓库文档不依赖） ---
foreach ($t in @('procmon', 'procexp', 'windbg')) {
    if (Get-Command $t -ErrorAction SilentlyContinue) { Write-Ok $t 'installed' } else { Write-No $t '' }
}

# --- WPT（WPA / xperf）：装了就能分析 .etl，没装就只能录 ---
$wpa = Get-Command wpa -ErrorAction SilentlyContinue
$xperf = Get-Command xperf -ErrorAction SilentlyContinue
if ($wpa -and $xperf) {
    Write-Ok 'wpa/xperf' ('installed ({0})' -f (Split-Path (Split-Path $xperf.Source -Parent) -Leaf))
} elseif ($wpa -or $xperf) {
    Write-Ok 'wpa/xperf' 'partially installed'
} else {
    Write-No 'wpa/xperf' 'not installed (can record .etl, cannot analyse)'
}

# --- 硬件事实（走 PDH，避免 WMI 权限问题） ---
Write-Host ''
Write-Host ("page size          : {0} bytes" -f [System.Environment]::SystemPageSize)
Write-Host ("logical processors : {0}" -f [System.Environment]::ProcessorCount)
$avail = (Get-Counter '\Memory\Available MBytes' -MaxSamples 1).CounterSamples[0].CookedValue
Write-Host ("available memory   : {0:N0} MB" -f $avail)
