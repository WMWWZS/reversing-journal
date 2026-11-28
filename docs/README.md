# docs · 文档索引

> 主线文档：逆向、内存、PE、调试器、引擎、反作弊，以及 OS / 网络何时该补。

文件名保持英文，是为了不打断各文档之间的相对链接；**标题与一句话摘要都是中文**，看这一页就能决定读哪篇。

| 文件 | 中文标题 | 一句话 |
|---|---|---|
| [00-roadmap.md](00-roadmap.md) | 学习路线：24 周（主线） | 从「能读代码」到「能独立把一个游戏客户端拆到能量化的程度，并做出能跑的东西」。 |
| [01-memory-layout.md](01-memory-layout.md) | 01 · 内存布局：逆向的第一性原理 | 逆向工程的全部工作，本质上就是还原两样东西： |
| [02-calling-conventions.md](02-calling-conventions.md) | 02 · 调用约定与栈帧 | 看反汇编的第一步：搞清楚「参数在哪、返回值在哪、栈怎么长」。 |
| [03-toolchain.md](03-toolchain.md) | 03 · 工具链 | 工具是消耗品，方法论才是资产。但工具有手感，手感靠练。 |
| [04-engine-unity.md](04-engine-unity.md) | 04 · Unity 引擎专项 | Unity 是逆向门槛最低、也是反作弊最容易翻车的引擎。 |
| [05-engine-unreal.md](05-engine-unreal.md) | 05 · Unreal Engine 专项 | UE 的逆向核心是三张全局表：GObjects、GNames、以及 UWorld 的对象链。 |
| [06-network-protocol.md](06-network-protocol.md) | 06 · 网络协议分析 | 客户端可以被完全控制，只有网络这一层是攻击者无法伪造的边界。 |
| [07-anticheat.md](07-anticheat.md) | 07 · 反作弊：威胁模型与防护设计 | 这是这个仓库的最终目的地。 |
| [08-ghidra.md](08-ghidra.md) | 08 · Ghidra 上手 | 装好之后第一次打开 player_O0.exe 的完整流程。 |
| [09-pe-format.md](09-pe-format.md) | 09 · PE 格式：从磁盘到内存 | R3（第 7–9 周）的核心。看不懂 PE，你在 Ghidra 里就只是「翻函数」，而不是「理解一个程序」。 |
| [10-registers.md](10-registers.md) | 10 · 寄存器入门：`rcx` 到底是什么 | 学汇编卡住的第一个坎，几乎都是「这些 r 开头的字母是什么」。 |
| [11-flags.md](11-flags.md) | 11 · 标志位：CPU 的「上一次运算体检报告」 | 上一篇讲寄存器是「放数据的抽屉」。标志位不是数据，是上一次运算留下的判断依据。 |
| [12-x64dbg.md](12-x64dbg.md) | 12 · x64dbg 上手：让程序跑起来看 | Ghidra 是「看图纸」，x64dbg 是「上手拆」。 |
| [13-stack-frame.md](13-stack-frame.md) | 13 · 栈帧是什么（零基础版） | 这一篇不用任何前置知识。看完你应该能指着 x64dbg 说清楚 RSP、RBP、[RBP+0x10] 分别在干嘛。 |
| [14-operating-system.md](14-operating-system.md) | 14 · 操作系统：你双击 exe 之后，内核到底给了你什么 | 前置：13 讲栈帧。 |
| [15-network.md](15-network.md) | 15 · 网络通信：从一行 `send()` 到网线上的 12 个字节 | 前置：14 讲操作系统（进程/句柄/内核态）。 |
| [16-when-os-and-net.md](16-when-os-and-net.md) | 16 · 什么时候真正需要"操作系统"和"网络"这两块 | 这份文档只回答两个问题： |

共 17 篇。