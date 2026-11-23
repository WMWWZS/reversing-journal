# 09 · PE 格式：从磁盘到内存

> R3（第 7–9 周）的核心。看不懂 PE，你在 Ghidra 里就只是「翻函数」，而不是「理解一个程序」。
>
> 本文所有数字都取自本仓库的 [`labs/06-crackme01/crackme_O2s.exe`](../labs/06-crackme01/)，可以逐条复现。

## 为什么要先学这个

逆向时你手上只有一堆字节。PE 头回答的是**「这堆字节里，哪部分是指令、哪部分是数据、程序从哪开始跑」**，没有它，反汇编器连第一行都摆不对。

反作弊也在这层做文章：导入表能看出程序调了哪些敏感 API，节表能看出有没有加壳（节名异常、可写可执行、大小不匹配），TLS 回调能在 `main` 之前执行代码。

## 一、三个地址，别混

这是新手第一个坑，也是后面所有定位工作的基础。

| 名字 | 含义 | 谁在用 |
|---|---|---|
| **FOA**（文件偏移） | 字节在 `.exe` 文件里的位置 | 十六进制编辑器、`objdump -s` 的偏移 |
| **RVA**（相对虚拟地址） | 相对镜像基址的偏移 | PE 头里所有地址字段、节表 |
| **VA**（虚拟地址） | 进程里真实地址 = `ImageBase + RVA` | 调试器、Ghidra 的 Listing |

本靶子的实际值：

```
ImageBase            0x140000000
AddressOfEntryPoint  0x13f0          ← 这是 RVA
所以入口 VA  =       0x1400013F0     ← 这是 objdump -f 报的 start address
```

从 RVA 换 FOA，要查节表。`objdump -h` 输出（节选）：

```
Idx Name      Size      VMA               LMA               File off
  0 .text     00001af0  0000000140001000  0000000140001000  00000400
  2 .rdata    00000638  0000000140004000  0000000140004000  00002200
  6 .idata    000009e0  0000000140008000  0000000140008000  00003000
```

`.text` 的 RVA 是 `0x1000`、FOA 是 `0x400`，差 `0xC00`。所以：

```
入口 RVA 0x13F0 → FOA = 0x13F0 - 0x1000 + 0x400 = 0x7F0
```

**去十六进制编辑器跳 `0x7F0`，看到的就是入口那几条指令。** 跳 `0x13F0` 会落到别的地方——这就是「地址对不上」的经典原因。

> `SectionAlignment` 是 `0x1000`（内存按页），`FileAlignment` 是 `0x200`（磁盘按扇区）。两个对齐粒度不同，是 RVA 和 FOA 必须换算的根本原因。

## 二、文件骨架

```
DOS 头(64B)  →  NT 头  →  节表  →  .text .data .rdata .idata ...
```

### DOS 头

只有两个字段重要：

- 前两字节 `4D 5A` = `MZ`，确认是 Windows 可执行文件
- `e_lfanew`（偏移 `0x3C`，4 字节）＝ NT 头的位置。**永远不要假设 NT 头固定在 `0x40`**，必须读这个字段跳过去

### NT 头

签名 `PE\0\0` 之后是 `IMAGE_FILE_HEADER`（20 字节）和 `IMAGE_OPTIONAL_HEADER`（PE32+ 是 240 字节）。

必背字段：

| 字段 | 本靶子的值 | 意义 |
|---|---|---|
| `Magic` | `0x20B` | PE32+（64 位）；`0x10B` 是 PE32 |
| `Machine` | `0x8664` | x86-64 |
| `NumberOfSections` | `9` | 节表有几项 |
| `AddressOfEntryPoint` | `0x13F0` | 入口 RVA，注意不是 `main` |
| `ImageBase` | `0x140000000` | 默认装载地址（有 ASLR 时会变） |
| `SizeOfImage` | `0xB000` | 装载后占多大虚拟内存 |
| `Subsystem` | `3` | 控制台程序；`2` 是 GUI |

`Characteristics` 位里藏着很重要的信息，`objdump -p` 会直接翻译出来：

```
executable
line numbers stripped
symbols stripped          ← 用了 -s
debugging information removed
```

> 看到 `symbols stripped` 就明白了：这题没有捷径，全靠读逻辑。

### 节表

每项 40 字节，关键字段：`Name`（8 字节，**不保证以 `\0` 结尾**）、`VirtualSize`、`VirtualAddress`（RVA）、`SizeOfRawData`、`PointerToRawData`（FOA）、`Characteristics`。

本靶子的 9 个节，各自的职责：

| 节 | RVA | 内容 | 关键属性 |
|---|---|---|---|
| `.text` | `0x1000` | 代码 | 可执行、只读 |
| `.data` | `0x3000` | 已初始化全局变量 | 可读写 |
| `.rdata` | `0x4000` | 只读数据：字符串、常量表 | 只读 |
| `.pdata` / `.xdata` | `0x5000` / `0x6000` | 异常处理表（x64 SEH） | 只读 |
| `.bss` | `0x7000` | 未初始化数据 | 只有 `ALLOC`，**文件里不占空间** |
| `.idata` | `0x8000` | 导入表 + IAT | 只读 |
| `.tls` | `0x9000` | 线程局部存储 | 可读写 |
| `.reloc` | `0xA000` | 重定位表 | 只读 |

**`.rdata` 是你找字符串和密文表的地方，`.text` 是你找逻辑的地方，`.idata` 是你找「它用了什么」的地方。**

加壳的特征也在这张表里：节名变成 `UPX0`/`UPX1`、`.text` 变得巨大、出现「可写又可执行」的节（`WX`）、`SizeOfRawData` 远小于 `VirtualSize`。看到这些组合，先脱壳再谈分析。

### 数据目录

`OptionalHeader` 末尾是 16 项 `DataDirectory`（每项 8 字节：RVA + Size）。常用的几个：

| 索引 | 名字 | 用途 |
|:---:|---|---|
| 0 | 导出表 | DLL 对外提供什么函数 |
| 1 | 导入表 | 它用了哪些外部函数 |
| 2 | 资源 | 图标、字符串表、版本信息 |
| 3 | 异常表 | x64 展开信息，对应 `.pdata` |
| 5 | 重定位表 | ASLR 需要修的地址 |
| 12 | **IAT** | 导入地址表（下面细讲） |
| 13 | 延迟导入 | 首次调用才解析 |

索引 12 单独列出来是因为它和索引 1 不是一回事：**1 是「清单」，12 是「装好地址的槽位」**。

## 三、导入表与 IAT：反作弊的主战场

这是全篇最该记住的一节。

程序调用 `fgets` 时，它并不知道 `fgets` 在 `api-ms-win-crt-stdio-l1-1-0.dll` 里的地址——那是装载时才确定的。于是 PE 设计了间接层：

```
你的代码 ──call qword ptr [rip+0x2xxx]──►  IAT 槽位  ──►  fgets 真实地址
                                          (装载器填的)
```

反汇编里你看到的是 `call QWORD PTR [0x140008xxx]`——**方括号里的地址就是 IAT 槽**，方括号表示「取这个地址里存的值当跳转目标」。

`objdump -p` 能直接列出这张清单：

```
DLL Name: api-ms-win-crt-stdio-l1-1-0.dll
  00008420  <none>  0004  __stdio_common_vfprintf
  00008430  <none>  009a  fgets
  00008440  <none>  00b3  puts
DLL Name: api-ms-win-crt-string-l1-1-0.dll
  00008458  <none>  00a8  strlen
```

### 结构

`.idata` 里是一串 `IMAGE_IMPORT_DESCRIPTOR`（20 字节一项，全 0 结束）：

```
OriginalFirstThunk (ILT RVA)   ── 函数名数组（按名字查）
TimeDateStamp
ForwarderChain
Name               (DLL 名 RVA)
FirstThunk         (IAT RVA)   ── 函数地址数组（装载后填真实地址）
```

**ILT 存名字，IAT 存地址。** 装载器拿 ILT 去 DLL 的导出表里查，查到就写进 IAT 对应槽位。所以磁盘上的 IAT 和 ILT 长得一样，内存里的 IAT 已经变成一堆真实地址了。

### 为什么这决定了 Hook 的位置

| 手法 | 改哪里 | 反作弊怎么查 |
|---|---|---|
| IAT Hook | 改 IAT 槽，指向自己的函数 | 校验 IAT 槽是否落在目标模块的地址范围内 |
| Inline Hook | 改函数开头 5 字节为 `jmp` | 校验函数首字节、比对内存与磁盘映像 |
| SSDT / 内核 Hook | 改内核函数表 | PatchGuard、内核完整性校验 |

**你 `docs/07-anticheat.md` 里那五层防护，第一层的技术依据就是这张表。** 学攻击面的目的是知道检测点在哪——这一点在本仓库的原则里已经写死了。

## 四、实战：从入口找到 main

`-s` 剥掉符号后，Ghidra 给你的是 `entry`，不是 `main`。标准路径：

```
entry
 └─ __mingw_invalidParameterHandler? / __tmainCRTStartup
     └─ __main
         └─ main
```

Ghidra 里的操作：

1. `G` 跳 `0x1400013F0`
2. 看 `entry` 里的 `call`——通常是 `__tmainCRTStartup`
3. 跟着往下，找**带三个参数 `(argc, argv, envp)` 的那个 call**，那就是 `main`
4. 找到后第一件事：`L` 重命名，别让它继续叫 `FUN_xxxx`

命令行等价操作：

```powershell
objdump -f .\crackme_O2s.exe                              # 入口在哪
objdump -h .\crackme_O2s.exe                              # 节表：RVA / FOA
objdump -p .\crackme_O2s.exe                              # 导入表、ImageBase
objdump -d -M intel --no-show-raw-insn .\crackme_O2s.exe   # 反汇编
objdump -s -j .rdata .\crackme_O2s.exe                     # 看只读数据（字符串、密文表）
```

## 五、自测

1. `AddressOfEntryPoint` 是 `0x13F0`，为什么 `objdump -f` 报的却是 `0x1400013F0`？
2. `.bss` 为什么在文件里不占空间？运行时它占的内存从哪来？
3. 反汇编里出现 `call QWORD PTR [0x140008420]`，这个地址在哪个节？它指向的最终目标由谁决定？
4. 一个样本的 `.text` 节 `VirtualSize = 0x80000` 但 `SizeOfRawData = 0x400`，你第一反应是什么？
5. 如果程序开了 ASLR，`ImageBase` 还会是 `0x140000000` 吗？那 `0x1400013F0` 这个 VA 还有意义吗？

## 参考

- 微软文档：[PE Format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
- 工具：`dumpbin /headers`（MSVC）、`objdump -p`（MinGW）、PE-bear、DIE（查壳）
- 关联：[03 · 工具链](03-toolchain.md)、[07 · 反作弊](07-anticheat.md)、[08 · Ghidra 上手](08-ghidra.md)
