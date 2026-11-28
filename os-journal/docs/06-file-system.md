# 06 · 文件系统：VFS、inode 与 page cache

> 一句话：`read()` 拿到的不是磁盘上的字节，而是「缓存里有一份就先给你」；文件系统这一层的所有性能问题，都绕不开那三个字——page cache。

## 是什么 / 为什么重要

文件系统要解决两个不同的问题：

| 问题 | 谁来答 | 典型实现 |
|:---:|---|---|
| 「这个路径对应哪个文件对象？」 | 命名层（路径解析） | VFS dentry 缓存 / Windows 对象管理器 |
| 「文件的数据块在哪、怎么读？」 | 存储层（布局与分配） | ext4 extent / NTFS MFT + runlist |

操作系统用一层**虚拟文件系统（VFS）**把两者隔开：上层只认 `open/read/write` 接口，下层可以是 ext4、NTFS、FAT32、网络文件系统。Windows 的对应抽象是对象管理器 + I/O 管理器 + 文件系统驱动。

为什么重要：**一次「读文件」可能只是内存拷贝，也可能是一次磁盘 I/O，两者差 3~4 个数量级。** 不搞清 cache 命中路径，所有「文件慢」的分析都是猜。

## 机制与结构

### VFS 的四个对象（Linux）与 Windows 的对应

| Linux | 存什么 | 生命周期 | Windows 对应 |
|:---:|---|---|---|
| `superblock` | 文件系统整体信息（块大小、空闲块） | 挂载期间 | Volume（`\Device\HarddiskVolumeN`） |
| `inode` | 一个文件的元数据 + 数据块位置 | 文件存在期间（可能缓存） | MFT 记录 + FCB |
| `dentry` | 路径名 → inode 的映射 | 目录项缓存，可回收 | 对象管理器命名空间 / 路径缓存 |
| `file` | 一次打开实例（偏移、标志） | `open` 到 `close` | `FILE_OBJECT` + CCB |

> 关键区别：Linux 的 inode 是磁盘结构 + 内存缓存；Windows 的 MFT 记录是磁盘结构，内存里对应 `FCB`（File Control Block），一次打开还会多一个 `CCB`（Context Control Block）。**别把 `FILE_OBJECT` 当 inode 用。**

### 磁盘布局：NTFS vs ext4

| 维度 | NTFS | ext4 |
|:---:|---|---|
| 元数据主表 | MFT（`$MFT`），每条记录 1 KB（常见） | inode 表，`inode_size` 可配（默认 256 B） |
| 数据块寻址 | runlist（VCN → LCN 的区间列表） | extent tree（区段树） |
| 小文件 | 数据可**驻留**在 MFT 记录里（几百字节内） | 数据在块里，inode 只存指针 |
| 时间戳 | 创建/修改/MFT 修改/访问 | atime/mtime/ctime |
| 压缩/加密 | 原生压缩、EFS 加密 | 无原生压缩（靠上层） |
| 扩展属性 | 命名数据流（ADS） | `xattr` |

### 一次读的完整路径

```mermaid
flowchart LR
    A["read(fd, buf, n)"] --> B["VFS: file → inode"]
    B --> C{"page cache<br>命中?"}
    C -->|"命中"| D["memcpy 到用户 buf"]
    C -->|"未命中"| E["文件系统: 块映射"]
    E --> F["块层: 提交 bio/IRP"]
    F --> G["设备驱动 → 磁盘"]
    G --> H["DMA 写入页缓存"]
    H --> D
```

Windows 侧同一条路：`ReadFile` → `NtReadFile` → I/O 管理器构造 `IRP_MJ_READ` → 文件系统驱动（NTFS）→ 卷/磁盘驱动 → 完成例程。缓存由 **Cache Manager** 统一管，和 Linux 的 page cache 是同一个角色。

### page cache 的三个动作

| 动作 | 触发 | 效果 |
|:---:|---|---|
| 预读（read-ahead） | 检测到顺序读 | 一次多读几页，减少 I/O 次数 |
| 写回（write-back） | 脏页超过阈值 / 定时 / `FlushFileBuffers` | 异步写盘；写调用本身早就返回了 |
| 回收（reclaim） | 内存压力 | 干净页直接丢，脏页先写回 |

这就是「写文件返回成功，但数据还没落盘」的机制来源——**写成功只代表写进了 page cache**。

## 动手验证

### 1. 量一次「已缓存」的读写

跑 [`labs/01-syscall-cost`](../labs/01-syscall-cost/) 的 `[3]`，本机实测（文件已在 page cache）：

```text
  operation              size       iters      best us    median us       MB/s
  ------------------------------------------------------------------
  WriteFile              4 KiB       2000        1.700        2.200     2409.4
  ReadFile               4 KiB       2000        2.300        2.800     1780.9
  WriteFile              64 KiB       500       10.500       15.200     6241.5
  ReadFile               64 KiB       500        8.700       13.200     7532.9
  WriteFile              1 MiB         64      210.400      300.650     4983.7
  ReadFile               1 MiB         64      140.500      167.400     7463.2
  CreateFileW+CloseHandle -           2000       12.400       18.600          -
```

读法：

- 4 KiB 读只要 **2.3 µs**——这是 `syscall + memcpy` 的价格，不是磁盘延迟。
- 1 MiB 读 140 µs，折合约 **7.5 GB/s**，远超任何单块 SSD——因为数据在内存里。
- **打开+关闭要 12.4 µs**，比读 4 KiB 还贵：路径解析、安全检查、创建 `FILE_OBJECT` 才是大头。**所以小文件高频打开是性能杀手。**

### 2. 关掉缓存，看真实设备延迟

```c
#include <windows.h>
#include <stdio.h>

int main(void) {
    HANDLE f = CreateFileW(L"cold.bin", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);
    if (f == INVALID_HANDLE_VALUE) { printf("open failed %lu\n", GetLastError()); return 1; }

    /* NO_BUFFERING 要求缓冲区按扇区对齐；用 VirtualAlloc 拿页对齐内存（页 4 KB 是扇区整数倍） */
    char *buf = (char *)VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD written = 0;
    for (int i = 0; i < 4096; i++) buf[i] = (char)i;

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < 256; i++) {
        WriteFile(f, buf, 4096, &written, NULL);   /* 绕过缓存，直写设备 */
    }
    QueryPerformanceCounter(&t1);
    printf("NO_BUFFERING: %.1f us per 4 KiB write\n",
           (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)freq.QuadPart / 256.0);

    CloseHandle(f);
    VirtualFree(buf, 0, MEM_RELEASE);
    DeleteFileW(L"cold.bin");
    return 0;
}
```

对比同一个程序去掉 `FILE_FLAG_NO_BUFFERING` 的版本，通常会看到**一个数量级以上的差距**。注意：具体倍数取决于你的磁盘，本机没有把这个数字写死。

### 3. 看时间戳与数据流

```powershell
Get-Item C:\Windows\System32\ntdll.dll | Select-Object Length,LastWriteTime
Get-Item C:\Windows\System32\ntdll.dll -Stream *
```

本机实测：

```text
 Length LastWriteTime
 ------ -------------
2575008 2026/11/11 11:41:55

Stream  Length
------  ------
:$DATA 2575008
```

`:$DATA` 是 NTFS 的**未命名数据流**，也就是「文件内容」本身。NTFS 允许一个文件挂多条命名流（ADS），大小不体现在 `Get-Item .Length` 里——这也是「文件大小对不上」的常见原因之一。

### 4. 看 page cache 命中率

```powershell
(Get-Counter '\Cache\Data Map Hits %' -MaxSamples 1).CounterSamples[0].CookedValue
```

本机实测：

```text
100
```

100% 表示这一秒内所有「映射文件数据」的请求都命中了缓存。持续接近 100% 说明读的是热数据；如果这个值低、同时 `PhysicalDisk\Avg. Disk sec/Read` 高，才是真在等盘。

### 5. Linux 对照（本机不可执行）

```text
# Linux 示例：本机不可执行，原因见 docs/00 的环境一节
strace -T -e trace=openat,read ls     # 每次 open/read 的耗时
cat /proc/meminfo | grep -E 'Cached|Dirty|Writeback'
stat /etc/hostname                    # inode 号、块数、时间戳
```

## 常见误区

1. **以为 `WriteFile` 返回成功，数据就落盘了**。它只写进 page cache；要落盘得 `FlushFileBuffers`（或 `fsync`）。断电丢数据、数据库要 WAL，都是这个原因。
2. **以为删除文件就释放了空间**。还有进程打开着这个文件时，空间要等最后一个句柄关闭才回收——这就是「删了文件磁盘还是满」的经典现象。
3. **以为 inode 是「文件内容」**。inode 是元数据 + 数据块指针；文件名在目录项里，不在 inode 里（这也是「硬链接」能存在的原理）。
4. **以为 page cache 和 buffer cache 是两回事**。Linux 2.4 之后两者合并成统一的 page cache；Windows 的 Cache Manager 也是统一缓存。看到「缓存占用高」不要急着清，它是性能资产。
5. **以为 NTFS 的「大小」只有一个数**。`Length`（逻辑大小）、`AllocatedSize`（占盘大小）、ADS 大小可以完全不同；压缩、稀疏文件、簇大小都会影响。

## 自测

**1. 读一个 4 KiB 文件只要 2.3 µs，读一个 1 MiB 文件要 140 µs。后者为什么不是前者的 256 倍？**

- **考点**：固定开销与可变开销
- **应明白**：4 KiB 的 2.3 µs 里大部分是 syscall/路径/对象开销；1 MiB 时固定开销被摊薄，剩下的是内存拷贝带宽
- **算会**：能用「固定成本 + 每字节成本」拆开解释，并知道小文件慢在哪

**2. 程序 `WriteFile` 都返回成功，进程被杀后文件内容却少了一截。为什么？**

- **考点**：write-back 缓存
- **应明白**：数据还在 page cache 的脏页里，没提交到磁盘；进程被杀不影响内核缓存，但断电/崩溃会丢
- **算会**：能说出 `FlushFileBuffers` 的位置与代价，以及为什么数据库要自己维护 WAL

**3. 一个文件 `Get-Item` 显示 2.5 MB，但磁盘占用是 0。可能是什么？**

- **考点**：稀疏文件 / 压缩 / ADS
- **应明白**：可能是稀疏文件（只分配了元数据）、NTFS 压缩、或者内容在 ADS 里而主数据流为空
- **算会**：能说出至少两种验证方式（`-Stream *`、看 `AllocatedSize`/压缩属性）

## 相关

- 缓存背后的物理内存：[`05 · 物理内存`](./05-physical-memory.md)
- I/O 模型与设备：[`09 · I/O 与多路复用`](./09-io-and-epoll.md)
- 虚拟内存映射：[`04 · 虚拟内存`](./04-virtual-memory.md)
- 实验：[`labs/01-syscall-cost`](../labs/01-syscall-cost/)
