# 镜像基址在同一次开机里是固定的——「ASLR 每次随机」这句话只对了一半

- **日期**：2026-11-23
- **标签**：#ASLR #地址空间 #TEB #句柄
- **环境**：Windows 11 x64 / MinGW-w64 gcc 14.2 / 无可用 Linux 环境
- **相关文档**：[`labs/02-address-space-probe`](../labs/02-address-space-probe/)、[`docs/02-process-and-thread.md`](../docs/02-process-and-thread.md)、[`docs/04-virtual-memory.md`](../docs/04-virtual-memory.md)

## 现象

跑 `labs/02-address-space-probe` 的 `run.ps1`：同一个 `osprobe.exe` 连续跑 3 次，打印出来的模块基址 `A1` **三次完全一样**，都是 `0x00007ff6ec840000`。

第 4 次跑的是 `osprobe_copy.exe`——`osprobe.exe` 的**字节完全相同**的副本，只是换了个文件名——`A1` 变成了 `0x00007ff79f940000`。

同一个程序，什么都没改，基址却「有时不变、有时变」。

## 我看到了什么（原始输出）

`evidence/aslr.txt` 表 1（同文件 ×3，同一次开机，中间没重启）：

```text
  #  what                                       run1                 run2                 run3                 same?
  A1  module base       GetModuleHandle(NULL)    0x00007ff6ec840000   0x00007ff6ec840000   0x00007ff6ec840000   SAME
  A5  kernel32.dll base GetModuleHandle("kernel32.dll") 0x00007ff8772c0000   0x00007ff8772c0000   0x00007ff8772c0000   SAME
  A6  ntdll.dll base    GetModuleHandle("ntdll.dll") 0x00007ff877d90000   0x00007ff877d90000   0x00007ff877d90000   SAME
  A3  heap block        HeapAlloc(GetProcessHeap()) 0x000001cf565f13e0   0x0000021b7aa91110   0x000001f5a1a61110   DIFFERENT
  A4  stack local       &local_marker (in main)  0x000000b03a1ff677   0x000000ac38bffda7   0x0000004d993ffd07   DIFFERENT
  D1  main   StackBase  (TEB+0x08)               0x000000b03a200000   0x000000ac38c00000   0x0000004d99400000   DIFFERENT
  D2  main   StackLimit (TEB+0x10)               0x000000b03a1fa000   0x000000ac38bfa000   0x0000004d993fa000   DIFFERENT
  Z   main stack size = D1-D2 (KB)               24 KB                24 KB                24 KB                SAME
```

表 2（同一个文件 vs 字节相同的副本）：

```text
  A1  module base       GetModuleHandle(NULL)    0x00007ff6ec840000   0x00007ff79f940000   CHANGED
  A5  kernel32.dll base GetModuleHandle("kernel32.dll") 0x00007ff8772c0000   0x00007ff8772c0000   SAME
  A6  ntdll.dll base    GetModuleHandle("ntdll.dll") 0x00007ff877d90000   0x00007ff877d90000   SAME
  A4  stack local       &local_marker (in main)  0x000000b03a1ff677   0x000000d4d8fff6e7   CHANGED
```

两个 exe 的 sha256 完全一样，都是 `6e7f30841335c7c3b3c99c8b64a7f6ce92626b451ca8ea40d5da059b87917304`；
`objdump -p` 里 PE 申请的 `ImageBase = 0x0000000140000000`。

## 我的第一反应

「ASLR 不就是每次运行随机吗？三次一样，说明我读错了地址，或者探针没真的读 `GetModuleHandle(NULL)`。」

第一反应是去怀疑测量，而不是怀疑「ASLR 每次随机」这句背下来的话。

## 实际原因

镜像（exe/DLL）的基址随机化不是「每次运行现抽一个数」，而是以**镜像文件本身 + 本次开机的随机种子**为输入算出来的：

- 同一个文件 + 同一次开机 → 每次加载都用同一个结果，所以 3 次全一样。
- 换一个文件（哪怕字节相同、只是改个名字）→ 输入变了，基址就变了。
- 系统 DLL（kernel32/ntdll）还要在整个开机期间被**所有进程共享同一份物理页**，所以它们的基址必须全局一致——表 1、表 2 里它们一直没变。

而**栈和堆**不共享，每个进程创建时现抽，所以 3 次全不同；但**形状不变**（主栈 24 KB，3 次一样），随机的只是位置。

## 证据

| 证据类型 | 内容 |
|:---:|---|
| 命令 | `powershell -ExecutionPolicy Bypass -File .\run.ps1`（跑 3 次同文件 + 1 次复制件） |
| 输出 | 表 1：A1/A5/A6 `SAME`，A3/A4/D1/D2 `DIFFERENT`，栈大小 24 KB `SAME` |
| 对照组 | 表 2：只把文件换个名字（sha256 相同），A1 `0x00007ff6ec840000` → `0x00007ff79f940000`；DLL 仍 `SAME` |
| 反证 | 栈局部变量 A4 三次全不同，说明「读地址」这件事本身没问题 |

## 结论

**当看到「同一个程序每次基址都不一样」时，先确认两件事：文件换没换、机器重启没重启。**
同一次开机内，同一个镜像文件的基址是固定的；换文件（重编译 / 改名 / 换目录）或重启才会变；栈和堆才是逐进程随机。

对调试的意义：**地址一律用「基址 + RVA」**。PE 里 `ImageBase = 0x140000000`，Ghidra 里 `0x140001450` 的 RVA 是 `0x1450`，
运行时就是 `0x7ff6ec840000 + 0x1450 = 0x7ff6ec841450`。写死绝对地址的脚本换个文件就废。

## 延伸

- **重启后基址会不会变？** 本次没有「重启前 / 重启后」的对照，只能说到「同一次开机内固定」——待验证。
- **同目录重新编译（文件内容变了）呢？** 按这个解释应该变，但本次只测了「复制改名」，没测「重编译」——待验证。
- 为什么 DLL 必须全局同基址、而 exe 不需要？这和「共享映射 / 重定位 / COW」是同一串问题，下一步想看 `docs/04` 的页表部分。
- 能不能不重启就观测到「基址重新随机」？（比如同时加载同一个 DLL 的两个不同版本，观察是否冲突）
