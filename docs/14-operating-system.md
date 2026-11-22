# 14 · 操作系统：你双击 exe 之后，内核到底给了你什么

> 前置：13 讲栈帧。
> 这一篇不背概念。下面每一个数字都是 `labs/07-net-os/os/osprobe.exe` 在**这台机器上真跑出来**的，
> 原始输出在 `labs/07-net-os/os/evidence/run1.txt`。你跑出来的数字会和这里不一样——**不一样本身就是这节课的内容**。

## 0. 先看一个真进程的自白

```
A1 module base     GetModuleHandle(NULL)              = 0x00007ff6ec840000
A2 global variable &g_global                           = 0x00007ff6ec846000
A3 heap block      HeapAlloc(GetProcessHeap(), 0, 64)  = 0x000001cf565f13e0
A4 stack local     &local_marker                       = 0x000000b03a1ff677
A5 kernel32.dll base                                   = 0x00007ff8772c0000
A6 ntdll.dll base                                      = 0x00007ff877d90000
```

六个地址，横跨三个数量级，全都不是"物理内存"。这就是操作系统给每个进程发的第一样东西。

## 一、内核给每个进程发三样东西

| 内核发的 | 是什么 | 你在逆向里什么时候会碰到它 |
|---|---|---|
| **一张地址空间** | 一整套"假地址"，从 0 到 0x7FFFFFFFFFFF | Ghidra 的 `0x140001450`、x64dbg 的 `0x7FF7...`、`[rbp+0x10]` 全在这里面 |
| **一张句柄表** | 一张"号码 → 内核对象"的表 | 文件、管道、事件、**socket**、进程、线程，全是一个号码 |
| **至少一个线程** | 真正在 CPU 上跑的实体 | 断点停在哪个线程、为什么会有"不是你的代码"的线程 |

双击 exe 的那一刻，`CreateProcess` 做的事情就是：建地址空间 → 把 PE 映射进去 → 建句柄表 → 建主线程 → 让它开始跑。
你之后在 Ghidra/x64dbg 里看到的每一样东西，都是这三样里面的。

## 二、地址空间：四个地址，四个区

先把上面六个数字排一下（低 → 高）：

```
低地址   0x000000b03a1ff677   A4 栈上的局部变量         ← 栈：往低地址长
        0x000001cf565f13e0   A3 堆块 HeapAlloc         ← 堆：一堆 MEM_PRIVATE
        0x00007ff6ec840000   A1 exe 的基址             ← 镜像：磁盘上的文件映射进来
        0x00007ff6ec846000   A2 全局变量 g_global      ← 在镜像的 .data 段里
        0x00007ff8772c0000   A5 kernel32.dll           ← 系统 DLL，还要更高
高地址   0x00007ff877d90000   A6 ntdll.dll              ← 通常是最高的那批之一
```

**为什么能确定它们各在哪？** 用 `VirtualQuery` 问内核，它告诉你每个地址属于哪种区域：

```
A1 -> type=MEM_IMAGE    Protect=PAGE_READONLY     ← 代码段，只读
A2 -> type=MEM_IMAGE    Protect=PAGE_WRITECOPY    ← .data，写的时候才复制
A3 -> type=MEM_PRIVATE  Protect=PAGE_READWRITE    ← 堆，进程私有
A4 -> type=MEM_PRIVATE  Protect=PAGE_READWRITE    ← 栈，进程私有
```

三个关键词，看懂就够用了：

| 词 | 人话 |
|---|---|
| `MEM_IMAGE` | 这块内存**是从磁盘上的 exe/dll 文件映射来的**。所以磁盘上改 exe，内存里就变 |
| `MEM_PRIVATE` | 这块内存**只属于本进程**，和文件无关。堆、栈都是 |
| `Protect` | 这段内存**能不能读/写/执行**。`.text` 是 READONLY；想改代码得先 `VirtualProtect` |

> **对逆向的意义**：`Protect` 是很多反作弊和壳的战场。你写的"打补丁改指令"在只读页上会直接崩，
> 所以补丁工具的第一步永远是 `VirtualProtect(地址, 长度, PAGE_EXECUTE_READWRITE, &old)`。
> 反过来，检测方会去看"谁把 .text 改成可写了"。

`VirtualQuery` 走了一遍整个空间，这台机器上的结果是：

```
regions found: 103
  MEM_COMMIT=62   MEM_RESERVE=15   MEM_FREE=26
  committed by Type: MEM_IMAGE=35  MEM_MAPPED=15  MEM_PRIVATE=12
```

`MEM_IMAGE=35` 就是"1 个 exe + 34 个 DLL"。一个空壳程序也已经加载了 35 个模块——
这就是为什么真实游戏的模块列表长得吓人，也是为什么**先看模块列表**是逆向的第一步。

## 三、ASLR：为什么 Ghidra 说 `0x140001450`，x64dbg 里却在 `0x7FF7...`

这是你上节课最懵的地方，现在把它彻底解决。

`objdump -p` 看 PE 头，里面写着一个数字：

```
ImageBase  = 0x0000000140000000   ← 这是编译时"我希望被加载到哪"
```

但真跑起来，`GetModuleHandle(NULL)` 给的是 `0x00007ff6ec840000`。

**PE 头里那个数只是"申请"，实际地址是内核分配的，而且被随机化了**——这就是 ASLR
（Address Space Layout Randomization，地址空间布局随机化）。

我在同一台机器、没重启，连着跑了 3 次同一个 exe，第 4 次换了个**字节完全相同、只是文件名不同**的副本
（`os/evidence/aslr.txt` 里的原表）：

| | 跑 1 | 跑 2 | 跑 3 | 结论 |
|---|---|---|---|---|
| A1 exe 基址 | `0x00007ff6ec840000` | 同 | 同 | **不变** |
| A5 kernel32 | `0x00007ff8772c0000` | 同 | 同 | **不变** |
| A6 ntdll | `0x00007ff877d90000` | 同 | 同 | **不变** |
| A3 堆块 | `0x000001cf565f13e0` | `0x0000021b7aa91110` | `0x000001f5a1a61110` | **每次都变** |
| A4 栈局部 | `0x000000b03a1ff677` | `0x000000ac38bffda7` | `0x0000004d993ffd07` | **每次都变** |

第 4 次跑 `osprobe_copy.exe`（sha256 和原文件一模一样）：

```text
osprobe.exe        module base = 0x00007ff6ec840000
osprobe_copy.exe   module base = 0x00007ff79f940000   ← 变了
```

看出来了吗：**镜像基址 3 次一模一样，栈/堆地址 3 次全不一样。**

三条实测结论（记这三条就够了）：

1. **栈、堆**：每个进程创建时现抽随机数 → 同一个程序跑两次，`[rbp-0x8]` 的绝对地址不可能一样。
2. **镜像基址（exe/dll）**：同一次开机 + 同一个文件 → 固定不变；文件换了（重编译/改名）或重启 → 变。
3. **系统 DLL 更稳**：kernel32/ntdll 是整个开机期间所有进程**共享映射**，所以基址对所有进程都一样
   ——这就是为什么你在 x64dbg 里看 ntdll 的地址"很稳"。

### 手算一次，这个坑就再也不会踩

Ghidra 里函数在 `0x140001450`，PE 的 ImageBase 是 `0x140000000`，所以**文件内偏移（RVA）**是：

```
0x140001450 - 0x140000000 = 0x1450
```

运行时基址是 `0x7ff6ec840000`，那么它真正在内存里的地址是：

```
0x7ff6ec840000 + 0x1450 = 0x7ff6ec841450
```

**你在 x64dbg 里按 Ctrl+G 跳这个地址，就能看到 Ghidra 里同一个函数。**
（上节课你在 x64dbg 里看到的 `00007FF618FA1450` 就是这么来的：基址 + 0x1450。）

> 这就是为什么所有教程都在说"RVA/偏移"。**逆向里 99% 的地址都是偏移，不是绝对地址。**

## 四、栈是内核发的，`[rbp-0x8]` 住在哪

从 TEB（线程环境块）能直接读到当前线程栈的两个边界。x64 下 `gs` 基址指向 TEB：

```
D0  TEB self = gs:0x30 = 0x000000b039f88000   PEB = gs:0x60 = 0x000000b039f87000
D1  main   [TEB+0x08] StackBase  = 0x000000b03a200000   ← 栈底（高地址）
D2  main   [TEB+0x10] StackLimit = 0x000000b03a1fa000   ← 栈能用的最低处
```

`0xb03a200000 - 0xb03a1fa000 = 0x6000 = 24 KB`：这是**主线程当时已经提交的栈**。
栈是"按需长大"的：用多少页就提交多少页，默认最多能长到 1 MB（`CreateThread` 的 reserve 参数）。
（证据里还看到：`[A]` 段刚进 main 时栈只有 4 KB，走到 `[B]` 已经 32 KB——就是长出来的。）

**为什么 `[RBP-0x8]` 一定落在这个区间里？** 因为函数一进来就 `SUB RSP, N`，`RBP` 就是当时的 `RSP`。
所以局部变量在 `RBP` 往下，永远在 `StackLimit` 和 `StackBase` 之间。

栈再往下就是**守卫页（guard page）**，证据里也抓到了：

```
STACK  base=0x000000b03a1f8000  size=0x00008000 (32 KB)  Protect=PAGE_READWRITE
GUARD  base=0x000000b03a1f5000  size=0x00003000 (12 KB)  Protect=PAGE_READWRITE|PAGE_GUARD
```

`PAGE_GUARD` 的意思：**碰到这个页，内核立刻知道"栈要长了"**，于是把下一页提交好、守卫页往下挪一格。
如果它挪不动了（到 1MB 上限），你就得到 `STATUS_STACK_OVERFLOW`——递归太深的死法。

> 这也是栈溢出攻击的物理基础：局部数组越界写，写到返回地址，`RET` 就跳到攻击者的地址。
> 现代系统用 `/GS` cookie、DEP、CFG 拦它。你分析壳和反作弊时会天天见到这些东西。

## 五、线程：两份栈，一份代码

```
D3 worker [TEB+0x08] StackBase  = 0x000000b03a600000
D4 worker [TEB+0x10] StackLimit = 0x000000b03a5fd000   ← 12 KB
```

同一个进程里，两个线程：

- **代码是共享的**（同一张地址空间，同一个 `.text`）
- **栈是各自的**（`0xb03a200000` vs `0xb03a600000`）
- **寄存器是各自的**（CPU 切换线程 = 保存一套寄存器、装另一套，这叫上下文切换）

`Get-Process -Id <pid>` 看到的线程（`os/evidence/threads.txt`）：

```
   Id ThreadState     WaitReason StartAddress
51956        Wait ExecutionDelay            0     ← 主线程
52792        Wait   EventPairLow            0     ← 不是 osprobe 建的（见下）
42308        Wait    UserRequest            0     ← 我们 CreateThread 出来的工作线程
```

全是 `Wait`。**一个进程里绝大多数线程，绝大多数时间都在等**（等锁、等事件、等网络、等下一次调度）。
这也解释了为什么断点要"选对线程"：你断在某个线程上，别的线程还在跑，游戏画面不会卡住。

> 还有个更野的事实，证据里抓到了：`osprobe` 在 **还没调用 `CreateThread`** 的时候，
> 自己数线程就已经是 **2 个**（`C4: TIDs 51956 52792`）——多出来的 `52792` 不是它的代码建的。
> 对照实验（一个只 `Sleep` 的空程序）也有 2 个线程。
> **别的进程可以在你的进程里创建线程**（`CreateRemoteThread` / `NtCreateThreadEx`）。
> 这不是 bug，这是 DLL 注入和各种安全软件互相打架的主要战场之一。
>
> 所以记住：**"线程数"不等于"我的代码创建了几个线程"。** 分析游戏时看到一堆陌生线程，先别急着当外挂。

## 六、句柄：内核对象的号码牌

```
C3 open handles GetProcessHandleCount() = 56
```

56 是什么意思？每打开一个内核对象（文件、事件、socket、线程……），句柄表就占一格，
返回给你的那个数字就是"表里的第几格"。

同一个程序里三种不同的对象，句柄长这样：

```
F1 CreateFileW("osprobe_tmp.txt")  handle=0xd4   type=1  FILE_TYPE_DISK   文件
F2 CreatePipe()   write end        handle=0xc8   type=3  FILE_TYPE_PIPE   匿名管道
F3 CreateFileW("CONOUT$")          handle=0xe0   type=2  FILE_TYPE_CHAR   控制台
```

三个要点：

1. **句柄是号码，不是地址。** `0xb0` 这个数在另一个进程里可能指向完全不同的东西——所以句柄不能跨进程直接用。
2. **光看数字猜不出类型。** `GetFileType(0xb0)` 是在问内核"这个号码指向什么对象"。
3. **句柄会复用。** 关掉一个，下一个打开的很可能拿到同一个号——所以调试时"句柄值一样"不代表"是同一个对象"。

> 证据 F3/F4 还有个意外收获：`CONOUT$` 是 `FILE_TYPE_CHAR`，但 `GetStdHandle(STD_OUTPUT_HANDLE)`
> 这次返回 `FILE_TYPE_DISK`——因为这次运行把 stdout **重定向到文件**了。
> 这就是"重定向"的全部真相：程序里的 stdout 只是一个句柄，指向哪由启动它的人决定。

**socket 也是句柄**（下一课会用到）。你在 x64dbg 里看到 `ws2_32!send` 的第一个参数 `RCX`，
那个数字就是句柄表里的一个格子——和 `0x114` 没有本质区别，只是它指向的是 AFD 设备对象。

## 七、用户态 / 内核态：系统调用的真实代价

x64 有四个特权级，Windows 只用两个：

- **用户态（Ring 3）**：你的程序、游戏、Ghidra。不能碰硬件，不能碰别人的内存。
- **内核态（Ring 0）**：内核、驱动。什么都能干。
- 从用户态进内核态**只有一条路**：`syscall` 指令（旧的是 `int 2Eh`/`sysenter`）。

进来要付代价。真跑 160 万次 `CreateEventW + CloseHandleW`：

```
    calls    wall ms   user ms  kernel ms
  1600000    787.820    31.250    750.000
```

- `kernel 750 ms` ← 320 万次内核进出，全记在这
- `user 31 ms` ← 用户态那点代码
- 平均**每一对 0.49 微秒**，其中 0.47 微秒花在内核（约 95%）

**这是"每一次 send() 都要付的过路费"。** 所以高性能网络程序都在想办法少进内核
（批量收发、IOCP、`WSARecv` 一次收多个 buffer）。

> 小坑：只跑 5000 次时，`kernel ms` 读出来是 `0.000`。不是免费，是**这台机器的时钟粒度是 15.625 ms**
> （`GetSystemTimeAdjustment` 给的 `TimeIncrement=156250`×100ns），而 5000 次才花 2.33 ms，不够一格。
> 这就是为什么证据里要把批次放大到 160 万——**测量工具的分辨率不够时，读到 0 不等于真是 0**。

### 和 x64dbg 对上：你停的那个 ntdll 地址是什么

你第一次用 x64dbg 打开 exe，停在 `ntdll.dll` 的 `LdrpInitializeProcess`，
状态栏写"已到达系统断点"。那一刻发生了什么：

```
exe 的入口  →  ntdll!_LdrpInitializeProcess  →  加载 DLL、初始化堆、建 TEB/PEB
            →  最后跳到你的 main
```

`ntdll` 是**每个进程都加载的、专门负责"进内核"的那一层**。你的 `CreateFile` / `send` / `VirtualAlloc`，
最后都要经过它里面的 `Nt*` 函数，然后 `syscall`。

所以你以后下断点的优先级是：

1. **先断自己的函数**（有符号/有反编译时最省事）
2. **再断 API**：`ws2_32!send`、`kernel32!ReadFile`、`ntdll!NtCreateFile`
3. **永远别想"断在 syscall 里面"**——进去了就是内核，用户态调试器看不见

## 八、把上节课串起来：一次 `send()` 在 OS 里走了什么路

假设客户端要发 16 字节的 ATTACK 帧（下一课就是它）：

```
[客户端 用户态]  你写的代码：memcpy(buf, ...); send(s, buf, 16, 0)
                 ↓ 函数调用（还是用户态，栈帧就是 13 讲那个）
[客户端 用户态]  ws2_32.dll!send → mswsock → ntdll!NtDeviceIoControlFile
                 ↓ syscall 指令（Ring3 → Ring0，约 0.4 μs）
[客户端 内核态]  AFD 驱动拿到这 16 字节 → 拷进 socket 发送缓冲区 → 返回
                 ↑ 注意：这时候数据"发出去了"只是进了内核缓冲区，网卡还没动
[网卡/回环]      tcpip.sys 组包 → 走到服务端
[服务端 内核态]  16 字节进服务端 socket 的接收缓冲区
[服务端 用户态]  recv() 返回 16 字节 → 你的代码解析 → hp 减 5
```

这一路上发生了：**2 次用户态→内核态切换、2 次内核态→用户态、2 次线程调度**。
每一段都有代价，也每一段都能被观测——下一课就在网卡层面把它抓下来看。

## 九、动手（约 30 分钟）

```powershell
cd E:\harness\reversing-journal\labs\07-net-os\os
.\build.ps1          # 编译
.\osprobe.exe        # 第 1 遍，记下 A1/A3/A4
.\osprobe.exe        # 第 2 遍，对比
Copy-Item .\osprobe.exe .\osprobe_copy.exe; .\osprobe_copy.exe   # 看 A1 变不变
```

然后做这四件事（每件都能对上本文的某一节）：

1. 打开 x64dbg 加载 `osprobe.exe`，在"符号"面板看模块基址，和 `A1` 对上；再用 `Ctrl+G` 跳 `基址+0x1450` 附近的 `main`
2. `Get-Process -Id <PID> | Select Handles,Threads`
3. 在 osprobe 暂停那 2.6 秒里（输出里写着 `[HOLD]`），去任务管理器看它
4. 把 osprobe.c 的 `MAX_ITER` 从 5000 改成 2000000 重编译，看`kernel ms` 从 0 变成几百毫秒（第 7 节的"分辨率坑"）

## 自测

**1. `objdump -p` 显示 ImageBase=0x140000000，Ghidra 里函数在 0x140001450。x64dbg 里它是多少？**

- 考点：绝对地址 = 运行时基址 + RVA
- 应明白：RVA = 0x1450，运行时地址 = `GetModuleHandle(NULL)` + 0x1450
- 算会：给你一个基址 `0x7ff6ec840000`，你能立刻算出 `0x7ff6ec841450`

**2. 为什么同一个程序跑两次，`[rbp-0x8]` 的地址不一样，但函数在 exe 里的偏移永远一样？**

- 考点：ASLR 随机的是"基址"，不是"偏移"
- 应明白：栈每次新建（随机），镜像基址同一次开机内固定
- 算会：你能解释"外挂为什么要写 基址+偏移 而不是写死一个地址"

**3. 你在 x64dbg 里改了一条指令（把 `SUB` 改成 `ADD`），为什么可能直接崩？**

- 考点：`Protect` 和内存页属性
- 应明白：`.text` 是 `PAGE_EXECUTE_READ`，改之前必须 `VirtualProtect` 成可写
- 算会：你能说出"只读页写入 → 访问违例"

**4. 5000 次系统调用读出来 kernel time = 0.000 ms，是不是说明系统调用不要钱？**

- 考点：测量分辨率
- 应明白：时钟粒度 15.625 ms，批次太短读不出来；放大到 160 万次就现形（kernel 750 ms）
- 算会：你能算出每次约 0.4 μs，并解释为什么这是"测量精度"不是"事实"

**5. 一个进程有 3 个线程，为什么任务管理器有时看到 4 个？**

- 考点：线程是内核对象，别人可以往你进程里塞
- 应明白：`CreateRemoteThread`/`NtCreateThreadEx`；每个线程有自己的栈，代码共享
- 算会：你能说出"多出来的那个线程，它的栈不在你的 CreateThread 返回值里"
