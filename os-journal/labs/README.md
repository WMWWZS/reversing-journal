# Labs · 实验

> **看懂 ≠ 会了。** 每个概念都要有一个能编译、能跑、能自己改的实验。

## 约定

每个实验一个目录，编号与 `docs/` 对应，必须包含：

```text
lab-name/
├── README.md      # 目的、原理、真实输出、自测题
├── build.ps1      # 一键构建（Windows + MinGW）
├── run.ps1        # 可选：多轮对照 + 落 evidence/（需要多份证据时才有）
├── *.c / *.h      # 源码，只用 Windows API，不引入第三方库
└── *.c            # 可选：对照程序
```

三条硬规矩：

1. **必须在本机跑通**：README 里贴的输出只能是真实运行结果，不能是理论值。
2. **必须防优化**：测量循环要把结果喂给 `volatile` 变量，否则 `-O2` 会把循环删掉。
3. **不提交二进制**：`*.exe`、`*.o` 已在 `.gitignore` 里忽略。

## 实验列表

| 编号 | 实验 | 对应笔记 | 状态 |
|:---:|---|---|:---:|
| 01 | 系统调用与内核路径成本 | [01 · 内核边界](../docs/01-kernel-boundary.md) | ✅ 完成 |
| 02 | 进程地址空间探测：地址空间分区 · ASLR · TEB 栈 · 句柄 · syscall | [02 · 进程与线程](../docs/02-process-and-thread.md)、[04 · 虚拟内存](../docs/04-virtual-memory.md) | ✅ 完成 |
| 03 | 缺页与工作集追踪 | [04 · 虚拟内存](../docs/04-virtual-memory.md) | ⬜ 待做 |
| 04 | 物理内存与池泄漏趋势 | [05 · 物理内存](../docs/05-physical-memory.md) | ⬜ 待做 |
| 05 | 文件读写的缓存 vs 直写 | [06 · 文件系统](../docs/06-file-system.md) | ⬜ 待做 |
| 06 | 锁竞争与丢失更新 | [07 · 同步与并发](../docs/07-sync-and-concurrency.md) | ⬜ 待做 |
| 07 | 共享内存与命名管道 | [08 · IPC](../docs/08-ipc.md) | ⬜ 待做 |
| 08 | IOCP 与阻塞 I/O 的并发规模 | [09 · I/O 模型](../docs/09-io-and-epoll.md) | ⬜ 待做 |

> 编号与 `docs/` 的章节号不必一一对应（一篇文档可能对应多个实验），但**必须能互链**。

## 环境要求

- Windows 11 x64（本机无可用 Linux 环境）
- MinGW-w64：`gcc`（本机 14.2.0）、`objdump`
- PowerShell 5.1+（`build.ps1` 带 UTF-8 BOM，5.1 也能读）

自检：

```powershell
gcc --version
objdump --version
powershell -ExecutionPolicy Bypass -File .\tools\check-env.ps1
```

## 运行方式

每个实验都一样的入口：

```powershell
cd .\labs\01-syscall-cost
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

`build.ps1` 只做「编译 + 运行」，不改系统设置、不装东西、不需要管理员权限。**唯一的副作用是在临时目录写一个测量用的文件，跑完自己删掉。**
