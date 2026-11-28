# 04 · 虚拟内存与分页：每个进程都以为自己独占内存

> 一句话：程序里那个「地址」从来不是内存条上的位置，而是一张由内核维护的映射表——理解这张表，才知道内存为什么会「看起来用了很多、其实没占」。

## 是什么 / 为什么重要

每个进程拿到的都是完整、连续、私有的地址空间。x64 上虚拟地址是 48 位有效：

| 区域 | 范围 | 大小 |
|:---:|---|---:|
| 用户空间 | `0x00000000_00000000` – `0x00007FFF_FFFFFFFF` | 128 TB |
| 内核空间 | `0xFFFF8000_00000000` – `0xFFFF_FFFF_FFFF_FFFF` | 128 TB |

（上面是 Windows x64 的典型布局；Linux x86-64 的用户/内核分界也在 `0x00007FFF_FFFFFFFF`，但具体映射不同。）

这张表带来三件事：

1. **隔离**：A 进程的地址在 B 进程里根本不存在，写不进去。
2. **超额分配**：可以「预留」1 GB 却只提交 4 KB，物理内存按需给。
3. **共享**：同一个物理页可以映射进多个进程（DLL、共享内存、page cache）。

## 机制与结构

### 48 位地址怎么拆成四级

```mermaid
flowchart LR
    VA["虚拟地址 48 位"] --> L4["PML4 索引<br>bit 47-39"]
    L4 --> L3["PDPT 索引<br>bit 38-30"]
    L3 --> L2["PD 索引<br>bit 29-21"]
    L2 --> L1["PT 索引<br>bit 20-12"]
    L1 --> OFF["页内偏移<br>bit 11-0"]
    OFF --> PHY["物理页帧 PFN"]
```

| 层级 | 表项数 | 每个表项覆盖的地址范围 |
|:---:|---:|---:|
| PML4 | 512 | 512 GB |
| PDPT | 512 | 1 GB |
| PD | 512 | 2 MB |
| PT | 512 | 4 KB |

一次 TLB 未命中的地址翻译要**最多 4 次内存访问**（每级查表一次）。这就是 TLB 存在的意义：命中时只要 1 个周期级别。

### PTE：64 位里塞一个页的所有状态

| 位 | 名称 | 含义 |
|:---:|---|---|
| 0 | `Valid`（P） | 1 = 页在物理内存里 |
| 1 | `Writable`（R/W） | 1 = 可写 |
| 2 | `User`（U/S） | 1 = 用户态可访问 |
| 3–4 | `PWT` / `PCD` | 写穿透 / 禁止缓存（MMIO 常用） |
| 5 | `Accessed` | CPU 置位：这页被读过 |
| 6 | `Dirty` | CPU 置位：这页被写过 |
| 7 | `PAT` | 页属性表索引 |
| 8 | `LargePage` | 1 = 大页（2 MB 或 1 GB） |
| 9 | `Global` | 切换 `CR3` 时不清 TLB |
| 12–51 | `PFN` | 物理页帧号 |
| 63 | `NX` | 1 = 不可执行（DEP） |

```c
/* x86-64 4 KB PTE 的位域（architectural 部分） */
typedef union {
    unsigned long long value;
    struct {
        unsigned long long present   : 1;   /* bit 0  */
        unsigned long long writable  : 1;   /* bit 1  */
        unsigned long long user      : 1;   /* bit 2  */
        unsigned long long pwt       : 1;
        unsigned long long pcd       : 1;
        unsigned long long accessed  : 1;
        unsigned long long dirty     : 1;
        unsigned long long pat       : 1;
        unsigned long long large     : 1;
        unsigned long long global    : 1;
        unsigned long long avail     : 3;   /* 内核自用 */
        unsigned long long pfn       : 40;  /* bit 12-51 */
        unsigned long long reserved  : 11;
        unsigned long long nx        : 1;   /* bit 63 */
    } bits;
} pte64_t;
```

> Windows 在 `present = 0` 时用低位存软件状态（`Transition`、`Prototype`、`PageFile` 等）。所以「PTE 为 0」不代表「地址无效」，可能只是页被换出去了——这正是内存分析工具容易误判的地方。

### 缺页异常：三种完全不同的路径

| 类型 | 触发场景 | 内核做什么 | 代价量级 |
|:---:|---|---|---|
| 软缺页（soft） | 页已在物理内存，只是进程工作集里没有（standby/零页） | 把 PTE 指向已有页帧，加入工作集 | 几百 ns |
| 硬缺页（hard） | 页在页面文件或映射文件里，不在内存 | 发起 I/O、等待、再映射 | 几十 µs 到几 ms |
| 写时复制（COW） | 共享页被写（`fork` 后的页、只读映射） | 复制一份新页、改 PTE 为可写 | 一次复制 + 软缺页 |
| 访问违例 | 真的无权访问（`PAGE_NOACCESS`、越界） | 转成异常抛给进程 | 进程崩溃 |
| Guard page | 栈增长检测、`PAGE_GUARD` | 解除 guard、按需提交下一页 | 一次软缺页 |

### Windows 的三态内存

| 状态 | API | 含义 | 是否占物理内存 |
|:---:|---|---|:---:|
| Reserved | `VirtualAlloc(..., MEM_RESERVE, ...)` | 只占地址空间，PTE 无效 | 否 |
| Committed | `MEM_COMMIT` | 承诺「要用时给得出」，PTE 指向零页/页面文件 | 不一定 |
| Resident | 首次访问后 | 页真正在物理内存里，PTE 有效 | 是 |

**这三态就是任务管理器里那堆数字互相矛盾的根源**：`Commit size` 看的是第二态，`Working set` 看的是第三态。

### `mmap` 与 `VirtualAlloc` 对照

| 目的 | Linux | Windows |
|:---:|---|---|
| 预留地址空间 | `mmap(PROT_NONE)` | `VirtualAlloc(MEM_RESERVE)` |
| 提交/映射匿名内存 | `mmap(MAP_ANONYMOUS)` | `VirtualAlloc(MEM_COMMIT)` |
| 映射文件（共享） | `mmap(MAP_SHARED, fd)` | `CreateFileMapping` + `MapViewOfFile` |
| 映射文件（私有 COW） | `mmap(MAP_PRIVATE, fd)` | `CreateFileMapping(PAGE_WRITECOPY)` |
| 释放 | `munmap` | `VirtualFree(MEM_RELEASE)` |
| 建议内核（预读/释放） | `madvise` | `PrefetchVirtualMemory` / `OfferVirtualMemory` |

## 动手验证

### 1. 首次触碰一页到底多贵

跑 [`labs/01-syscall-cost`](../labs/01-syscall-cost/) 的 `[5]`，本机实测（64 MiB = 16384 页）：

```text
  operation                                 pages      ns/page
  ------------------------------------------------------------------
  commit all + first touch (fault)          16384        394.9
  second touch (already resident)           16384         15.6
  MEM_COMMIT per page (reserve first)       16384        413.7
  first touch after per-page commit         16384        454.2
  -> page faults are about 96% of first-touch cost, i.e. 379 ns / 4 KiB
```

三个可以直接用的结论：

1. **首次触碰 ≈ 395 ns/页**，第二次写同一页只要 **15.6 ns**——差的 379 ns 就是缺页处理（软缺页 + 零页映射）。
2. **`MEM_COMMIT` 本身也要 ~414 ns/页**：它是 syscall，还要改 VAD；「提交」不是免费的。
3. **提交之后首次触碰仍要 ~454 ns**：因为提交只保证「给得出」，页还没进工作集。**commit ≠ resident**。

### 2. 看提交量与工作集

```powershell
$p = Get-Process -Id $PID
"WS(MB)={0:N1}  Private(MB)={1:N1}" -f ($p.WorkingSet64/1MB), ($p.PrivateMemorySize64/1MB)
(Get-Counter '\Memory\Committed Bytes','\Memory\Commit Limit' -MaxSamples 1).CounterSamples |
  ForEach-Object { "{0} = {1:N0}" -f $_.Path.Split('\')[-1], $_.CookedValue }
```

本机实测：

```text
WS(MB)=78.5  Private(MB)=62.6
committed bytes = 36,975,042,560
commit limit = 45,878,075,392
```

`commit limit`（约 45.9 GB）> 物理内存，因为它把页面文件也算进去了。**committed 接近 limit 时系统才会真正开始拒绝分配**。

### 3. 亲手摸一次 reserve / commit / touch

```c
#include <windows.h>
#include <stdio.h>

int main(void) {
    const SIZE_T one_page = 4096;
    /* 只预留 1 GB 地址空间，不提交 */
    char *p = (char *)VirtualAlloc(NULL, 1u << 30, MEM_RESERVE, PAGE_NOACCESS);
    if (!p) { printf("reserve failed %lu\n", GetLastError()); return 1; }
    printf("reserved at %p (no physical memory used yet)\n", (void *)p);

    /* 只提交第一页 */
    if (!VirtualAlloc(p, one_page, MEM_COMMIT, PAGE_READWRITE)) {
        printf("commit failed %lu\n", GetLastError()); return 1;
    }
    /* 第一次写：触发缺页，页才算 resident */
    p[0] = 42;
    printf("first touch ok: p[0]=%d\n", p[0]);

    VirtualFree(p, 0, MEM_RELEASE);
    return 0;
}
```

用 gcc 编译（`gcc -O2 -o vm_demo.exe vm_demo.c`）后运行，应看到 `reserved at ...` 与 `first touch ok`；任务管理器里这个进程的提交量在预留后**不变**，提交后才 +4 KB。

### 4. Linux 对照（本机不可执行）

```text
# Linux 示例：本机不可执行，原因见 docs/00 的环境一节
cat /proc/self/maps        # 看 VMA 布局
cat /proc/self/smaps       # 每段的 RSS / PSS
vmstat -s | head           # 缺页统计
```

## 常见误区

1. **以为「虚拟内存 = 页面文件」**。虚拟内存是「地址空间」这个抽象；页面文件只是硬缺页时的一个后备存储。关掉页面文件，虚拟内存照样工作，只是 commit limit 变小。
2. **以为 `VirtualAlloc` 成功就是「占了内存」**。`MEM_RESERVE` 只占地址空间，`MEM_COMMIT` 只承诺可提供，真正占物理页的是第一次访问。三个数字（reserved / committed / working set）必须分开看。
3. **以为「缺页」是错误**。缺页是正常的按需分页机制：本机空闲时 `Page Faults/sec` 也在 40 万量级（大部分是软缺页）。只有**硬缺页**多才是问题。
4. **以为任务管理器里的「内存」就是物理内存**。默认列是 `Working set (private)`，既不是 commit，也不是 PSS，多进程共享 DLL 时还会重复计算。
5. **以为页大小固定 4 KB**。x86-64 支持 2 MB / 1 GB 大页；数据库、JVM 常用大页来减少 TLB miss 和页表开销。

## 自测

**1. 一个进程 `Commit size` 是 2 GB，`Working set` 是 200 MB。这意味着什么？**

- **考点**：commit 与 resident 的区别
- **应明白**：它承诺了 2 GB 的「可提供」额度，但只有 200 MB 真在物理内存里；剩下的是预留/已提交未触碰/被换出的页
- **算会**：能解释为什么这两个数字可以差一个数量级，且都「没算错」

**2. 为什么首次触碰一页要 ~395 ns，而第二次写只要 ~15 ns？**

- **考点**：缺页处理路径
- **应明白**：首次触碰要陷入内核、找零页、建 PTE、更新工作集；第二次 PTE 已有效，只是普通写内存
- **算会**：能说出这 380 ns 花在哪些步骤上，以及为什么大页能摊薄它

**3. 四级页表一次完整翻译最多几次内存访问？TLB 在这里起什么作用？**

- **考点**：地址翻译成本
- **应明白**：4 次（PML4/PDPT/PD/PT）；TLB 缓存最近的翻译结果，命中就省掉这 4 次
- **算会**：能解释「为什么随机访问大数组比顺序访问慢」里 TLB 的那部分原因

## 相关

- 物理内存怎么管：[`05 · 物理内存`](./05-physical-memory.md)
- 边界与 syscall：[`01 · 内核边界`](./01-kernel-boundary.md)
- 实验：[`labs/01-syscall-cost`](../labs/01-syscall-cost/)
- 实验：[`labs/02-address-space-probe`](../labs/02-address-space-probe/)：地址空间分区、ASLR 三条实测结论、栈的按需增长
- 观测工具：[`10 · 内核观测`](./10-kernel-observability.md)
