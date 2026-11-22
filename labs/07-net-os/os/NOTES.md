# os-lab NOTES（给 Lead 看：实际跑了什么、看到什么、哪里跟预期不一样）

目录：`E:\harness\reversing-journal\labs\07-net-os\os\`

## 文件清单

| 文件 | 说明 |
|---|---|
| `osprobe.c` | 探针源码。只调 Win32 API（KERNEL32.dll + gcc 自带 C runtime），不联网、不需要管理员 |
| `osprobe.exe` | `gcc -O0 -g -Wall -Wextra` 编出来的版本（可被 Ghidra / x64dbg 看得懂的那种） |
| `osprobe_copy.exe` | `run.ps1` 把 `osprobe.exe` 原样复制出来的副本，**字节完全相同**（sha256 见 `evidence\aslr.txt`），专门用来证明"换一个镜像文件，基址就变" |
| `build.ps1` | 一键编译 + 打印 exe 体积 / PE ImageBase / 导入 DLL |
| `run.ps1` | 一键跑：3 次同文件 + 1 次复制文件，并生成 `evidence\` 下所有证据 |
| `evidence\` | `build.txt` `run1.txt` `run2.txt` `run3.txt` `run4_copy.txt` `aslr.txt` `handles.txt` `threads.txt` |

`osprobe.exe --hold 4000` 是给 `run.ps1` 用的：打印完所有东西后**在 --hold 里睡 4000 ms**，
别的窗口才能用 `Get-Process` / `tasklist` 看到这个活着的进程。不带参数直接跑就是打印完退出。

## 怎么跑（真实命令）

这台机器的执行策略是 `RemoteSigned`，直接 `.\build.ps1` 会被拒
（`无法加载文件 ... 未对文件进行数字签名`；实测在本机 E: 盘上直接跑就被拒）。
不改系统设置，用 per-process 放行：

```powershell
cd E:\harness\reversing-journal\labs\07-net-os\os
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\run.ps1
```

或者在当前 PowerShell 里：

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\build.ps1
.\run.ps1
```

实际耗时：`run.ps1` 约 24 秒（每次 run 约 5.5 s：打印 + hold 4 s；中间间隔 2 s；外加复制文件的第 4 次 run）。

## [A] 四个（现在六个）地址放在一起

`evidence\run1.txt` 原样：

```
A1 module base     GetModuleHandle(NULL)                 = 0x00007ff6ec840000
A2 global variable &g_global  (module .data section)     = 0x00007ff6ec846000
A3 heap block      HeapAlloc(GetProcessHeap(), 0, 64)    = 0x000001cf565f13e0
A4 stack local     &local_marker (local var in main())   = 0x000000b03a1ff677
A5 kernel32 base   GetModuleHandle("kernel32.dll")      = 0x00007ff8772c0000
A6 ntdll    base   GetModuleHandle("ntdll.dll")         = 0x00007ff877d90000
```

- A1/A2 只差 `0x6000`：全局变量就在 exe 的**映像**里（.data 段），不是"另一块内存"。
- A3 在 `0x000001cf...`、A4 在 `0x000000b0...`、exe 在 `0x00007ff6...`：三个完全不同的区。
- 排序（程序自己排的，run1 真实输出）：`A4 栈 0x...b03a1ff677 < A3 堆 0x1cf... < A1 exe 0x7ff6... < A2 < A5 kernel32 0x7ff877... < A6 ntdll 0x7ff877d9...`。
  即：**栈最低、堆居中、exe 更高、系统 DLL 最高**。
- 每个地址都用 `VirtualQuery` 回查了它所在的 region（类型 / 状态 / Protect），例如
  A1 → `MEM_IMAGE / MEM_COMMIT / PAGE_READONLY`，A2 → `MEM_IMAGE / MEM_COMMIT / PAGE_WRITECOPY`
  （.data 是写时复制），A3 → `MEM_PRIVATE / PAGE_READWRITE`，A4 → `MEM_PRIVATE / PAGE_READWRITE`。

## [B] VirtualQuery 全地址空间遍历（run1）

```
B0 scanned 0x0000000000000000 .. 0x00007fffffff0000
   regions found: 103
     by State : MEM_COMMIT=62  MEM_RESERVE=15  MEM_FREE=26
     committed by Type: MEM_IMAGE=35  MEM_MAPPED=15  MEM_PRIVATE=12
```

只挑了三类各 1~3 条打印（没有几百行）：

```
IMAGE  base=0x00007ff6ec840000  size=0x00001000 ( 4 KB)  MEM_COMMIT  MEM_IMAGE   PAGE_READONLY          osprobe.exe   ← PE 头
IMAGE  base=0x00007ff6ec841000  size=0x00005000 (20 KB)  MEM_COMMIT  MEM_IMAGE   PAGE_EXECUTE_READ      osprobe.exe   ← 代码段
IMAGE  base=0x00007ff6ec846000  size=0x00001000 ( 4 KB)  MEM_COMMIT  MEM_IMAGE   PAGE_WRITECOPY         osprobe.exe   ← 全局变量 A2
IMAGE  base=0x00007ff874ff0000  size=0x00001000 ( 4 KB)  MEM_COMMIT  MEM_IMAGE   PAGE_READONLY          ucrtbase.dll  ← 别的 DLL
HEAP   base=0x000001cf565e0000  size=0x00012000 (72 KB)  MEM_COMMIT  MEM_PRIVATE PAGE_READWRITE         process heap block A3
HEAP   base=0x000001cf56aa0000  size=0x00010000 (64 KB)  MEM_COMMIT  MEM_PRIVATE PAGE_READWRITE         private heap (HeapCreate)
STACK  base=0x000000b03a1f8000  size=0x00008000 (32 KB)  MEM_COMMIT  MEM_PRIVATE PAGE_READWRITE         main stack
GUARD  base=0x000000b03a1f5000  size=0x00003000 (12 KB)  MEM_COMMIT  MEM_PRIVATE PAGE_READWRITE|PAGE_GUARD  stack guard page
```

同样的 exe 的映像被拆成**好几个 region**（PE 头 4KB `PAGE_READONLY`、代码 20KB `PAGE_EXECUTE_READ`、
数据 4KB `PAGE_WRITECOPY`），这就是"一个 exe 不是一个 region"的真实样子。

坑（第一版写错、已修）：`MEM_FREE` 是 `State` 的值，不是 `Type` 的值；free region 的
`Type` 是 0。第一版按 Type 统计，结果 `MEM_FREE=0`，改成按 State 统计后是 26。

## [C] PID / TID / 句柄 / 线程

```
C1 process id  PID                    = 49328
C2 thread id   TID (this = main thread)= 51956
C3 open handles GetProcessHandleCount() = 56
C4 threads now (Toolhelp32 snapshot inside the process): 2 thread(s), TIDs: 51956 52792
```

句柄数是 56。`run.ps1` 在 --hold 期间用 `Get-Process` 抓到的活进程是 **Handles=59、3 个线程**。
`Get-Process` 的 `Threads` 列显示的是**线程 id 列表**（不是个数），比如 `{51956, 52792, 42308}`。

## [D] 再开一个线程 + 从 TEB 读它自己的栈（run1）

```
D0 TEB self = gs:0x30 = 0x000000b039f88000   PEB = gs:0x60 = 0x000000b039f87000
D1 main    TID=51956  [TEB+0x08] StackBase  = 0x000000b03a200000
D2 main    TID=51956  [TEB+0x10] StackLimit = 0x000000b03a1fa000
D3 worker  TID=42308  [TEB+0x08] StackBase  = 0x000000b03a600000
D4 worker  TID=42308  [TEB+0x10] StackLimit = 0x000000b03a5fd000
D5 threads now (Toolhelp32 snapshot inside the process): 3 thread(s), TIDs: 51956 52792 42308
   main stack  : StackBase=0x000000b03a200000  StackLimit=0x000000b03a1fa000  size=24 KB
   worker stack: StackBase=0x000000b03a600000  StackLimit=0x000000b03a5fd000  size=12 KB
```

- 栈范围是**线程里自己**用 `movq %gs:0x08 / %gs:0x10` 读的，不是父线程猜的。
- 两个线程的栈完全分开：`0x...a200000` vs `0x...a600000`（同一次开机里差 4MB，但每次运行都重新随机）。
- `main stack` 的 24 KB 是**当时已提交**的大小，不是 1MB 保留量：`StackBase - StackLimit = 0x6000`。
  同一个程序的 A4 那一行在 [A] 里显示 region 只有 4KB，到 [B] 就变成 32KB —— **栈是按需长的**，
  这就是 guard page 的用处（输出里 `GUARD ... PAGE_GUARD`）。

## [E] 5000 次真 syscall 的 user/kernel 时间（**跟预期不一样，重点**）

这台机器的 `GetProcessTimes` 只按**时钟节拍**（`GetSystemTimeAdjustment` 报 `TimeIncrement=156250`，
即 **15.625 ms**）跳变。5000 次 `CreateEventW+CloseHandleW` 只要 **2.3 ms**，不到一个节拍，
所以 5000 这一行读出来是 **0.000 / 0.000**（这是仪器分辨率下限，不是"系统调用免费"）。

真实输出（run1）：

```
    calls    wall ms   user ms  kernel ms   user tick  kern tick
     5000      2.332     0.000      0.000          0         0     ← 本实验要求的 5000 次
   400000    194.867    46.875    140.625          3         9
  1600000    787.820    31.250    750.000          2        48
   per pair, from the 1600000-call batch:
     wall    0.492 us
     kernel  0.469 us
     user    0.020 us
   cross-check: the 5000-call batch took 2.332 ms of wall time = 0.466 us/pair
   open handles before=58 after=58   (2005000 create+close pairs, 没有泄漏)
```

结论（可以写进文档的真数字）：

- 一对 `CreateEventW+CloseHandleW` 的墙钟约 **0.49 µs**，其中 **0.469 µs 记在内核时间**，用户态只占约 **0.02 µs**。
  即 **约 95% 的成本在内核**。
- 200.5 万对调用前后，句柄数 `58 → 58`，没泄漏。
- 数字会跳 **15.625 ms** 一格（表里 user tick / kernel tick 就是"几个节拍"）；
  仅 5000 次时读数可能碰巧是 0，也可能是 1 个节拍，属于量化误差，不是程序在骗人。

## [F] GetFileType：句柄指向什么

```
F1 CreateFileW("osprobe_tmp.txt")      handle=0x00000000000000d4  type=0x00000001  FILE_TYPE_DISK  (真实磁盘文件)
F2 CreatePipe() write end              handle=0x00000000000000c8  type=0x00000003  FILE_TYPE_PIPE  (匿名管道，无文件名)
F3 CreateFileW("CONOUT$")              handle=0x00000000000000e0  type=0x00000002  FILE_TYPE_CHAR  (控制台=字符设备)
F4 GetStdHandle(STD_OUTPUT_HANDLE)     handle=0x00000000000009c4  type=0x00000001  FILE_TYPE_DISK
```

**注意 [F4]**：`run.ps1` 把 stdout 重定向到 `evidence\run1.txt`，所以这一次 stdout 不是控制台而是
磁盘文件（type=1）。真正"控制台"的案例用 `CreateFileW("CONOUT$")` 单独开（type=2）。
如果用户直接在自己的终端里 `.\osprobe.exe`，F4 会变成 `FILE_TYPE_CHAR`。
句柄值本身（0xd4 / 0xc8 / 0xe0 / 0x9c4）没有"文件/管道/控制台"的信息，**类型要问内核**（GetFileType）。

## ASLR：**镜像基址不是每次运行都变**（与最初预期不同，已按 Lead 口径重做）

`evidence\aslr.txt` 是 3 次同文件 + 1 次复制文件的对照表，全部真实数字：

### 表 1：同一个 `osprobe.exe` 连续 3 次（同一次开机，没重启）

```
A1 module base               0x00007ff6ec840000  0x00007ff6ec840000  0x00007ff6ec840000  SAME
A5 kernel32.dll base         0x00007ff8772c0000  0x00007ff8772c0000  0x00007ff8772c0000  SAME
A6 ntdll.dll base            0x00007ff877d90000  0x00007ff877d90000  0x00007ff877d90000  SAME
A3 heap block                0x000001cf565f13e0  0x0000021b7aa91110  0x000001f5a1a61110  DIFFERENT
A4 stack local               0x000000b03a1ff677  0x000000ac38bffda7  0x0000004d993ffd07  DIFFERENT
D1 main StackBase (TEB+0x08) 0x000000b03a200000  0x000000ac38c00000  0x0000004d99400000  DIFFERENT
D2 main StackLimit (TEB+0x10)0x000000b03a1fa000  0x000000ac38bfa000  0x0000004d993fa000  DIFFERENT
D3 worker StackBase          0x000000b03a600000  0x000000ac39000000  0x0000004d99800000  DIFFERENT
D4 worker StackLimit         0x000000b03a5fd000  0x000000ac38ffd000  0x0000004d997fd000  DIFFERENT
Z  main stack size (D1-D2)   24 KB               24 KB               24 KB               SAME
```

### 表 2：`osprobe.exe` vs 字节完全相同的 `osprobe_copy.exe`

```
sha256(osprobe.exe)      = 6e7f30841335c7c3b3c99c8b64a7f6ce92626b451ca8ea40d5da059b87917304
sha256(osprobe_copy.exe) = 6e7f30841335c7c3b3c99c8b64a7f6ce92626b451ca8ea40d5da059b87917304   ← 同字节

A1 module base   0x00007ff6ec840000  →  0x00007ff79f940000   CHANGED
A5 kernel32 base 0x00007ff8772c0000  →  0x00007ff8772c0000   SAME
A6 ntdll base    0x00007ff877d90000  →  0x00007ff877d90000   SAME
A4 stack local   0x000000b03a1ff677  →  0x000000d4d8fff6e7   CHANGED（栈本来就逐进程随机）
```

一句话口径（写文档就用这个，不要写"每次都变"）：

- **同一个 exe 文件 + 同一次开机 → 模块基址固定**（3 次全一样）。
- **换一个镜像文件（哪怕字节完全相同，只是改个名字）→ 模块基址变**。
  说明系统确实把 exe 搬到了一个随机基址（PE 里写的 ImageBase 是 `0x140000000`，
  实际却在 `0x7ff6...`），只是"随机结果对同一个文件固定"。
- **栈、堆的地址每个进程都重新随机**（3 次全不同；栈范围整体平移，大小恒定 24 KB）。
- 系统 DLL（kernel32/ntdll）基址 3 次相同：它们是共享映射，一次开机期间所有进程同一个基址。
  这正是"调试器里 ntdll/kernel32 地址看起来稳定"的原因。
- 对逆向教学的意义：Ghidra 里 `0x140001450` = `ImageBase(0x140000000) + RVA 0x1450`；
  x64dbg 里实际是 `随机基址 + 同一个 RVA`。**RVA / 文件偏移不变**，所以硬编码偏移、特征码
  一直能用；写死的绝对地址（0x140001450 / 0x7FF7...）不能用。

## handles.txt / threads.txt

`run.ps1` 在进程 **--hold 4000 的睡眠窗口内**（+1.5 s）抓 `Get-Process` 和 `tasklist`，
每个片段都带 `(process was still alive at snapshot time: True)`。3 次 run 各一段。

真实内容（run1）：`Handles=59`，`Threads={51956, 52792, 42308}`；`tasklist /v` 那行显示
`osprobe.exe 49328 Console ... 4,656 K`（进程确实在）。

## 跟预期 / 常见说法不一样的地方（请 Lead 注意，别写错）

1. **镜像基址不是"每次进程都变"**：同文件同 boot 固定；改文件（重新编译 / 复制改名）才变。
   以上有 3 次 + 复制文件的实测表。
2. **GetProcessTimes 有 15.625 ms 的量化**：5000 次调用读成 `0.000`；放大到 160 万次才稳稳看到
   `kernel 750 ms`。文档里要把"5000 次 → 0.000"解释成仪器分辨率，而不是"系统调用不花时间"。
3. **进程里比代码多一个线程**：`C4` 显示我们**还没调用 CreateThread** 时进程里已有 2 个线程
   （TIDs 51956 52792）；`D5` 在我们建完工作线程后是 3 个（51956 52792 42308）。
   我们代码只创建了 1 个（42308）。对照实验：写一个只 `Sleep` 的空程序，`Get-Process` 也看到
   2 个线程。结论写法："线程数不等于你的代码创建数；以自己打印的 TID 为准。"
4. **stdout 重定向后 GetFileType 不是 CHAR**：`run.ps1` 收集证据时 stdout 是磁盘文件（type=1），
   所以 `[F]` 里控制台用 `CONOUT$` 单独演示（type=2）。直接跑 exe 才看到 F4=CHAR。
5. **栈的大小会变**：A4 的 region 是 4KB（刚提交），走到 B 时 32KB，因为程序调用深了。
   别把某个时刻的 region size 当成"栈的大小"。
6. `MEM_FREE` 是 State 不是 Type（第一版写错，已修）。
7. Windows PowerShell 5.1 下 `Start-Process -PassThru` 的 `ExitCode` 读出来是空的（实测
   `cmd /c exit 7` 也读不到），所以 `handles.txt` 里不放 exit code；进程退出码以
   `run*.txt` 末尾程序自己打印的 `exit code 0` 为准。
8. 本 lab 只链接 KERNEL32 + gcc 的 C runtime（`evidence\build.txt` 里的 `DLL Name` 列表：
   `KERNEL32.dll` + 一串 `api-ms-win-crt-*`），没有 ws2_32/advapi32；没有提权、没有抓包
   （抓包是 capture-lab 的事，非管理员也抓不了）。

## 复现命令（给用户）

```powershell
cd E:\harness\reversing-journal\labs\07-net-os\os
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\run.ps1
# 只看程序本身（不落盘）：
.\osprobe.exe
# 想自己一边看一边用 Get-Process 查它：
Start-Process .\osprobe.exe -ArgumentList '--hold','30000'
Get-Process osprobe | Select-Object Id,Handles,Threads
```
