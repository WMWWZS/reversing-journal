# build.ps1 -- labs/07-net-os / os-lab
#
# One command builds osprobe.exe with MinGW-w64 gcc:
#     gcc -O0 -g -Wall -Wextra -o osprobe.exe osprobe.c
#
# -O0 -g  : no optimisation + debug info, so the disassembly still looks like
#           the C source (that is what Ghidra / x64dbg will show).
# Only the Win32 API is used (KERNEL32.dll) plus the C runtime that gcc
# always links; nothing else. No sockets, no extra -l options.

$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here

$ev = Join-Path $here 'evidence'
New-Item -ItemType Directory -Force -Path $ev | Out-Null

Write-Output "PS> gcc --version"
Write-Output (& gcc --version | Select-Object -First 1)
Write-Output ""
Write-Output "PS> gcc -O0 -g -Wall -Wextra -o osprobe.exe osprobe.c"
& gcc -O0 -g -Wall -Wextra -o osprobe.exe osprobe.c
if ($LASTEXITCODE -ne 0) {
    Write-Output "BUILD FAILED, gcc exit code = $LASTEXITCODE"
    exit $LASTEXITCODE
}
Write-Output "gcc exit code = 0"
Write-Output ""

Write-Output "PS> Get-Item .\osprobe.exe | Select-Object Name,Length,LastWriteTime"
Get-Item .\osprobe.exe | Select-Object Name, Length, LastWriteTime
Write-Output ""

Write-Output "PS> objdump -p .\osprobe.exe | Select-String 'ImageBase|SizeOfImage|DllCharacteristics'"
& objdump -p .\osprobe.exe | Select-String -Pattern 'ImageBase|SizeOfImage|DllCharacteristics' |
    ForEach-Object { $_.Line }
Write-Output ""
Write-Output "  (ImageBase is what the PE *asks* for, DllCharacteristics bit 0x40 ="
Write-Output "   DYNAMIC_BASE = the loader is allowed to relocate the image, which is"
Write-Output "   what ASLR does. The real base at run time is printed by osprobe.exe [A].)"
Write-Output ""

Write-Output "PS> objdump -p .\osprobe.exe | Select-String 'DLL Name'"
& objdump -p .\osprobe.exe | Select-String -Pattern 'DLL Name' | ForEach-Object { $_.Line }
Write-Output ""
Write-Output "  (only KERNEL32 + the C runtime that gcc links: no ws2_32, no advapi32)"
