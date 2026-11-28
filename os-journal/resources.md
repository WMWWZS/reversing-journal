# 资源清单

## 书

按「读得下去」排序，不是按权威性：

| 书名 | 作者 / 版本 | 读哪几章 | 说明 |
|:---:|---|---|---|
| **《Operating Systems: Three Easy Pieces》**（OSTEP） | Arpaci-Dusseau，v1.10 | 虚拟化 4–24、并发 26–34、持久化 36–46 | 免费在线，讲直觉不堆公式；本仓库的很多说法以它为准 |
| **《Computer Systems: A Programmer's Perspective》**（CSAPP） | Bryant & O'Hallaron，3rd | 第 3、6、8、9、10、12 章 | 从程序员视角讲链接/内存/异常/并发，和本仓库最贴 |
| **《Windows Internals》** | Russinovich, Ionescu, Solomon, Yosifovich，Part 1 & 2（7th ed.） | 进程与线程、内存管理、I/O 系统、文件系统 | Windows 侧的权威参考；字段偏移以它对应 build 为准 |
| **《Linux 内核设计与实现》** | Robert Love，3rd | 进程调度、系统调用、内存管理、VFS | 薄、可读完，适合建立 Linux 侧骨架 |
| **《深入理解 Linux 内核》** | Bovet & Cesati，3rd | 内存寻址、进程地址空间、I/O | 比 Love 更细，查页表和缺页时用 |
| **《Modern Operating Systems》** | Tanenbaum，4th | 进程、内存、文件系统 | 概念对照，适合补「为什么这样设计」 |
| **《Windows Kernel Programming》** | Pavel Yosifovich | 驱动基础、IRP、同步 | 想从「观测」走到「写驱动」时再读 |
| **《xv6: a simple, Unix-like teaching operating system》** | Cox, Kaashoek, Morris | 全书 | 一万行以内的完整内核，读代码比读书有用 |

> 书里写的是「某个版本」的行为。Windows 的服务号、结构体偏移、默认量子都会变——**以本机实测为准，书用来解释原理。**

## 公开课程与实验

| 课程 / 资源 | 内容 | 链接 | 本机能不能做 |
|:---:|---|---|:---:|
| **MIT 6.1810（原 6.S081）Operating System Engineering** | xv6 实验：syscall、页表、trap、COW、锁、文件系统、mmap、网卡驱动 | [pdos.csail.mit.edu/6.1810](https://pdos.csail.mit.edu/6.1810/) | ❌ 需要 Linux + QEMU；本机无可用 Linux 环境 |
| **xv6 教材（在线）** | 带注释的内核教材，配合 6.1810 | [mit-pdos.github.io/xv6-riscv-book](https://mit-pdos.github.io/xv6-riscv-book/) | ✅ 只能读，不能跑 |
| **xv6 源码** | RISC-V 版，约一万行 | [github.com/mit-pdos/xv6-riscv](https://github.com/mit-pdos/xv6-riscv) | ✅ 可克隆阅读 |
| **OSTEP 作业模拟器** | 调度、分页、TLB、并发等模拟器 | [pages.cs.wisc.edu/~remzi/OSTEP](https://pages.cs.wisc.edu/~remzi/OSTEP/) 的 Homework 一节 | ✅ 需要 Python；本机用 `py` |
| **CSAPP Labs** | Data Lab、Bomb Lab、Malloc Lab、Shell Lab 等 | [csapp.cs.cmu.edu](https://csapp.cs.cmu.edu/) | ⚠️ 多数为 Linux 环境；本机只能做纯 C 的部分 |

> 本机是 Windows 11 x64 且**没有可用的 Linux 环境**，所以 xv6 那一类「改内核 + 跑 QEMU」的实验只能另找 Linux 机器（虚拟机或云主机）。这不影响读代码，但**没跑过就不能说自己做过**。

## 观测工具

| 工具 | 平台 | 用途 | 本机状态 |
|:---:|---|---|:---:|
| `Get-Counter` | Windows（PowerShell） | 秒级采样性能计数器 | ✅ |
| `typeperf.exe` | Windows 自带 | 计数器 → CSV，适合脚本/长采样 | ✅ |
| `wpr.exe` | Windows 自带 | ETW 录制（`.etl`） | ✅ 能录 |
| `logman.exe` | Windows 自带 | 查 provider、管理 ETW session | ✅ |
| `Get-Process` / `netstat -ano` | Windows | 进程、线程、句柄、连接 | ✅ |
| 任务管理器 / 资源监视器 | Windows | 快速看总量与磁盘队列 | ✅ |
| Procmon | Sysinternals | 文件/注册表/网络事件级追踪 | ❌ 未装，可自行下载 |
| Process Explorer | Sysinternals | 句柄、线程栈、加载的 DLL | ❌ 未装 |
| WPA / xperf | Windows Performance Toolkit | 分析 `.etl`，看 CPU/DPC/中断 | ✅ 已装（WPT） |
| WinDbg | Windows SDK | `!process` / `!vm` / `!thread` / 内核调试 | ❌ 未装 |
| `strace` | Linux | 系统调用级追踪 | ❌ 本机不可执行 |
| `perf` | Linux | 采样、调度延迟、热点 | ❌ 本机不可执行 |
| eBPF / bpftrace / bcc | Linux | 内核动态追踪 | ❌ 本机不可执行 |

官方文档（都指向 Microsoft Learn，链接稳定）：

- Sysinternals 工具集：[learn.microsoft.com/sysinternals](https://learn.microsoft.com/sysinternals/)
- Procmon 下载：[learn.microsoft.com/sysinternals/downloads/procmon](https://learn.microsoft.com/sysinternals/downloads/procmon)
- Process Explorer：[learn.microsoft.com/sysinternals/downloads/process-explorer](https://learn.microsoft.com/sysinternals/downloads/process-explorer)
- Windows Performance Toolkit：[learn.microsoft.com/windows-hardware/test/wpt](https://learn.microsoft.com/windows-hardware/test/wpt/)
- ETW 文档：[learn.microsoft.com/windows/win32/etw/event-tracing-portal](https://learn.microsoft.com/windows/win32/etw/event-tracing-portal)
- 驱动开发（WDK）：[learn.microsoft.com/windows-hardware/drivers](https://learn.microsoft.com/windows-hardware/drivers/)
- WinDbg 文档：[learn.microsoft.com/windows-hardware/drivers/debugger](https://learn.microsoft.com/windows-hardware/drivers/debugger/)

Linux 侧参考（只读，本机不可执行）：

- man-pages：[man7.org/linux/man-pages](https://man7.org/linux/man-pages/)
- 内核源码：[kernel.org](https://www.kernel.org/)
- bcc 工具集：[github.com/iovisor/bcc](https://github.com/iovisor/bcc)
- Brendan Gregg 的观测资料：[brendangregg.com](https://www.brendangregg.com/)
- LWN（内核改动与设计讨论）：[lwn.net](https://lwn.net/)

## 社区

| 社区 | 说明 |
|:---:|---|
| [LWN](https://lwn.net/) | 内核新特性、调度器变更（比如 CFS → EEVDF）的一手报道 |
| [Brendan Gregg](https://www.brendangregg.com/) | 性能分析方法论与 Linux 工具地图 |
| [Microsoft Learn](https://learn.microsoft.com/) | Windows 内核/驱动/ETW 的官方文档，比博客可靠 |
| [OSDev Wiki](https://wiki.osdev.org/) | 想自己写玩具内核时的百科（注意：内容新旧不一） |

> 论坛和博客只能当线索，**结论要回到文档或本机实验**。看到「Windows 时间片是 15.6 ms」这种断言，先查它对应哪个版本。

## 安全与授权边界

这一条比上面所有资源都重要：

| 边界 | 具体要求 |
|:---:|---|
| **只碰自己的机器** | 所有内核级操作（装驱动、开内核调试、改页表、挂 ETW 内核 provider）只在自己的测试机/虚拟机上做 |
| **不做未授权观测** | 不 attach 别人的进程、不抓别人的网络、不在公司生产机上开 Driver Verifier 或内核调试 |
| **需要管理员就说清楚** | 本仓库的文档会标注「需要管理员/需要装工具」；没标注的才假定普通权限可做 |
| **内核代码风险自负** | 写驱动意味着蓝屏权限；先在虚拟机快照里试，别在主力机上直接上 |
| **数据最小化** | ETW `.etl`、Procmon 日志可能含路径、用户名、连接信息；不要上传到公开仓库 |
| **法律底线** | 遵守《中华人民共和国网络安全法》《刑法》第 285/286 条等规定；不对他人系统做侵入或破坏性测试 |

**一句话**：观测和调试的授权，来自「这台机器是我的，或者我拿到了书面许可」。越界之后，技术再好也是事故。

## 怎么用这份清单

1. 先读 OSTEP 的对应章节建立直觉（免费、快）。
2. 再读 CSAPP 的对应章节补「程序员视角」。
3. 然后跑本仓库的 lab，把数字变成自己的。
4. 卡住时查 Windows Internals / Love，**不要从第一页读**。
5. 想深入内核就上 xv6（需要 Linux 环境），不要一上来写驱动。
