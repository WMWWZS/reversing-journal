# Lab 02 · 一个真进程的地址空间：ASLR / TEB / 句柄 / syscall

对应笔记：[docs/02-process-and-thread.md](../../docs/02-process-and-thread.md)、[docs/04-virtual-memory.md](../../docs/04-virtual-memory.md)、[docs/01-kernel-boundary.md](../../docs/01-kernel-boundary.md)

> 姊妹仓库里的**逆向视角**版：`reversing-journal` 的 `docs/14-operating-system.md` + `labs/07-net-os/os/`。
> 两边用的是**同一份 evidence**（同一台机器、同一次开机录的）。本 lab 问「这个数字为什么是这样」，那边问「这段代码为什么会长成这样」。

## 目的

1. 把「进程 = 一张地址空间 + 一张句柄表 + 至少一个线程」从一句话变成**六个能自己读出来的地址**
2. 亲手测一次 ASLR，得到三条结论（而不是背「ASLR 每次随机」）
3. 从 `TEB+0x08` / `TEB+0x10` 直接读出**线程自己**的栈范围，理解栈为什么是「按需长大」的
4. 用 `GetProcessTimes` 量一次真 syscall：160 万对 `CreateEventW + CloseHandleW`，看成本落在 user 还是 kernel
5. 踩一次仪器分辨率的坑：15.625 ms 的时钟粒度，会让 5000 次 syscall 读成 `0.000 ms`

## 文件

| 文件 | 作用 |
|:---:|---|
| `osprobe.c` | 六段观测 `[A]`~`[F]`，单文件、只调 Win32 API、无第三方库 |
| `build.ps1` | 一键编译（`gcc -O0 -g`），并打印 PE 头里的 `ImageBase` 与导入表 |
| `run.ps1` | 跑 4 次（同文件 ×3 + 字节相同的副本 ×1），抓活进程快照，生成 `evidence/` |
| `evidence/` | 真实输出原样保存：`run1.txt` `run2.txt` `run3.txt` `run4_copy.txt` `aslr.txt` `handles.txt` `threads.txt` `build.txt` |
| `README.md` | 你正在看的这份 |

编译产物 `osprobe.exe` / `osprobe_copy.exe` 已被 `.gitignore` 忽略，**不提交二进制**。

## 构建与运行

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1   # 只编译，顺便看一眼 PE 头
powershell -ExecutionPolicy Bypass -File .\run.ps1     # 跑 4 次 + 生成 evidence\（约 24 秒）
```

只想看一次、不落盘、不动 `evidence/`：

```powershell
.\osprobe.exe
.\osprobe.exe --hold 30000      # 打印完停留 30 秒，另一个窗口可以用 Get-Process 看这个活进程
```

`build.ps1` 只做「编译 + 打印」，`run.ps1` 只做「跑 + 记录」：不改系统设置、不装东西、不需要管理员权限。

> `run.ps1` 会**重写** `evidence/` 下的同名文件——这是它存在的意义：用你机器的真实数字替换。
> 想保留仓库里这份记录，先复制一份再跑。

## 实测环境

| 项 | 值 |
|:---:|---|
| OS | Windows 11 x64 |
| 编译器 | MinGW-w64 gcc 14.2.0（`-O0 -g -Wall -Wextra`） |
| PE 头里「申请」的 ImageBase | `0x0000000140000000` |
| 时钟粒度 | 15.625 ms（`GetSystemTimeAdjustment` 的 `TimeIncrement=156250` × 100 ns） |
| 页大小 | 4096 字节 |

> 数字随机器状态变化。**绝对值只对这台机器有意义，量级和结论才是可迁移的。**
>
> `evidence/` 里 `aslr.txt` 那个 exe 哈希，是录制这次运行时那份构建产物的；你自己重新编译时，`-g` 会把源文件路径写进调试信息，哈希自然不同——**ASLR 的对照结论不受影响**。
> 同理，`[F]` 里 F1 那行还留着录制时的绝对路径（当时这个探针放在姊妹仓库里），内容才是重点。

## 真实输出

下面是 `evidence/run1.txt` 的原样输出（不是理论值，不是编的）：

```text
================================================================
 osprobe.exe -- real numbers from a real Windows process
 built with MinGW-w64 gcc 14.2.0, -O0 -g, Win32 API only
================================================================

--- [A] THE ADDRESSES IN ONE PLACE (address-space partitions) ---
A1 module base     GetModuleHandle(NULL)                 = 0x00007ff6ec840000
A2 global variable &g_global  (module .data section)     = 0x00007ff6ec846000
A3 heap block      HeapAlloc(GetProcessHeap(), 0, 64)    = 0x000001cf565f13e0
A4 stack local     &local_marker (local var in main())   = 0x000000b03a1ff677
A5 kernel32 base   GetModuleHandle("kernel32.dll")      = 0x00007ff8772c0000
A6 ntdll    base   GetModuleHandle("ntdll.dll")         = 0x00007ff877d90000
   (g_global = 0x1122334455667788, local_marker = 0x5a, the heap block was filled with 0xAB)
   (A5/A6: kernel32 and ntdll are shared image mappings. In this boot every
    process sees the same base for them, and a debugger shows that value.)

   where each of them really lives (VirtualQuery on each address):
   A1 -> region base=0x00007ff6ec840000 size=0x00001000 state=MEM_COMMIT  type=MEM_IMAGE   Protect=PAGE_READONLY    module=osprobe.exe
   A2 -> region base=0x00007ff6ec846000 size=0x00001000 state=MEM_COMMIT  type=MEM_IMAGE   Protect=PAGE_WRITECOPY   module=osprobe.exe
   A3 -> region base=0x000001cf565f1000 size=0x00001000 state=MEM_COMMIT  type=MEM_PRIVATE Protect=PAGE_READWRITE
   A4 -> region base=0x000000b03a1ff000 size=0x00001000 state=MEM_COMMIT  type=MEM_PRIVATE Protect=PAGE_READWRITE
   (note: A4's region size is only what the stack has committed SO FAR;
    the stack grows on demand, so by the time [B] runs more pages are in)

   sorted from LOW address to HIGH address (this run):
     0x000000b03a1ff677  A4 local variable (stack)
     0x000001cf565f13e0  A3 heap block
     0x00007ff6ec840000  A1 module base (osprobe.exe image)
     0x00007ff6ec846000  A2 global g_global (inside that image)
     0x00007ff8772c0000  A5 kernel32.dll (shared image)
     0x00007ff877d90000  A6 ntdll.dll (shared image, usually highest)
   low  = stack (grows down from StackBase), then the heap
   high = the .exe image, and the system DLLs above it

--- [B] VirtualQuery(): WALK THE WHOLE ADDRESS SPACE OF THIS PROCESS ---
B0 scanned 0x0000000000000000 .. 0x00007fffffff0000
   regions found: 103
     by State : MEM_COMMIT=62  MEM_RESERVE=15  MEM_FREE=26
     committed by Type: MEM_IMAGE=35  MEM_MAPPED=15  MEM_PRIVATE=12
   (a 'region' is one run of pages with the same state/type/Protect.
    The kernel keeps them in this process's VAD tree; VirtualQuery walks it.)

B1 MEM_IMAGE regions: the .exe and every DLL it loaded
IMAGE  base=0x00007ff6ec840000  size=0x00001000 (     4 KB)  state=MEM_COMMIT  type=MEM_IMAGE    Protect=PAGE_READONLY                  osprobe.exe
IMAGE  base=0x00007ff6ec841000  size=0x00005000 (    20 KB)  state=MEM_COMMIT  type=MEM_IMAGE    Protect=PAGE_EXECUTE_READ              osprobe.exe
IMAGE  base=0x00007ff6ec846000  size=0x00001000 (     4 KB)  state=MEM_COMMIT  type=MEM_IMAGE    Protect=PAGE_WRITECOPY                 osprobe.exe
IMAGE  base=0x00007ff874ff0000  size=0x00001000 (     4 KB)  state=MEM_COMMIT  type=MEM_IMAGE    Protect=PAGE_READONLY                  ucrtbase.dll

B2 MEM_PRIVATE heap regions: the regions that hold the HeapAlloc blocks
HEAP   base=0x000001cf565e0000  size=0x00012000 (    72 KB)  state=MEM_COMMIT  type=MEM_PRIVATE  Protect=PAGE_READWRITE                 process heap block A3
HEAP   base=0x000001cf56aa0000  size=0x00010000 (    64 KB)  state=MEM_COMMIT  type=MEM_PRIVATE  Protect=PAGE_READWRITE                 private heap (HeapCreate)

B3 MEM_PRIVATE stack regions: the thread stack
STACK  base=0x000000b03a1f8000  size=0x00008000 (    32 KB)  state=MEM_COMMIT  type=MEM_PRIVATE  Protect=PAGE_READWRITE                 main stack
GUARD  base=0x000000b03a1f5000  size=0x00003000 (    12 KB)  state=MEM_COMMIT  type=MEM_PRIVATE  Protect=PAGE_READWRITE|PAGE_GUARD      stack guard page
   (the guard page is how Windows grows a stack: touch it -> kernel commits
    the next page and moves the guard one page lower)

--- [C] PROCESS / THREAD IDENTITY ---
C1 process id  PID                    = 49328
C2 thread id   TID (this = main thread)= 51956
C3 open handles GetProcessHandleCount() = 56
   (a handle is an index into this process's handle table; every file,
    event, thread, process ... you open costs one slot)
C4 threads now (Toolhelp32 snapshot inside the process): 2 thread(s), TIDs: 51956 52792
   (our code has NOT called CreateThread at this point - main is the only
    thread we created. If the line above shows 2, something else created
    one inside this process. That is possible on Windows, and it is why a
    tool like Get-Process can show more threads than your code made.)

--- [D] A REAL SECOND THREAD AND ITS OWN STACK (read from the TEB) ---
D0 TEB self = gs:0x30 = 0x000000b039f88000   PEB = gs:0x60 = 0x000000b039f87000
   (gs points at the TEB: NT_TIB is the first part of it, so
    gs:0x08 = TEB+0x08 = StackBase, gs:0x10 = TEB+0x10 = StackLimit)
D1 main    TID=51956  [TEB+0x08] StackBase  = 0x000000b03a200000
D2 main    TID=51956  [TEB+0x10] StackLimit = 0x000000b03a1fa000
   CreateThread() -> hThread = 0x00000000000000b4  (waiting for it to report)
D3 worker  TID=42308  [TEB+0x08] StackBase  = 0x000000b03a600000
D4 worker  TID=42308  [TEB+0x10] StackLimit = 0x000000b03a5fd000
D5 threads now (Toolhelp32 snapshot inside the process): 3 thread(s), TIDs: 51956 52792 42308
   (that is our own count: main thread + the one we just created.
    A tool like Get-Process may show MORE threads than this: any
    process may create a thread inside yours. We saw one extra on
    this machine - it is not in our code.)
   main stack  : StackBase=0x000000b03a200000  StackLimit=0x000000b03a1fa000  size=24 KB
   worker stack: StackBase=0x000000b03a600000  StackLimit=0x000000b03a5fd000  size=12 KB
   the two threads have TWO DIFFERENT stacks: 0x000000b03a200000 vs 0x000000b03a600000
   worker's local var &w_local = 0x000000b03a5ff92b -> VirtualQuery: region base=0x000000b03a5ff000 size=0x00001000 type=MEM_PRIVATE Protect=PAGE_READWRITE

[HOLD] process stays alive for 4000 ms so another window can look at it:
       Get-Process -Id 49328 | Select-Object Id,Handles,Threads
   worker thread was told to exit (SetEvent) and joined; thread count back to 1

--- [E] 5000 REAL SYSTEM CALLS vs 5000 PURE USER-SPACE ITERATIONS ---
   this machine's clock tick: GetSystemTimeAdjustment TimeIncrement=156250 x100ns
   = 15.6250 ms.  GetProcessTimes() only moves in whole ticks here, so a
   batch shorter than one tick reads as +0.000 ms. That is the
   resolution floor, NOT "the syscall was free" (see scaling table).

CONTROL (no syscall)   5000 x "acc += i"
   wall(QueryPerformanceCounter) +   0.002 ms
   user +   0.000 ms   kernel +   0.000 ms   <- 0 syscalls, 0 kernel time
   (the loop really ran: acc = 12497500)

SYSCALL batches: CreateEventW + CloseHandleW
     calls    wall ms   user ms  kernel ms   user tick  kern tick
      5000      2.332     0.000      0.000          0         0
    400000    194.867    46.875    140.625          3         9
   1600000    787.820    31.250    750.000          2        48
   (calls=5000 is the batch this lab asks for: its wall time is only ~2.4 ms,
    i.e. below one 15.6 ms tick, so its CPU columns can read +0.000 ms - that
    is the resolution floor, not a free syscall; the bigger batches fix that)

   per pair, from the 1600000-call batch:
     wall    0.492 us
     kernel  0.469 us   <- the 1600000 kernel entries/exits are billed here
     user    0.020 us
   cross-check: the 5000-call batch took 2.332 ms of wall time = 0.466 us/pair;
   the big batch says 0.488 us/pair of CPU total, the same number.
   open handles before=58 after=58  (2005000 create+close pairs, no leak)
   reading: kernel time is where the 2005000 calls were paid; user-mode code in
            front of them costs a small fraction. Both CPU columns move in
            whole ticks, so one batch can read 0 or 1 tick too high/low.

--- [F] GetFileType() ON THREE DIFFERENT KINDS OF HANDLE ---
F1 CreateFileW("osprobe_tmp.txt")      handle=0x00000000000000d4  type=0x00000001  FILE_TYPE_DISK    (E:\harness\reversing-journal\labs\07-net-os\os\osprobe_tmp.txt)
F2 CreatePipe() write end              handle=0x00000000000000c8  type=0x00000003  FILE_TYPE_PIPE    (anonymous pipe: kernel object, no filename)
F3 CreateFileW("CONOUT$")              handle=0x00000000000000e0  type=0x00000002  FILE_TYPE_CHAR    (console = character device)
F4 GetStdHandle(STD_OUTPUT_HANDLE)     handle=0x00000000000009c4  type=0x00000001  FILE_TYPE_DISK    -> this run's stdout is redirected to a FILE on disk (that is why it is not CHAR)
  reading: the handle value alone does not tell you what it is;
           GetFileType asks the kernel what object it points at.
           Run osprobe.exe directly in a console and F4 becomes FILE_TYPE_CHAR.

--- done: osprobe.exe finished, exit code 0 ---
PID was 49328, main TID was 51956
```

## 怎么读这张表

### 1. 六个地址 = 四层分区

先把 `[A]` 的六个地址按 `[A]` 自己排好的顺序看（低 → 高）：

| 顺序 | 地址 | 是什么 | `VirtualQuery` 判定 |
|:---:|---|---|---|
| 1 | `0x000000b03a1ff677` | A4 栈上的局部变量 | `MEM_PRIVATE` / `PAGE_READWRITE` |
| 2 | `0x000001cf565f13e0` | A3 堆块 | `MEM_PRIVATE` / `PAGE_READWRITE` |
| 3 | `0x00007ff6ec840000` | A1 exe 映像基址 | `MEM_IMAGE` / `PAGE_READONLY` |
| 4 | `0x00007ff6ec846000` | A2 全局变量（.data 段） | `MEM_IMAGE` / `PAGE_WRITECOPY` |
| 5 | `0x00007ff8772c0000` | A5 `kernel32.dll` | 共享映像 |
| 6 | `0x00007ff877d90000` | A6 `ntdll.dll` | 共享映像 |

三条能直接用的结论：

- **栈最低、堆居中、exe 更高、系统 DLL 最高**。但「谁在谁上面」不是 API 保证的顺序，**别背地址，要自己用 `VirtualQuery` 查**。
- **A1 和 A2 只差 `0x6000`**：全局变量不是「另一块内存」，它就在 exe 的映像里（`.data` 段）。`[B]` 更直接：同一个 exe 被拆成多个 region——PE 头 4 KB `PAGE_READONLY`、代码 20 KB `PAGE_EXECUTE_READ`、数据 4 KB `PAGE_WRITECOPY`。
- **`.data` 的 `PAGE_WRITECOPY`** 就是「写时复制」：读的时候和磁盘上的文件共享物理页，第一次写才复制一份私有的。

`[B]` 那 103 个 region 是这么分的（`MEM_FREE` 是 **State** 的值，不是 Type 的值——free region 的 Type 是 0，第一版统计错就错在这）：

| 维度 | 计数 | 含义 |
|---|---|---|
| State | `MEM_COMMIT=62`、`MEM_RESERVE=15`、`MEM_FREE=26` | 已提交 / 只预留 / 空闲 |
| 已提交的 Type | `MEM_IMAGE=35`、`MEM_MAPPED=15`、`MEM_PRIVATE=12` | 映像（exe+DLL）/ 映射 / 进程私有 |

`MEM_IMAGE=35` 是 **35 个「映像 region」**，不是 35 个模块——同一个 exe 自己就占了 3 个 region（PE 头 / 代码 / 数据，见 `B1`）。
所以别拿这个数字当模块数；要看模块列表得自己枚举，本探针没有打印。

### 2. ASLR：三条实测结论

PE 头里 `objdump -p` 读到的 `ImageBase = 0x0000000140000000`，只是**编译时「我希望被加载到哪」**；
真跑起来 `GetModuleHandle(NULL)` 给的是 `0x00007ff6ec840000`。中间那一步就是 ASLR。

`evidence/aslr.txt` 是 3 次同文件 + 1 次字节相同的副本（`run.ps1` 现场复制、sha256 一致）的对照表：

**表 1：同一个 `osprobe.exe` 连续跑 3 次（同一次开机，中间没重启）**

| # | 是什么 | run1 | run2 | run3 | 结论 |
|:---:|---|---|---|---|:---:|
| A1 | 模块基址 | `0x00007ff6ec840000` | 同 | 同 | **同** |
| A5 | kernel32 基址 | `0x00007ff8772c0000` | 同 | 同 | **同** |
| A6 | ntdll 基址 | `0x00007ff877d90000` | 同 | 同 | **同** |
| A3 | 堆块 | `0x000001cf565f13e0` | `0x0000021b7aa91110` | `0x000001f5a1a61110` | **变** |
| A4 | 栈局部变量 | `0x000000b03a1ff677` | `0x000000ac38bffda7` | `0x0000004d993ffd07` | **变** |
| D1/D2 | 主线程栈范围 | `0x000000b03a200000` / `0x000000b03a1fa000` | `0x000000ac38c00000` / `0x000000ac38bfa000` | `0x0000004d99400000` / `0x0000004d993fa000` | **变** |
| Z | 主线程栈大小（D1−D2） | 24 KB | 24 KB | 24 KB | **同** |

**表 2：`osprobe.exe` vs 字节完全相同的 `osprobe_copy.exe`（只改了文件名）**

| # | 是什么 | `osprobe.exe`（run1） | `osprobe_copy.exe`（run4） | 结论 |
|:---:|---|---|---|---|
| A1 | 模块基址 | `0x00007ff6ec840000` | `0x00007ff79f940000` | **变** |
| A5 | kernel32 基址 | `0x00007ff8772c0000` | `0x00007ff8772c0000` | 同 |
| A6 | ntdll 基址 | `0x00007ff877d90000` | `0x00007ff877d90000` | 同 |
| A4 | 栈局部变量 | `0x000000b03a1ff677` | `0x000000d4d8fff6e7` | 变 |

三条结论（记这三条就够了）：

1. **栈、堆**：每个进程创建时重新抽随机数 → 同一个程序跑两次，`[rbp-0x8]` 的绝对地址不可能一样。但**大小恒定**（24 KB），随机的是位置不是形状。
2. **镜像基址（exe/DLL）**：同一次开机 + 同一个文件 → **固定不变**；换一个镜像文件（本次实测：字节相同、只改文件名的副本）→ 变。
   **不要写成「每次运行基址都变」**——这份证据里 3 次同文件的 A1 一模一样，第 4 次只是改了个文件名就变了。
3. **系统 DLL 更稳**：kernel32/ntdll 是整个开机期间所有进程**共享映射**，所以基址对所有进程都一样——这就是调试器里看 ntdll 地址「很稳」的原因。

> 重启之后会不会变？本次**没有重启对照**，所以只能说「同一次开机内固定」。没有观测的部分不下结论。

对逆向的意义，手算一次就再也不会踩坑：Ghidra 里函数在 `0x140001450`，PE 的 ImageBase 是 `0x140000000`，
所以 RVA = `0x140001450 - 0x140000000 = 0x1450`；运行时地址 = `0x7ff6ec840000 + 0x1450 = 0x7ff6ec841450`。
**RVA / 文件偏移永远不变，绝对地址不能写死。**

### 3. TEB + 0x08 / + 0x10：栈是线程的

x64 下 `gs` 指向当前线程的 TEB，TEB 的头就是 `NT_TIB`：

| 偏移 | 字段 | 本机读到 |
|:---:|---|---|
| `gs:0x08` | `StackBase`（栈底，高地址） | main `0x000000b03a200000` / worker `0x000000b03a600000` |
| `gs:0x10` | `StackLimit`（已提交的最低处） | main `0x000000b03a1fa000` / worker `0x000000b03a5fd000` |
| `gs:0x30` | `Self`（TEB 自己的地址） | `0x000000b039f88000` |
| `gs:0x60` | `PEB` | `0x000000b039f87000` |

两个线程：**代码共享，栈各自**。main 栈 `0x...a200000` 往下长，worker 栈 `0x...a600000` 往下长，同一次开机里差 4 MB；但每次运行整体平移（见 ASLR 表 1）。

`24 KB` 和 `12 KB` 是 **`StackBase - StackLimit`，即当时已提交的量**，不是「栈的大小」——Windows 默认预留 1 MB（`CreateThread` 的 `dwStackSize` 默认值，可改），
用多少提交多少。证据里有三个互相印证的细节：

- `[A]` 时 A4 所在 region 只有 **4 KB**（`0x000000b03a1ff000`），走到 `[B]` 时主栈 region 已经是 **32 KB**（`0x000000b03a1f8000`）——同一个 run 里长出来的。
- `[B]` 抓到守卫页：`0x000000b03a1f5000`、**12 KB**、`PAGE_READWRITE|PAGE_GUARD`。碰到 `PAGE_GUARD`，内核就知道「栈要长了」：提交下一页、把守卫页往下挪一格。
- worker 的局部变量 `0x000000b03a5ff92b` 落在 `0x000000b03a5ff000` 这个 region 里——它确实在上面的 `0x...a5fd000 ~ 0x...a600000` 区间内。

### 4. 句柄：号码，不是地址

`C3` 读到 **56** 个句柄。每打开一个内核对象（文件、事件、socket、线程……），进程的句柄表就占一格，
返回给你的数字就是「表里的第几格」。

`[F]` 用三种对象说明「光看数字猜不出类型」：

| 对象 | 句柄 | `GetFileType` | 说明 |
|---|---|---|---|
| 磁盘文件 | `0xd4` | `FILE_TYPE_DISK`（1） | 有路径、有 `FILE_OBJECT` |
| 匿名管道（写端） | `0xc8` | `FILE_TYPE_PIPE`（3） | 内核对象，**没有文件名** |
| 控制台 `CONOUT$` | `0xe0` | `FILE_TYPE_CHAR`（2） | 字符设备 |
| 本次 stdout | `0x9c4` | `FILE_TYPE_DISK`（1） | 因为 `run.ps1` 把 stdout **重定向到文件**了 |

`F4` 是本 lab 最实用的一条：**`stdout` 只是一个句柄，指向哪由启动它的人决定**。直接在终端里跑 `.\osprobe.exe`，F4 就会变成 `FILE_TYPE_CHAR`。

> 顺便解释一个容易看岔的地方：`C3 = 56`，但 `evidence/handles.txt` 里同一个进程（在 `--hold` 里）用 `Get-Process` 抓到的是 **59**。
> 差 3 个不是矛盾：`C3` 打印在 `[D]` **之前**，之后主线程又创建了 2 个事件 + 1 个工作线程句柄，正好 +3。
> 所以「句柄数」是一个快照，不是一个常数。

### 5. syscall：160 万对的成本记在内核

对照组是 5000 次纯算术循环：wall `0.002 ms`，user/kernel 都是 `0.000 ms`，`acc = 12497500`——循环真跑了，但一次内核都没进。

真 syscall 的批次表：

| calls | wall ms | user ms | kernel ms |
|---:|---:|---:|---:|
| 5000 | 2.332 | 0.000 | 0.000 |
| 400000 | 194.867 | 46.875 | 140.625 |
| 1600000 | 787.820 | 31.250 | 750.000 |

从 160 万那批算单次：

- **一对 `CreateEventW + CloseHandleW` ≈ 0.49 µs**（wall 0.492）
- 其中 **kernel 0.469 µs**、user 0.020 µs → **约 95% 的成本在内核**
- 交叉验证：5000 那批 wall 2.332 ms ÷ 5000 = **0.466 µs/pair**，和 160 万批的 CPU 合计 0.488 µs/pair 是同一个数
- 2,005,000 对跑完，句柄数 `58 → 58`，**没有泄漏**

结论：**一次 syscall 的钱主要花在「进内核 + 出内核」**，前端用户态代码只占零头。所以高性能程序都在想办法少进内核（批量、IOCP、一次收多个 buffer）——这正是 lab01 那个「70 ns vs 2 ns」的延续。

### 6. 15.625 ms 的坑：5000 次读成 0.000

`GetSystemTimeAdjustment` 报 `TimeIncrement=156250`（×100 ns）= **15.625 ms**。这台机器的 `GetProcessTimes`
只在时钟节拍上跳：5000 对 syscall 只花 **2.332 ms**，不到一格，所以 CPU 两列读成 `0.000`。

**这不是「系统调用免费」，是仪器分辨率不够。** 证据里专门放了「tick」两列，把量化过程露出来：

- 40 万批：user 3 tick、kernel 9 tick
- 160 万批：user **2** tick、kernel 48 tick

调用数变成 4 倍，user 的 tick 反而从 3 变成 2——这一格的差异就是**量化噪声**（±1 tick）。所以：**计数不够大时，读到 0 只能说明「小于一格」，不能说明「等于 0」。** 要量小东西，就放大批次再除。

### 7. 多出来的那个线程

`C4` 是 `[C]` 段打的，那时我们的代码**还没调用过 `CreateThread`**，但进程里有 **2 个线程**：
`51956`（主线程，`C2`）和 `52792`（不是我们的代码建的）。`[D]` 建完工作线程 `42308` 之后，`D5` 变成 3 个。

`evidence/threads.txt` 里那 3 个线程全是 `Wait`：

| TID | ThreadState | WaitReason |
|---:|---|---|
| 51956 | Wait | ExecutionDelay（主线程在 `Sleep`） |
| 52792 | Wait | EventPairLow（不是我们的代码建的） |
| 42308 | Wait | UserRequest（我们 `CreateThread` 的工作线程，在等 `SetEvent`） |

能往别人进程里创建线程的机制就是 `CreateRemoteThread` / `NtCreateThreadEx` 这一类。**具体是谁、为什么，本次没有继续观测**——但「线程数 ≠ 你创建的线程数」这一条是实测的，所以 `Get-Process` 显示的线程数比你的代码多时，先别当成 bug。

## 自测题

> 每题写清三件事：**考点** · **做完你应该明白什么** · **怎么算真的会了**。

### 自测 1：同一个 exe 跑 3 次基址一模一样，复制改个文件名就变了。为什么？

**考点**：ASLR 随机的是「镜像加载在哪」，随机结果依赖什么

**做完你应该明白**
- 镜像基址不是每次运行重新抽：同一次开机 + 同一个文件 → 固定
- 换一个镜像文件（本次实测的是「字节相同、只改文件名」的副本）→ 基址变；「重新编译」只作为推论，本 lab 没单独测
- 系统 DLL 是共享映射，所以整个开机期间所有进程同一个基址
- 栈和堆不共享，所以逐进程随机；但大小恒定

**怎么算真的会了**
你能用 `0x140001450` 这个例子算出 RVA = `0x1450`、运行时地址 = 基址 + `0x1450`，并说出「为什么外挂写基址 + 偏移，不写死绝对地址」。

---

### 自测 2：5000 次 syscall 读出 `kernel 0.000 ms`，是不是说明系统调用不要钱？

**考点**：测量分辨率与量化噪声

**做完你应该明白**
- 这台机器的时钟粒度是 15.625 ms，5000 对只花 2.332 ms，不到一格
- 放大到 160 万对，同样的操作读出 `kernel 750 ms`
- 「tick」列会 ±1 跳动（40 万批 user 3 tick，160 万批 user 2 tick），这是量化噪声

**怎么算真的会了**
你能算出 750 ms ÷ 1,600,000 ≈ 0.469 µs/pair，并解释「读到 0 只说明小于一格」。

---

### 自测 3：从 `TEB+0x08` / `TEB+0x10` 读出 main 栈 24 KB、worker 栈 12 KB。这就是「栈的大小」吗？

**考点**：提交（commit）与保留（reserve）、栈的按需增长

**做完你应该明白**
- `StackBase - StackLimit` 是**当时已提交**的栈量，不是 `CreateThread` 的 reserve 参数（默认 1 MB）
- 同一个 run 里：`[A]` 时 A4 所在 region 只有 4 KB，`[B]` 时主栈 region 已 32 KB
- `PAGE_GUARD` 页是栈增长的开关：碰到它，内核提交下一页并把守卫页下移

**怎么算真的会了**
你能解释「为什么递归 10000 层会 `STATUS_STACK_OVERFLOW`」，以及「为什么把线程栈 reserve 改大只是改上限，不会立刻占内存」。

## 坑（都是真踩过的）

1. **15.625 ms 的量化**：`GetProcessTimes` 的 user/kernel 只按节拍跳。5000 次读成 0 不是免费，是分辨率不够；本 lab 用「放大批次 + tick 列」把这件事显式暴露出来。
2. **`MEM_FREE` 是 `State` 的值，不是 `Type` 的值**：free region 的 `Type` 是 0。第一版按 Type 统计，`MEM_FREE` 读成 0，改按 State 才对（26）。
3. **别把某一刻的 region size 当「栈大小」**：`[A]` 里 A4 的 region 是 4 KB，`[B]` 里主栈 region 是 32 KB，`StackBase-StackLimit` 又是 24 KB——三个数都对，问的是三件事（页 / 区域 / 已提交范围）。
4. **stdout 重定向后 `GetFileType` 不是 `CHAR`**：`run.ps1` 把 stdout 写进文件，所以 F4 是 `FILE_TYPE_DISK`；控制台单独用 `CONOUT$` 演示（`FILE_TYPE_CHAR`）。
5. **句柄数是快照**：`C3 = 56`，而 `handles.txt` 里同一进程（在 `--hold` 里）抓到的 `Get-Process` 是 `Handles=59`。差 3 = 之后创建的 2 个事件 + 1 个工作线程。
6. **线程数 ≠ 你的代码创建数**：`C4` 在我们还没 `CreateThread` 时就有 2 个线程，多出来的那个不是我们的。
7. **`--hold` 是给「另一个窗口」看的**：`Start-Process` 带 `-PassThru` 时，Windows PowerShell 5.1 读 `ExitCode` 会是空的（实测 `cmd /c exit 7` 也读不到），所以退出码以程序自己打印的 `exit code 0` 为准。
8. **重新编译后 exe 哈希会变**：`-g` 会把源文件路径写进调试信息，所以 `evidence/aslr.txt` 里的 sha256 只对应录制那次构建。ASLR 的对照（同文件 vs 复制件）在你机器上重跑 `run.ps1` 依然成立。

## 进阶

- 把 `run.ps1` 的复制件那一步去掉，改成「重新编译一次再跑」，看 A1 变不变（预测：变，因为文件变了）
- 在同一台机器上重启前后各跑一次，记录 A1/A5/A6，验证「重启后镜像基址是否重新随机」——这是本 lab 没有覆盖的部分
- 在 `[D]` 前后各调一次 `GetProcessHandleCount`，亲手验证 56 → 59 的那 3 个句柄
- 把 `worker_proc` 里的局部数组开大（比如 64 KB），再用 `[B]` 的走法看 worker 栈 region 怎么长
- 把 `[E]` 的 `CreateEventW` 换成一个真的 `CreateFileW` + `CloseHandle`，比较「内核对象种类」对单次成本的影响
