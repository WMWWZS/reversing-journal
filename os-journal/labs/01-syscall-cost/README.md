# Lab 01 · 系统调用与内核路径的真实成本

对应笔记：[docs/01-kernel-boundary.md](../../docs/01-kernel-boundary.md)、[docs/04-virtual-memory.md](../../docs/04-virtual-memory.md)

## 目的

1. 把「一次系统调用」「一次线程切换」「一次缺页」从形容词变成**可复现的数字**
2. 学会用 `QueryPerformanceCounter` 做微基准，并知道它的分辨率上限（本机 100 ns）
3. 学会**防优化**：让 `-O2` 没法把测量循环删掉
4. 亲手验证一个大坑：**名字带 `Nt` 的函数不一定真的进内核**

## 文件

| 文件 | 作用 |
|:---:|---|
| `syscall_cost.c` | 全部五组测量，单文件、无第三方依赖 |
| `build.ps1` | 一键编译（`gcc -O2`）并运行 |
| `README.md` | 你正在看的这份 |

编译产物 `syscall_cost.exe` 已被 `.gitignore` 忽略，**不提交二进制**。

## 构建与运行

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

脚本只做两件事：`gcc -O2 -std=c11 -Wall -Wextra -o syscall_cost.exe syscall_cost.c`，然后运行它。运行时间约 1–3 秒。

## 实测环境

| 项 | 值 |
|:---:|---|
| OS | Windows 11 x64 |
| 编译器 | MinGW-w64 gcc 14.2.0 |
| QPC 频率 | 10,000,000 Hz（1 tick = 100 ns） |
| 页大小 | 4096 字节 |
| 逻辑处理器 | 20 |

> 数字随机器状态变化。**绝对值只对这台机器有意义，量级和比例才是可迁移的结论。**

## 真实输出

下面是本机一次完整运行的原样输出（不是理论值，不是编的）：

```text
======================================================================
 Lab 01 - syscall & kernel path cost
 QPC frequency      : 10000000 Hz
 page size          : 4096 bytes
 logical processors : 20
 resolution floor   : 100 ns (1 QPC tick)
======================================================================

[1] memcpy bandwidth (dst <- src)
  size       iters      best ms    median ms       GB/s
  ------------------------------------------------------------------
  1 MiB        400        0.038        0.042      27.24
  4 MiB        200        0.191        0.247      21.97
  16 MiB        60        1.476        2.397      11.37
  64 MiB        20        8.127        9.331       8.26

[2] how expensive is crossing the kernel boundary?
  function                       first 6 bytes classification
  ------------------------------------------------------------------
  NtQuerySystemTime              e9 9b 4a f8 ff 66   jmp rel32 -> NOT a syscall
  NtQuerySystemInformation       4c 8b d1 b8 36 00   contains 0f 05 -> real syscall
  GetSystemTimeAsFileTime        48 ff 25 a9 b2 05   jmp [rip+..] thunk, user-mode

  batched timing: 2000 calls/sample, 100 samples, 5 repeats
  operation                        batches   best ns/call    median ns
  ------------------------------------------------------------------
  NtQuerySystemInformation             100           70.4         77.8
  NtQuerySystemTime                    100            1.8          2.0
  GetSystemTimeAsFileTime              100            1.6          1.7
  -> real syscall is about 44 x the user-mode call (70 ns vs 2 ns)
  note: NtQuerySystemTime shows ~2 ns, so it never enters the kernel on this
        build. 'Nt' prefix is not proof of a syscall; check the stub bytes.

[3] file I/O per call (page cache enabled, size matters)
  operation              size       iters      best us    median us       MB/s
  ------------------------------------------------------------------
  WriteFile              4 KiB       2000        1.700        2.200     2409.4
  ReadFile               4 KiB       2000        2.300        2.800     1780.9
  WriteFile              64 KiB       500       10.500       15.200     6241.5
  ReadFile               64 KiB       500        8.700       13.200     7532.9
  WriteFile              1 MiB         64      210.400      300.650     4983.7
  ReadFile               1 MiB         64      140.500      167.400     7463.2
  CreateFileW+CloseHandle -           2000       12.400       18.600          -

  note: the file is already in the page cache, so this is 'syscall + memcpy',
        not disk latency. A cold first read is an order of magnitude slower.
[4a] CreateThread + wait + CloseHandle
  operation                        iters      best us    median us
  ------------------------------------------------------------------
  create+join thread                 300       37.700       59.400

[4b] context switch round trip (event ping-pong, 2 threads)
  metric                          rounds      best us    median us
  ------------------------------------------------------------------
  round trip (2 switches)           2000        4.277            -
  per switch (round trip / 2)       2000        2.138            -
  note: one round trip = 2 x SetEvent + 2 x Wait + 2 x thread switch;
        dividing by 2 attributes the cost to one switch, it is not a pure
        'switch instruction' measurement.

[5] VirtualAlloc: commit / first touch / already touched
  operation                                 pages      ns/page
  ------------------------------------------------------------------
  commit all + first touch (fault)          16384        394.9
  second touch (already resident)           16384         15.6
  MEM_COMMIT per page (reserve first)       16384        413.7
  first touch after per-page commit         16384        454.2
  -> page faults are about 96% of first-touch cost, i.e. 379 ns / 4 KiB

checksum = 11858951905344743649  (all loops feed it, so nothing was optimised away)
```

## 怎么读这张表

### [1] memcpy 带宽：先建立「内存有多快」的基准

| 块大小 | 实测带宽 | 说明 |
|:---:|---:|---|
| 1 MiB | 27.2 GB/s | 基本在 L2/L3 里，最快 |
| 4 MiB | 22.0 GB/s | 开始部分走内存 |
| 16 MiB | 11.4 GB/s | 超出缓存，带宽下降 |
| 64 MiB | 8.3 GB/s | 稳定在 DRAM 带宽附近 |

后面所有「µs」都要拿它对照：**读 1 MiB 花 140 µs，而 memcpy 1 MiB 只要 ~37 µs**，说明那个 140 µs 里有一大半不是拷贝。

### [2] 过内核边界：70 ns vs 2 ns

- `NtQuerySystemInformation` 是真 syscall stub（字节里有 `0f 05`），一次 **70 ns**。
- `NtQuerySystemTime` 的 stub 是 `e9` 开头的 `jmp rel32`，被转发到用户态实现，只要 **2 ns**——**它根本没进内核**。
- 这条输出本身就是本 lab 最有价值的一课：**判断是不是 syscall 要看字节，不要看名字。**

为什么用「批量计时」：QPC 只有 100 ns 分辨率，单次 70 ns 的调用会被量化成 0 或 100 ns；一次量 2000 次再除，分辨率变成 0.05 ns。

### [3] 文件 I/O：缓存命中时「读文件」就是 memcpy

- 4 KiB 写 **1.7 µs**、读 **2.3 µs**——这是 `syscall + 内存拷贝`，不是磁盘。
- 1 MiB 读 **140 µs**，折合 7.5 GB/s，已经接近 memcpy 带宽。
- **打开 + 关闭 12.4 µs**，比读 4 KiB 还贵：路径解析、安全检查、建 `FILE_OBJECT` 才是大头。

### [4] 线程：创建比切换贵一个数量级

- 创建 + 等结束：**37.7 µs**
- 事件往返（2 次切换 + 2 次事件操作）：**4.28 µs**，摊到单次切换 **2.14 µs**

结论：**线程池复用线程能省掉 38 µs 的创建成本**，这是线程池存在的定量理由。

### [5] 缺页：395 ns/页，其中 96% 是缺页处理

| 操作 | ns/页 | 含义 |
|:---:|---:|---|
| 提交后首次触碰 | 394.9 | 缺页 + 零页映射 |
| 第二次写 | 15.6 | 已驻留，普通写内存 |
| 逐页 `MEM_COMMIT` | 413.7 | 提交本身也是 syscall |
| 逐页提交后首次触碰 | 454.2 | 提交 ≠ 驻留 |

**commit 不等于 resident**，这就是任务管理器里「提交 2 GB、工作集 200 MB」的来源。

## 自测题

> 每题写清三件事：**考点** · **做完你应该明白什么** · **怎么算真的会了**。

### 自测 1：为什么 `NtQuerySystemTime` 只要 1.8 ns？

**考点**：ntdll stub 的形态与转发

**做完你应该明白**
- 名字带 `Nt` 不代表跨内核边界
- 判断依据是 stub 里有没有 `0f 05`（`syscall`）指令
- Windows 会把部分「系统调用」在 ntdll 里转发到用户态实现

**怎么算真的会了**
你能说出「它是 `jmp rel32`，跳到用户态实现，读的是共享页」，而不是「因为它简单所以快」。

---

### 自测 2：为什么 [2] 要一次调用 2000 次再除，而 [1] 不用？

**考点**：计时器分辨率与信噪比

**做完你应该明白**
- QPC 的 1 tick = 100 ns，单次 2 ns 的调用测出来是 0
- 批量之后，误差被平摊到每次调用上
- [1] 的单次操作本身就有几十 µs，不需要批量

**怎么算真的会了**
你能算出「2000 次批量 + 100 ns 分辨率 = 每次 0.05 ns 分辨率」这个账。

---

### 自测 3：把 `memcpy` 的循环体注释掉，输出会怎样？为什么不能直接信编译器？

**考点**：死代码消除

**做完你应该明白**
- 如果结果没人用，`-O2` 会把整个循环删掉，测出「0 ms」
- 本 lab 用 `g_sink += dst[...]` 读回结果，强制编译器保留拷贝
- 微基准的第一原则：**先确认你测的代码真的被执行了**

**怎么算真的会了**
你能指出 `volatile` / 全局汇聚变量 / `asm volatile` 三种防优化手段的差别。

---

### 自测 4：为什么「提交 64 MiB」和「触碰 64 MiB」的耗时差这么多？

**考点**：reserve / commit / resident 三态

**做完你应该明白**
- `MEM_COMMIT` 只改 VAD 和 PTE 状态，不分配物理页
- 首次写触发缺页，内核才给零页并更新工作集
- 96% 的首次触碰成本是缺页处理

**怎么算真的会了**
你能解释「为什么进程刚启动时每秒几万次缺页是正常的」。

---

### 自测 5：把 [4b] 的 `rounds` 从 2000 改成 200000，数字会变吗？

**考点**：测量窗口与噪声

**做完你应该明白**
- 窗口变长，调度抖动被平均，`best` 会更稳定
- 但 20 万次往返要跑约 1 秒，期间可能被别的进程干扰
- 「取 min」是在拿「最理想情况」，不是「平均值」

**怎么算真的会了**
你能说清 `best` 和 `median` 分别回答什么问题，以及为什么本 lab 两个都打印。

## 进阶

- 把 `[3]` 的文件打开标志改成 `FILE_FLAG_NO_BUFFERING`，对比缓存命中与直写设备的差距（注意缓冲区要按扇区对齐）
- 把 `[5]` 的 `pages` 改成 65536（256 MiB），看缺页成本是否线性
- 在 `[2]` 里加一个 `NtQueryInformationProcess`（syscall 号 `0x19`），对比两个真 syscall 的成本
- 用 `Get-Counter '\System\Context Switches/sec'` 在跑 `[4b]` 时采样，看整机切换数是否被推高
