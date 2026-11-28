# 任务清单

> 勾掉一个才算学完一个。每周复盘一次。
> 原则：**以题带练**，不要从第一章开始啃书。

---

## 第 1–2 周目标：做完之后你能独立回答什么

**这两周不要求你「看完什么」，只要求你能独立回答下面 6 个问题。** 答不上来就是没学会。

| # | 问题 | 对应能力 | 在哪练 |
|:---:|---|---|---|
| 1 | 一次系统调用为什么至少要几十纳秒？这几十纳秒花在哪？ | 内核边界 | lab01 `[2]`、[docs/01](docs/01-kernel-boundary.md) |
| 2 | 为什么 `NtQuerySystemTime` 只要 2 ns，而 `NtQuerySystemInformation` 要 70 ns？ | stub 与转发 | lab01 自测 1 |
| 3 | 读一个 4 KiB 文件要 2.3 µs，打开+关闭却要 12.4 µs。为什么？ | 文件对象与路径解析 | lab01 `[3]`、[docs/06](docs/06-file-system.md) |
| 4 | 创建一个线程 38 µs，切换一次 2 µs。线程池省的是什么？ | 进程与线程 | lab01 `[4]`、[docs/02](docs/02-process-and-thread.md) |
| 5 | 「已提交 64 MiB」和「已触碰 64 MiB」差多少？差在哪一步？ | 缺页与三态内存 | lab01 `[5]`、[docs/04](docs/04-virtual-memory.md) |
| 6 | 为什么 QPC 测 2 ns 的调用必须批量计时，不能单次测？ | 计时器分辨率 | lab01 自测 2 |

> **检验方式**：合上电脑，用嘴讲出来。讲得清楚才是真懂，讲不清楚就是没懂。

---

## 第 1 周：把「边界」和「计时」练成肌肉记忆

### 必修（本周必须全部勾掉）

- [ ] 跑通 `labs/01-syscall-cost/build.ps1`，确认输出里有五组表
- [ ] **自测 1**：用 `objdump -p` 在 `kernel32.dll` 导入表里找到 `NtCreateFile`
- [ ] **自测 2**：用 `objdump -p` 在 `ntdll.dll` 导出表里找到 `NtCreateFile`
- [ ] **自测 3**：把 lab 的 `[2]` 单次计时改成「不批量」，观察输出变成 0 / 100 ns
- [ ] **自测 4**：注释掉 `g_sink += ...`，确认 `-O2` 会把哪个循环删掉（看耗时变化）
- [ ] 读 [docs/01](docs/01-kernel-boundary.md)，能画出「用户态 → ntdll stub → 内核」的路径
- [ ] 用 `Get-Counter '\System\System Calls/sec'` 采 5 次，记录波动范围
- [ ] 在 `notes/` 写第一条记录（用模板），写清「QPC 分辨率不够」这件事怎么发现的

### 进阶（有余力再做）

- [ ] 用 `objdump -p` 对比 `ntdll.dll` 与 `win32kfull.sys` 的导出数量
- [ ] 把 lab 的 `[1]` 块大小改成 128 MiB，看带宽是否继续下降
- [ ] 查一下 `KUSER_SHARED_DATA` 在用户态的地址（提示：`0x7FFE0000`）

**本周产出**：lab01 跑通 + 至少 1 条 `notes/`。

---

## 第 2 周：把「读到的东西」变成证据

- [ ] 给 lab01 加一组新测量：`CreateFileW` 打开**不存在**的文件，测失败路径的耗时
- [ ] 用 `typeperf "\System\Context Switches/sec" -sc 5` 采一段基线
- [ ] 边跑 lab `[4b]` 边采上下文切换数，对比「跑之前 / 跑之中」
- [ ] 读 [docs/02](docs/02-process-and-thread.md)，能说出 `EPROCESS`/`KTHREAD`/`TEB` 各存什么
- [ ] 用 `Get-Process -Id $PID` 记录自己进程的 CPU 时间、句柄数、线程数
- [ ] 在 `notes/` 里写一条「我以为 X，实际 Y」的记录（必须有原始输出）

**本周产出**：1 条带对照组的笔记 + lab01 的第二组数据。

---

## 第 3–4 周：进程与线程 + 调度

- [ ] 跑通 [`labs/02-address-space-probe`](labs/02-address-space-probe/)：`build.ps1` + `run.ps1`，确认 `[A]`~`[F]` 六段输出都有真实数字
- [ ] **自测 1**：用 `ImageBase + RVA` 解释「同文件 3 次基址一样、复制改名就变」，并算出 `0x140001450` 的运行时地址
- [ ] **自测 2**：把 syscall 批次从 5000 放大到 160 万，确认 `kernel ms` 从 `0.000` 变成 `750`
- [ ] **自测 3**：从 `TEB+0x08` / `+0x10` 读出两个线程各自的栈范围，解释 24 KB 与 12 KB 的含义
- [ ] 读 [docs/03](docs/03-scheduling.md)，能用权重表算出 nice 0 与 nice 10 的 CPU 份额比
- [ ] 用 `Get-Process` 统计本机优先级类别分布
- [ ] 写一个「创建 1000 个线程」的程序，观察失败点与内存变化（小心：先算地址空间）
- [ ] 用 `Get-Counter '\System\Processor Queue Length'` 判断本机是否在排队
- [ ] 记录一次「CPU 不高但很卡」的观察，列出至少 3 个可能原因
- [ ] 设计一个实验区分「等锁」和「等 I/O」（提示：对比 CPU 时间与切换数）

**本周产出**：lab02 的地址空间实测（ASLR 三表 + TEB 栈 + 句柄 + syscall）+ 一份「线程数量 vs 创建失败点」的实测记录。

---

## 第 5–6 周：虚拟内存与物理内存

- [ ] 读 [docs/04](docs/04-virtual-memory.md)、[docs/05](docs/05-physical-memory.md)
- [ ] 写程序分别测：`MEM_RESERVE` 1 GB、`MEM_COMMIT` 1 GB、触碰 1 GB 的耗时
- [ ] 用 `Get-Counter '\Memory\Pages/sec'` 与 `Page Faults/sec` 区分软/硬缺页
- [ ] 解释「working set 之和 > 物理内存」并用 PSS 的概念说明
- [ ] 画出 48 位 VA 的四级拆分图（手画，不看书）
- [ ] 在 `notes/` 里记一条「commit 与 resident 不一致」的实例

**本周产出**：缺页三态实测表 + 一张手画页表图。

---

## 第 7–8 周：文件系统 + I/O 与设备

- [ ] 读 [docs/06](docs/06-file-system.md)、[docs/09](docs/09-io-and-epoll.md)
- [ ] 用 `FILE_FLAG_NO_BUFFERING` 对比缓存读写与直写设备的差距
- [ ] 用 `Get-Item -Stream *` 观察 NTFS 的 `:$DATA`
- [ ] 用 `Get-Counter '\Cache\Data Map Hits %'` 观察缓存命中
- [ ] 写一个 IOCP 最小程序，确认 `WriteFile` 返回 `FALSE` + `ERROR_IO_PENDING` 不是失败
- [ ] 把一次文件读的时间拆成「syscall / 拷贝 / 设备」三段并试着估算占比

**本周产出**：一份「缓存 vs 直写」对照表。

---

## 第 9–10 周：同步与并发 + IPC

- [ ] 读 [docs/07](docs/07-sync-and-concurrency.md)、[docs/08](docs/08-ipc.md)
- [ ] 跑通 docs/07 里的 `sync_demo`，确认能复现「丢失更新」
- [ ] 把 `plain ++` 换成 `InterlockedIncrement`，确认结果正好等于期望值
- [ ] 测三种原语在 2 线程竞争下的 ns/op，排序并解释
- [ ] 写一个共享内存 + 命名事件的生产者/消费者（两个进程）
- [ ] 用 `\\.\pipe\` 枚举本会话命名管道，记录数量
- [ ] 在 `notes/` 里记一条「死锁现场」或「差点死锁」的分析

**本周产出**：竞态复现记录 + 一个跨进程共享内存 demo。

---

## 第 11–12 周：观测与综合实验

- [ ] 读 [docs/10](docs/10-kernel-observability.md)
- [ ] 用 `logman query providers` 找到 3 个内核 provider 并抄下 GUID
- [ ] 用 `wpr -start CPU -filemode` / `wpr -stop` 录一段 `.etl`（本机可录；分析用 WPA，已装）
- [ ] 用「四数字法」分析一个真实进程，写出「假设 → 计数器 → 结论」
- [ ] 找一次偶发卡顿，证明瓶颈是 CPU / 内存 / 锁之一
- [ ] 把全过程写成一份报告：现象、假设、证据、结论、误判
- [ ] 更新 `notes/` 索引，把 12 周里最有价值的 3 条置顶

**本周产出**：一份完整的瓶颈定位报告（必须包含一次「猜错了」）。

---

## 每天怎么安排

| 时长 | 内容 | 说明 |
|:---:|---|---|
| 30 分钟 | 读 | 只读当天要动手的那一节，不要通读 |
| 60 分钟 | 做 | 跑 lab、改参数、记数字；**必须动手** |
| 15 分钟 | 写 | 在 `notes/` 留一条：现象、证据、结论 |

## 记录规则

每个任务做完，在 `notes/` 里写一条，回答四个问题：

1. 我看到了什么？（带数字/输出）
2. 我的第一反应是什么？
3. 实际原因是什么？证据是什么？
4. 下次遇到同类现象，先查什么？

模板：[`notes/_TEMPLATE.md`](notes/_TEMPLATE.md)

## 不要做的事

- ❌ 从《操作系统导论》第一章开始通读
- ❌ 看几小时课程视频不动手
- ❌ 背 `EPROCESS` 的字段偏移（换个 build 就变）
- ❌ 拿理论值当实测值（「内存带宽 50 GB/s」不是本机数字）
- ❌ 只打开任务管理器就下结论
- ❌ 在没有假设的情况下开 ETW 乱录
- ❌ 在自己没有权限的机器上做内核级观测
