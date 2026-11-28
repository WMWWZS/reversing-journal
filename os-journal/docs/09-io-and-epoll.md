# 09 · I/O 模型：阻塞、多路复用、epoll 与 IOCP

> 一句话：一次 I/O 慢，可能慢在等设备、慢在拷数据、也可能慢在你根本不知道它在等；I/O 模型解决的是「等的时候线程去哪儿」。

## 是什么 / 为什么重要

I/O 的物理本质只有两步：**发起请求** 和 **等待完成**。不同模型只是在这两步上做不同选择：

| 模型 | 发起后线程状态 | 谁来搬数据 | 典型接口 |
|:---:|---|---|---|
| 阻塞 I/O | 睡在系统调用里 | 内核 | `read()` / `ReadFile` |
| 非阻塞 I/O | 立刻返回 `EAGAIN`/`ERROR_NO_DATA` | 应用自己轮询 | `O_NONBLOCK` / 管道 `PIPE_NOWAIT` |
| I/O 多路复用 | 睡在「等就绪」上 | 内核 | `select` / `poll` / `epoll` / `IOCP` |
| 信号驱动 | 注册回调，继续跑 | 内核 | `SIGIO`（Linux）、`WSAEventSelect` |
| 异步 I/O | 立刻返回，完成时通知 | 内核 | `io_uring`（Linux）、`IOCP`（Windows） |

为什么重要：**一台服务器能扛多少连接，几乎完全由 I/O 模型决定**——阻塞 + 每连接一线程，1 万连接就是 1 万线程；多路复用 + 事件循环，1 万连接只要几个线程。

## 机制与结构

### 从设备到内存：中断与 DMA

```mermaid
flowchart LR
    A["应用 read"] --> B["内核: 构造 IRP/IO 请求"]
    B --> C["驱动: 把 DMA 描述符交给设备"]
    C --> D["设备: 直接写内存（DMA）"]
    D -->|"MSI-X 中断"| E["ISR: 极短，只确认"]
    E --> F["DPC / 完成例程: 收尾"]
    F --> G["唤醒等待的线程"]
    G --> H["数据已在页缓存，memcpy 给用户"]
```

两个关键角色：

| 机制 | 作用 | 代价 |
|:---:|---|---|
| **DMA** | 设备直接读写内存，CPU 不参与搬运 | 配置描述符、缓存一致性 |
| **中断** | 设备通知 CPU「我完成了」 | 上下文切换、可能打断关键路径 |
| **轮询（polling）** | 高负载时不进中断，主动查状态 | 空闲时浪费 CPU；NVMe 的高 IOPS 靠它 |

如果每个 4 KB 块都中断一次，高 IOPS 设备会被中断淹没。所以现代驱动用**中断合并（coalescing）**：攒一批完成再报一次。

### Windows 的设备栈与 IRP

一次 `ReadFile` 在内核里是一张 **IRP（I/O Request Packet）** 沿着设备栈往下传：

```text
应用 ReadFile
  → I/O 管理器: IoBuildDeviceIoControlRequest / NtReadFile
    → 文件系统驱动（NTFS）: IRP_MJ_READ，查 cache
      → 卷驱动 → 磁盘驱动: 下发 DMA
        ← 完成例程逐层返回
  ← 用户态: 阻塞的线程被唤醒 / 完成端口收到包
```

每一层都可以「不立刻完成」：把 IRP 挂起，等设备完成后再调 `IoCompleteRequest`。这就是异步 I/O 的内核基础。

### 多路复用：epoll vs IOCP

| 维度 | Linux epoll | Windows IOCP |
|:---:|---|---|
| 模型 | **就绪通知**（readable/writable） | **完成通知**（操作已完成） |
| 注册 | `epoll_ctl(ADD)` 注册 fd | `CreateIoCompletionPort` 绑定句柄 |
| 等待 | `epoll_wait` 返回就绪 fd 列表 | `GetQueuedCompletionStatus` 取完成包 |
| 数据拷贝 | 应用自己 `read()` 再拷一次 | 内核直接写入应用缓冲区（overlapped） |
| 线程模型 | 通常单/少线程事件循环 | 线程池 + 完成端口，天然多线程 |
| 适用 | socket 为主 | 文件 + socket + 设备都统一 |

一句话区别：**epoll 告诉你「可以读了」，IOCP 告诉你「已经读完了」。** 前者要再发一次 `read`，后者数据已经在你的缓冲区里。

### IOCP 的最小可用形态

```c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

int main(void) {
    const DWORD size = 4096;
    HANDLE file = CreateFileW(L"iocp_demo.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

    /* 把文件句柄绑定到完成端口；key 用来区分是哪个句柄完成 */
    CreateIoCompletionPort(file, iocp, (ULONG_PTR)1, 0);

    char *buf = (char *)VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    for (DWORD i = 0; i < size; i++) buf[i] = (char)('a' + (i % 26));

    OVERLAPPED ov;
    ZeroMemory(&ov, sizeof(ov));
    DWORD written = 0;
    BOOL ok = WriteFile(file, buf, size, &written, &ov);
    printf("WriteFile returned %d, GetLastError=%lu (997 = ERROR_IO_PENDING)\n",
           ok, GetLastError());

    DWORD bytes = 0;
    ULONG_PTR key = 0;
    LPOVERLAPPED pov = NULL;
    BOOL got = GetQueuedCompletionStatus(iocp, &bytes, &key, &pov, 1000);
    printf("completion: ok=%d key=%llu bytes=%lu\n",
           got, (unsigned long long)key, bytes);

    CloseHandle(file);
    CloseHandle(iocp);
    VirtualFree(buf, 0, MEM_RELEASE);
    DeleteFileW(L"iocp_demo.bin");
    return 0;
}
```

本机实测输出：

```text
WriteFile returned 0, GetLastError=997 (997 = ERROR_IO_PENDING)
completion: ok=1 key=1 bytes=4096
```

`WriteFile` 返回 `FALSE` **不是失败**——`ERROR_IO_PENDING` 表示「操作已提交，完成时会进完成端口」。这是异步 I/O 最反直觉的一点：**同步代码里 `FALSE` 代表错，异步代码里它代表「在路上」。**

## 动手验证

### 1. 缓存命中 vs 设备延迟

[`labs/01-syscall-cost`](../labs/01-syscall-cost/) 实测：读 4 KiB（page cache 命中）只要 **2.3 µs**。

同一台机器上，设备级计数器（`Get-Counter`）的实测快照：

```text
avg. disk sec/read = 0.000429
disk reads/sec     = 3.971812
disk bytes/sec     = 337,572.216224
```

`0.000429 秒` ≈ **429 µs**，是缓存命中的 **180 多倍**。注意这是**全系统平均**，不是某个进程的单次读——但它足够说明量级差。

> 结论：分析「文件慢」时，第一步永远是判断命中没命中缓存，而不是直接怀疑磁盘。

### 2. 看进程/系统的 I/O 速率

```powershell
(Get-Counter '\Process(_Total)\IO Read Bytes/sec','\Process(_Total)\IO Write Bytes/sec' -MaxSamples 1).CounterSamples |
  ForEach-Object { "{0} = {1:N0}" -f $_.Path.Split('\')[-1], $_.CookedValue }
```

本机实测快照：

```text
io read bytes/sec  = 2,642,708
io write bytes/sec = 153,693
```

这个计数器统计的是**所有进程**的读写字节数（含缓存命中），只能看趋势；要定位到进程，得用 ETW 的 `Microsoft-Windows-Kernel-File` provider。

### 3. 对比「带缓存」与「绕过缓存」

把 [`06 · 文件系统`](./06-file-system.md) 里的 `FILE_FLAG_NO_BUFFERING` 程序跑一遍，再跑一遍不带该标志的版本。预期：**绕过缓存的版本慢一个数量级**，因为每次写都要等设备。

判断是否绕过了缓存，可以同时观察：

```text
avg. disk sec/read   上升（等设备）
disk reads/sec       上升（真的下发了读）
process io bytes/sec 不变或下降
```

### 4. 检查中断/DPC 有没有成为瓶颈

```text
# 本机已装 WPT（wpa/xperf 在 E:\Windows Kits\10\Windows Performance Toolkit），xperf 录制需要管理员
xperf -on PROC_THREAD+LOADER+DPC+INTERRUPT
# 或用 wpr（本机自带）录制后分析
wpr -start CPU -filemode
wpr -stop cpu.etl
```

本机 `wpr.exe` 实测可用（`wpr -status` 输出 `WPR is not recording`），`.etl` 的分析用 WPA（本机 WPT 已装，GUI 工具）。所以命令行只能做到「录得下来」。

### 5. Linux 对照（本机不可执行）

```text
# Linux 示例：本机不可执行，原因见 docs/00 的环境一节
strace -T -e trace=read,write ./prog     # 每次 I/O 的耗时
perf trace -e 'syscalls:sys_enter_read'  # 系统调用级追踪
cat /proc/diskstats                      # 块设备统计
```

## 常见误区

1. **以为异步 I/O 一定比同步快**。单线程、单请求、数据在缓存里时，同步 `ReadFile` 的 2.3 µs 已经很低；异步反而多了一次完成端口往返。**异步的价值在并发规模，不在单次延迟。**
2. **以为 epoll 是「轮询优化」**。epoll 的核心是内核里的就绪队列 + 回调，`epoll_wait` 本身不扫全部 fd；它快在「只返回就绪的」，不是「轮询更快」。
3. **以为 `WriteFile` 返回 `FALSE` 就是失败**。带 `OVERLAPPED` 时 `ERROR_IO_PENDING` 表示异步进行中；必须等完成端口/事件，不能直接报错退出。
4. **以为顺序读一定能跑满磁盘带宽**。还要看块大小、队列深度、文件是否碎片化、是否命中 page cache；4 KB 随机读和 1 MB 顺序读能差几十倍。
5. **以为中断开销可以忽略**。高 IOPS 场景下，中断 + DPC 可能吃掉一个核；这就是 NVMe 要用轮询、驱动要做中断合并的原因。

## 自测

**1. 阻塞 I/O 和多路复用的根本区别是什么？**

- **考点**：等待的位置
- **应明白**：阻塞 I/O 让**线程**睡在单个操作上；多路复用让线程睡在「一组 fd 的就绪事件」上，一个线程管很多连接
- **算会**：能说出「1 万连接」在两种模型下各需要多少线程，以及瓶颈在哪

**2. epoll 返回可读，和 IOCP 返回完成，差在哪一步？**

- **考点**：就绪通知 vs 完成通知
- **应明白**：epoll 之后还要自己 `read()`（可能阻塞/部分读）；IOCP 的数据已经被内核写进 `OVERLAPPED` 指定的缓冲区
- **算会**：能解释为什么 IOCP 更适合「文件 + socket 统一处理」

**3. 一个程序 `IO Read Bytes/sec` 很高，但 `Avg. Disk sec/Read` 很低。说明什么？**

- **考点**：缓存命中
- **应明白**：读的量很大但设备几乎没参与，说明数据在 page cache 里；此时优化方向是减少拷贝/系统调用，不是换磁盘
- **算会**：能用两个计数器区分「缓存读」和「设备读」

## 相关

- 文件与 page cache：[`06 · 文件系统`](./06-file-system.md)
- 边界与 syscall：[`01 · 内核边界`](./01-kernel-boundary.md)
- 阻塞与调度：[`03 · 调度`](./03-scheduling.md)
- 实验：[`labs/01-syscall-cost`](../labs/01-syscall-cost/)
