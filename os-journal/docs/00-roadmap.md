# 学习路线：12 周（支撑线）

> 一句话：12 周后，你能指着一个变慢的 Windows 程序说清「时间花在用户态、内核态还是硬件上」，并用手边的工具证明它——同时能说清「跨进程读写为什么会失败」和「反作弊在哪个层面看得见我」。

这是三仓库里的**支撑线**。主线是 [`reversing-journal`](https://github.com/WMWWZS/reversing-journal)，另一条支撑线是 [`network-journal`](https://github.com/WMWWZS/network-journal)。

定位说明：这份路线本来只服务「性能与故障分析」。现在加了一层用途——为逆向提供三样东西：**能安全跨进程读写**、**看得懂 PE 与 DLL 加载**、**知道检测面在哪**。原有六个阶段不变，每阶段多一列「对逆向的用处」。

## 是什么 / 为什么重要

这是一份「从会调 API 到能解释系统行为」的施工图，不是课程表：

| 它是什么 | 它不是什么 |
|:---:|---|
| 12 周、6 个阶段、每阶段 2 周的线性路线 | 一份「操作系统全部知识点」的清单 |
| 每个阶段都有可编译的实验或可复现的计数器 | 一堆书单和视频链接 |
| 用「读完必须能答」检验，而不是用「读完几章」检验 | 以页数/时长为进度的打卡表 |

为什么需要路线：操作系统的知识是**网状**的（缺页牵扯到页表、文件、调度、锁），按书目录线性读，读到一半就会同时挂着十几个没验证的概念。这条路线的做法是：**每 2 周只打通一层，每层都用本机数字收尾**，让「懂了」有可验证的形态。

## 能力模型

操作系统的地基是四块，缺一块都会卡住：

| 模块 | 必须掌握 | 为什么分析需要 | 对逆向的用处 |
|:---:|---|---|---|
| **边界与系统调用** | 用户态/内核态切换、syscall 表、参数与返回值传递 | 性能与故障分析的起点 | 所有内存读写最终都是一次 syscall；hook 的代价要在这个尺度上算 |
| **进程与内存** | PCB/EPROCESS、上下文切换、四级页表、缺页异常 | 解释卡顿、崩溃与内存增长 | `OpenProcess` 为什么被拒、`ReadProcessMemory` 怎么分页、CR3 是什么 |
| **文件与 I/O** | VFS/inode/page cache、中断与 DMA、I/O 模型 | 解释读写放大与阻塞 | PE 是文件格式，DLL 加载走的是文件 + 映射路径 |
| **并发与同步** | 原子操作、锁、内存序、死锁四条件 | 解释竞态与性能抖动 | 注入后在自己的线程里跑代码，怎么不死锁 |

```mermaid
flowchart LR
    A["边界与系统调用<br>syscall · SSDT · 中断"] --> E["可观测的系统直觉<br>现象 → 层次 → 证据"]
    B["进程与内存<br>EPROCESS · 页表 · 缺页"] --> E
    C["文件与 I/O<br>VFS · page cache · DMA"] --> E
    D["并发与同步<br>原子 · 锁 · 内存序"] --> E
    E --> F["定位瓶颈<br>CPU / 内存 / I/O / 锁"]
    E --> G["跨进程读写与检测面<br>句柄 · 页表 · 回调"]
```

推荐顺序：**边界 → 进程与内存 → 文件与 I/O → 并发 → 观测**。观测放最后不是因为它不重要，而是因为**你得先知道该看什么**，工具才有意义。

## 分周计划

12 周 = 6 个阶段，每阶段 2 周：

| 阶段 | 周次 | 主题 | 产出 | 与主线的对接点 |
|:---:|:---:|---|---|---|
| 1 | 1–2 | 内核边界与系统调用（含中断/异常） | [`labs/01-syscall-cost`](../labs/01-syscall-cost/) | 无（打底） |
| 2 | 3–4 | 进程与线程 + 调度 | [`labs/02-address-space-probe`](../labs/02-address-space-probe/) | **R5 之前必须过**（注入与线程） |
| 3 | 5–6 | 虚拟内存与物理内存 | 缺页与内存增长分析 | **R4 之前必须过**（跨进程读写） |
| 4 | 7–8 | 文件系统 + I/O 与设备 | 一次读路径的分层计时 | R3 的补充（PE 与 DLL 加载） |
| 5 | 9–10 | 同步与并发 + IPC | 锁竞争与竞态实验 | R5 的补充（自己的线程怎么活） |
| 6 | 11–12 | 内核观测与综合实验 | 一份完整的瓶颈定位报告 | R8 的前置（检测面） |

### 第 1–2 周：内核边界与系统调用（含中断/异常）

- 用户态 / 内核态、特权级、`syscall` 指令
- SSDT 与 ntdll stub：一次 `CreateFile` 到底走了多少层
- 中断、异常、IRQL 的区别（三个概念经常被混为一谈）
- 用 QPC 实测：syscall 往返、文件 I/O、线程创建、缺页

**产出**：[`labs/01-syscall-cost`](../labs/01-syscall-cost/) —— 一张全部来自本机实测的开销表。

**读完必须能答**：为什么一个「空」syscall 也要几十纳秒？为什么 `NtQuerySystemTime` 只要 2 ns？

**对逆向的补充**：`ReadProcessMemory` 本质是 `NtReadVirtualMemory` 的一次 syscall；hook 一个函数所付出的代价，要和「一次 syscall ≈ 70 ns」这个基准比才有意义。

### 第 3–4 周：进程与线程 + 调度

- PCB / EPROCESS / KTHREAD / TEB 各存什么
- `CreateProcess` 与 `fork + exec` 的路径差异
- 上下文切换到底保存了什么
- CFS 的 vruntime；Windows 的 32 级优先级与时间片

**产出**：[`labs/02-address-space-probe`](../labs/02-address-space-probe/) —— 地址空间分区、ASLR 三条实测结论、TEB 里的线程栈、句柄表、syscall 的内核时间；再用 `Get-Process` + 计数器解释一次「线程变多但 CPU 没满」的现象。

**读完必须能答**：上下文切换 2 µs 的代价由哪些部分组成？为什么线程不是「更轻的进程」这么简单？

**对逆向的补充（本阶段必须多做两件事）**：

1. **句柄权限**：用 `OpenProcess` 分别请求 `PROCESS_QUERY_INFORMATION` / `PROCESS_VM_READ` / `PROCESS_VM_WRITE` / `PROCESS_CREATE_THREAD`，记录每种组合在什么情况下返回 `ERROR_ACCESS_DENIED`。这是外部读写最先撞上的墙。
2. **模块枚举失败的含义**：`Process.Modules` 抛异常和返回空列表是两件不同的事；前者通常意味着权限不够或目标有保护。

### 第 5–6 周：虚拟内存与物理内存

- 四级页表、PTE 位、TLB
- 缺页异常的三条路径：软缺页 / 硬缺页 / 写时复制
- `VirtualAlloc` 的 reserve / commit / fault 三态
- buddy allocator、slab/pool、工作集与页回收

**产出**：解释「进程显示占用 500 MB，但物理内存只多了 50 MB」这类问题。

**读完必须能答**：commit 和 resident 差在哪？为什么首次触碰一页要 ~400 ns？

**对逆向的补充（这条是本仓库与逆向最直接的接口）**：

- **`ReadProcessMemory` 的失败不是原子的**：它可能读到一半才失败，返回值是「读了多少字节」。所以跨页读必须自己按页边界切分，并逐段校验。
- **VAD（虚拟地址描述符）**：`VirtualQueryEx` 返回的 `MEMORY_BASIC_INFORMATION` 就是 VAD 树的一次查询结果；`MEM_COMMIT` / `MEM_RESERVE` / `MEM_FREE` 与 `PAGE_*` 保护标志决定了这块内存能不能读、能不能执行。
- **CR3**：每个进程一个页目录基址。这也是为什么 DMA 路线（绕过进程、直接读物理内存）在检测上更难被发现——它不产生任何句柄。

### 第 7–8 周：文件系统 + I/O 与设备

- VFS 四件套（superblock/inode/dentry/file）与 Windows 的 FILE_OBJECT
- NTFS 的 MFT、ext4 的 extent 对照
- page cache / Cache Manager 与读写放大
- 中断、DMA、IRP 栈；阻塞 / 非阻塞 / 多路复用 / 异步

**产出**：把一次文件读的时间拆到「cache 命中 / 缺页 / 磁盘」三层。

**读完必须能答**：`WriteFile` 返回成功，数据一定落盘了吗？

**对逆向的补充**：PE 文件被加载时走的是「文件 → 映射 → 重定位 → 解析导入表」这条路径。理解了这一层，才能解释三个现象：为什么改 IAT 槽不需要改代码字节、为什么某些保护会在映射完成后重新校验磁盘上的原始文件、为什么 `LoadLibrary` 会拿 loader lock。

### 第 9–10 周：同步与并发 + IPC

- 原子指令、`lock` 前缀、内存序
- 自旋锁 / 互斥量 / 临界区 / SRWLock 的适用边界
- 死锁四条件与破坏方法
- 管道、共享内存、socket、命名对象；Windows 没有 POSIX signal

**产出**：一个能稳定演示「丢失更新」的程序，以及三种原语的竞争成本对比。

**读完必须能答**：`volatile` 为什么不是同步原语？

**对逆向的补充（本阶段最值钱的一条）**：

> **`SetWindowPos`、`SendMessage`、`UpdateLayeredWindow` 这类调用是同步的：它们会给目标窗口所属的线程发消息并等回复。如果你在自己创建的线程里去操作一个属于另一个线程的窗口，而那个线程从不泵消息，就会永久阻塞。**

这不是理论。主线仓库里有一份完整的踩坑记录（R5 的参考案例）：覆盖层窗口在 A 线程创建、绘制线程去 `SetWindowPos`，绘制线程卡在第一次迭代，日志停在 `iter 1` 之后再无输出；最后靠「另一个程序做同样的事却不卡，因为它的主线程在泵消息」确认。那份记录是别人跑通的，你学到 R5 时对着看；现在的任务只是记住这条结论。

**修复原则**：窗口的创建、位移、绘制、消息泵必须在同一个线程里。

### 第 11–12 周：内核观测与综合实验

- ETW：provider / session / consumer
- `wpr.exe`、`typeperf.exe`、`Get-Counter` 的真实用法与限制
- Windows 上不装第三方工具能做到什么
- Linux 对照：`strace` / `perf` / eBPF（本机无法执行，只作概念对照）

**产出**：一份「现象 → 假设 → 计数器 → 结论」的完整报告。

**读完必须能答**：拿到四个数字（用户态时间、内核态时间、缺页次数、上下文切换次数），怎么判断它在等锁、等 I/O 还是换页？

**对逆向的补充（R8 的入口）**：反作弊也是一个「观测系统」。它看得见的东西是有限的几类：进程/模块枚举、句柄请求记录、内存页的可执行性变化、代码段哈希、硬件断点寄存器、时间差。**你能说出这几类分别由哪个内核机制提供数据，就能说出怎么让它们看不见。**

## 动手验证

路线图不是用来读的，第一天就要跑起来。三件事，10 分钟内做完：

```powershell
# 1. 环境自检：确认本机有哪些工具、哪些没有
powershell -ExecutionPolicy Bypass -File .\tools\check-env.ps1

# 2. 跑第一个实验：系统调用与内核路径成本
cd labs\01-syscall-cost
powershell -ExecutionPolicy Bypass -File .\build.ps1

# 3. 采一条整机基线：每秒多少次系统调用
Get-Counter '\System\System Calls/sec' -MaxSamples 3
```

预期看到什么：

| 步骤 | 预期 |
|:---:|---|
| 1 | 8 行 `[ok]` / `[--]`，其中 `wsl` 为 `--`、`wpa/xperf` 为 `ok` |
| 2 | 五组表：memcpy 带宽、syscall 往返、文件 I/O、线程、缺页 |
| 3 | 读数在 10 万~300 万/秒之间跳动（机器越忙越高） |

如果第 2 步跑不出来，先看 [`labs/README.md`](../labs/README.md) 的环境要求，不要在工具链上耗三天。

## 机制与结构：为什么是这个顺序

三条理由，都来自踩过的坑：

1. **边界放最前**，因为后面每个概念都要用到它：缺页要进内核、文件读要进内核、加锁竞争要进内核。先量出「过一次边界 ≈ 70 ns」这个基准，后面才判断得出「这件事值不值得进内核」。
2. **内存放中间**，因为它是「性能解释力」最强的一层：卡顿、崩溃、内存增长、文件慢，追到底几乎都会碰到页表或 page cache。对逆向而言它同样是分水岭——过不了这层，跨进程读写永远只能靠碰运气。
3. **观测放最后**，因为工具只能放大你已经有的假设。没有「缺页多」这个假设，打开 ETW 只会看到几百万条事件。

## 三条学习原则

| 原则 | 落地方式 |
|:---:|---|
| **以题带练** | 每阶段先看「读完必须能答」，答不上再回去读；不从第一章开始通读 |
| **数字说话** | 任何结论都要带本机数字或计数器读数；没有数字的判断写成「待验证」 |
| **写下来** | 每条笔记必须含原始输出；模板见 [`notes/_TEMPLATE.md`](../notes/_TEMPLATE.md) |

> 12 周里最值钱的不是「读完了多少章」，而是**你手里有多少个自己能复现的数字**。

## 常见误区

1. **以为「会调 API 就等于懂内核」**。`malloc` 常常不触发系统调用（堆里还有空闲块），`read` 在 page cache 命中时一次磁盘都不碰。API 是接口，不是实现。
2. **以为背下 `EPROCESS` 的字段偏移就是懂内核**。偏移随 Windows build 变，服务号也变；该记的是「这个结构里有哪些类别的东西」，不是数字。
3. **以为工具能替代假设**。打开 ETW 录 30 秒，你会得到几百万条事件和更长的困惑；先有「我怀疑是硬缺页」，才知道该开哪个 provider、看哪个字段。
4. **以为 12 周能把书读完**。读不完。路线的目标是**建立可复用的排查动作**，不是覆盖所有章节；书是字典，不是小说。
5. **以为 Linux 那套可以直接套到 Windows**。`fork`、signal、`/proc`、`epoll` 在 Windows 上要么没有、要么名字和行为都不同；本仓库每处都会标出对照关系。
6. **（新增）以为权限失败是「权限不够」这么简单**。`ACCESS_DENIED` 至少有三种成因：完整性级别不匹配、目标进程有 `PROTECTED_PROCESS` 标志、以及 ACL 里显式拒绝。三者的验证方法完全不同，别混。

## 本机环境

这份日志的所有 Windows 实验都在同一台机器上完成。环境自检脚本在 [`tools/check-env.ps1`](../tools/check-env.ps1)：

```text
$ powershell -ExecutionPolicy Bypass -File .\tools\check-env.ps1
[ok] windows            windows 11 x64 (build 26200)
[ok] mingw-w64          gcc 14.2.0
[ok] python             3.11.0 (launcher: py)
[--] wsl                wsl.exe present, no usable distro (access denied)
[ok] etw                wpr.exe / typeperf.exe / Get-Counter
[--] procmon
[--] procexp
[--] windbg
[ok] wpa/xperf          installed (Windows Performance Toolkit)

page size          : 4096 bytes
logical processors : 20
available memory   : 1,328 MB
```

| 有 | 没有 / 不可用 |
|:---:|---|
| Windows 11 x64（build 26200）、MinGW-w64 gcc 14.2、Python 3.11（用 `py` 启动） | 可用的 WSL 发行版（`wsl.exe` 存在但 `wsl --list` 返回 `E_ACCESSDENIED`） |
| `wpr.exe`、`typeperf.exe`、`Get-Counter`、`logman.exe` | Procmon、Process Explorer、WinDbg |
| WPT（`wpa.exe` / `xperf.exe`，在 `E:\Windows Kits\10\Windows Performance Toolkit`） | `strace`、`perf`、eBPF、`/proc` |

三个必须说清的细节：

1. **`wsl.exe` 在，但没有可用发行版**。所以所有 Linux 命令都标注「本机不可执行」，只作概念对照；想跑 xv6 那类实验得另找 Linux 机器。
2. **WPA/xperf 装了，但 `.etl` 分析是 GUI 流程**，本仓库的命令行演示只用到「录得下来」为止，不要求你会用 WPA 界面。
3. **`python` 是 Microsoft Store 的占位程序**，直接敲会失败；装好的解释器用 `py`。

> 凡是没有标注「Linux 示例」的命令与数字，都是本机真实跑出来的；凡标注了的，都没有在本机执行过。

## 节奏建议

| 时段 | 做什么 | 说明 |
|:---:|---|---|
| 30 min | 读 | 只读当天要动手的那一节，不要通读 |
| 60 min | 做 | 跑 lab、改参数、记数字；**必须动手** |
| 15 min | 写 | 在 [`notes/`](../notes/) 留一条：现象、证据、结论 |

**每周只做一件事**：把本周的「读完必须能答」用嘴讲一遍。讲不顺的地方，就是下周要补的地方。

## 检验标准（自测 4 题）

学完不等于会了。用这四个问题自检，答不上来就是没到：

| # | 问题 | 对应能力 |
|:---:|---|---|
| 1 | 给你一次「读文件很慢」，你能沿着 page cache → 文件系统 → 块层 → 设备，指出时间花在哪一层吗？ | 文件与 I/O |
| 2 | 给你一个进程的四个数字（用户态时间、内核态时间、缺页次数、上下文切换次数），你能判断它在等锁、等 I/O、还是在换页吗？ | 进程与内存 |
| 3 | 给你一次偶发卡顿，你能用观测工具证明瓶颈是 CPU、内存还是锁竞争吗？ | 观测与综合 |
| 4 | 给你一个「跨进程读取失败」的现象，你能列出三种成因并分别给出验证方法吗？ | 逆向支撑 |

> 检验方式：合上电脑，用嘴讲。讲得出「为什么会这样」才算懂，只报得出工具名不算。

## 相关

- 第一站：[`01 · 内核边界`](./01-kernel-boundary.md)
- 第一个实验：[`labs/01-syscall-cost`](../labs/01-syscall-cost/)
- 第二个实验：[`labs/02-address-space-probe`](../labs/02-address-space-probe/)
- 记录模板：[`notes/_TEMPLATE.md`](../notes/_TEMPLATE.md)
- 任务清单：[`TASKS.md`](../TASKS.md)
- 主线：[reversing-journal](https://github.com/WMWWZS/reversing-journal) · 另一条支撑线：[network-journal](https://github.com/WMWWZS/network-journal)
