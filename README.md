<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="assets/banner-dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="assets/banner-light.svg">
  <img src="assets/banner-light.svg" width="1280" alt="Reversing Journal — learn the attack, design the defense">
</picture>

<strong>二进制之下，没有秘密。</strong><br>
面向 <strong>游戏安全防护</strong> 的逆向工程学习手记 —— 不是教人写外挂，<br>
而是搞懂「作弊器是怎么工作的」，再把它堵死。

<a href="docs/00-roadmap.md"><img src="https://img.shields.io/badge/roadmap-12_weeks-0d1117?style=flat-square&amp;labelColor=161b22" alt="roadmap"></a>
<a href="docs/03-toolchain.md"><img src="https://img.shields.io/badge/toolchain-MinGW--w64_GCC-0d1117?style=flat-square&amp;labelColor=161b22&amp;logo=gnu&amp;logoColor=e6edf3" alt="toolchain"></a>
<a href="labs/"><img src="https://img.shields.io/badge/stack-C%2B%2B__x86--64-0d1117?style=flat-square&amp;labelColor=161b22&amp;logo=cplusplus&amp;logoColor=00599C" alt="stack"></a>
<a href="docs/07-anticheat.md"><img src="https://img.shields.io/badge/focus-game_security-0d1117?style=flat-square&amp;labelColor=161b22" alt="focus"></a>
<img src="https://img.shields.io/badge/platform-Windows_11_x64-0d1117?style=flat-square&amp;labelColor=161b22&amp;logo=windows&amp;logoColor=0078D4" alt="platform">
<img src="https://img.shields.io/badge/status-week_01--10-d29922?style=flat-square&amp;labelColor=161b22" alt="status">

<sub><a href="docs/00-roadmap.md">12 周路线</a> · <a href="labs/">实验</a> · <a href="writeups/">题解</a> · <a href="resources.md">资源</a> · <a href="notes/_TEMPLATE.md">记录模板</a></sub>

</div>

## ▍ 这是什么样的仓库

逆向工程有个特点：**看懂了不等于会了**。

所以这里不是教程合集，而是**学习日志**：每学一个概念，就在 [`labs/`](labs/) 里做一个能编译、能复现的实验；每分析一个目标，就在 [`writeups/`](writeups/) 里留下一篇题解；每个结论都必须能指回「地址 → 证据」。

> **看懂 ≠ 会了。** 没有写下来的分析，等于没做过。

约定：不提交编译产物（`*.exe`、`*.o` 等已在 [`.gitignore`](.gitignore) 中忽略）。

## ▍ 目录

| 目录 | 用途 | 现有 |
|:---:|---|---:|
| [`docs/`](docs/) | 系统化知识笔记：内存布局 · 调用约定 · 工具链 · 引擎专项 · 协议 · 反作弊 · Ghidra · PE · 寄存器 · 栈帧 · 操作系统 · 网络 | 15 篇 |
| [`labs/`](labs/) | 可编译、可下断点的实验，一个概念一个实验 | 3 个 |
| [`writeups/`](writeups/) | crackme / CTF / 目标分析的完整题解 | 1 篇 |
| [`notes/`](notes/) | 零散观察与踩坑记录，随手记，不成体系 | 3 条 |
| [`tools/`](tools/) | Ghidra 启动/同步脚本、GhidraScript 与反编译辅助 | 脚本 |
| [`resources.md`](resources.md) | 书单、靶场、社区，以及法律与授权边界 | 清单 |

> **第一次来？** 先读 [`docs/00-roadmap.md`](docs/00-roadmap.md) 看 12 周路线，跑一遍 [`labs/01-struct-layout`](labs/01-struct-layout/)，再用 [`notes/_TEMPLATE.md`](notes/_TEMPLATE.md) 写下第一条记录。
>
> **想知道"网络和操作系统"长什么样？** 直接跑 [`labs/07-net-os`](labs/07-net-os/)：一个真掉血的联机靶场 + 真 socket + 真抓包 + 真反编译。

## ▍ 进度

```text
$ ./status.sh
phase    topic                                state    progress
01-02    cpp-memory / x86-64-asm              RUNNING  [>---------]   labs/01 done
03-04    pe-format / toolchain                TODO     [----------]
05-06    memory-analysis / struct-rebuild     TODO     [----------]
07-08    engine-specialty / unity / unreal    TODO     [----------]
09-10    network-protocol / os                RUNNING  [######>---]   labs/07 done
11-12    anticheat / defense-design           TODO     [----------]
----------------------------------------------------------------------------------
summary  phases 2/6 started   |   labs 3/5 done   |   writeups 1
```

| 周次 | 主题 | 产出 | 状态 |
|:---:|---|---|:---:|
| 1–2 | C++ 内存模型 + x86-64 汇编 | [`labs/01-struct-layout`](labs/01-struct-layout/) ✅ | 🟡 进行中 |\n| 13–15 | 栈帧 · 寄存器 · 标志位 · 操作系统 · 网络 | [`docs/13`](docs/13-stack-frame.md) / [`docs/14`](docs/14-operating-system.md) / [`docs/15`](docs/15-network.md) ✅ | 🟢 已完成 |
| 3–4 | PE 结构 + 逆向工具熟练 | 3 篇 crackme 题解 | ⬜ 未开始 |
| 5–6 | 单机游戏内存分析与结构重建 | 实体结构重建 | ⬜ 未开始 |
| 7–8 | 引擎专项（Unity / Unreal） | 定位一个具体属性字段 | ⬜ 未开始 |
| 9–10 | 网络协议分析 | [`labs/07-net-os`](labs/07-net-os/) ✅ 一次完整请求的结构图 | 🟡 进行中 |
| 11–12 | 反作弊机制研究与防护设计 | 一份防护方案文档 | ⬜ 未开始 |

> 详细拆解见 [`docs/00-roadmap.md`](docs/00-roadmap.md)。

## ▍ 能力模型

四块地基，缺一块都会卡住：

| 模块 | 必须掌握 | 为什么防护需要 |
|:---:|---|---|
| **C / C++** | 指针、内存布局、结构体对齐、虚函数表 | 逆向产物最终要还原成 C++ 语义 |
| **汇编** | x86-64 为主、ARM64 为辅；调用约定、栈帧 | 读反汇编是核心技能 |
| **操作系统** | 进程/线程、虚拟内存、PE 格式、句柄、DLL 加载 | 检测点就布在这些层面 |
| **编译链接** | 导入导出表、重定位、RTTI、异常表 | 定位函数、识别编译器与运行时 |

```mermaid
flowchart LR
    A["C / C++<br>内存布局 · 虚表"] --> E["逆向能力<br>地址 → 结构 → 语义"]
    B["x86-64 汇编<br>栈帧 · 调用约定"] --> E
    C["Windows / PE<br>导入表 · 加载器"] --> E
    D["编译链接<br>RTTI · 异常表"] --> E
    E --> F["防护设计<br>攻击面 · 检测点 · 服务端权威"]
```

## ▍ 原则

| 原则 | 落地方式 |
|:---:|---|
| **合法边界** | 只分析自己开发的、有书面授权的、CTF 靶场的或单机离线的目标；不制作、不分发任何作弊工具。 |
| **可复现** | 结论必须带「怎么做到的」：目标版本、工具、断点、偏移、截图或日志。 |
| **防守优先** | 学攻击是为了找到设防点：每一类攻击面，对应一层可检测的痕迹。 |
| **写下来** | 没有写下来的分析等于没做过；模板见 [`notes/_TEMPLATE.md`](notes/_TEMPLATE.md) 与 [`writeups/_TEMPLATE.md`](writeups/_TEMPLATE.md)。 |

## ▍ 环境

```text
$ ./env.sh
[ok] windows 11 x64
[ok] mingw-w64      gcc 14.2 / gdb / objdump
[ok] ghidra         12.2   (JDK 25)
[ok] x64dbg
[--] ida free
[--] arm64 toolchain
```

主力工具链是 MinGW-w64。Ghidra 已装好，启动：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ghidra.ps1
```

> Ghidra 12.x 要求 **JDK 25+**，本机额外装了 JDK 25；系统里其他项目仍可用 JDK 21。
>
> 上手流程见 [`docs/08-ghidra.md`](docs/08-ghidra.md)，工具全表见 [`docs/03-toolchain.md`](docs/03-toolchain.md)。

---

<div align="center">

<sub><strong>「没有写下来的分析，等于没做过。」</strong></sub><br>
<sub>只分析自己的 · 有书面授权的 · CTF 靶场的 · 单机离线的目标</sub><br>
<sub>不制作、不分发任何作弊工具</sub>

</div>
