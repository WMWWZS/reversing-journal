# reversing-journal

Windows 进程内存与二进制分析的动手实验日志。

每个实验只回答一个问题，全部在**自建靶子**上跑：自己写的 C 程序、自己定的结构体、
自己编译的 `-no-pie` 可执行文件。这样每一步都能被 `offsetof`、十六进制 dump、
以及两个互相自洽的数字验证一遍——**没有证据的结论，本仓库不算结论。**

## 这个仓库在练什么

调试器（x64dbg、WinDbg、Cheat Engine）能做的事，拆开来看只有几个原语：

```
按名字找到进程  ->  打开它  ->  拿到模块基址  ->  在内存里定位某个字段  ->  读 / 写它
```

本仓库就是把这条链子一节一节手写一遍，搞清楚每一步的 API、权限、失败原因，
以及"为什么写死的地址活不过一次重新编译"。

## 实验目录

| 目录 | 回答的问题 | 关键结论 |
|---|---|---|
| [labs/00-memory-model](labs/00-memory-model) | 地址 / 偏移 / 字节 / 跨进程读，到底是什么 | 地基五步：编号格、偏移是加法、float 4 字节低位在前、`ReadProcessMemory` = memcpy + handle、指针是装地址的格子 |
| [labs/01-struct-layout](labs/01-struct-layout) | `+0x10` 这个偏移是怎么来的 | 偏移 = 字段顺序 + 字段大小 + 填充；用 `offsetof` 验证，不靠记 |
| [labs/02-find-offset](labs/02-find-offset) | 没有源码，怎么把偏移挖出来 | 整映像扫描 + 五条互相印证的证据链；`hp = +0x10`、`sizeof = 0x28` |
| [labs/03-write-memory](labs/03-write-memory) | 怎么跨进程写一个值，并且让它留住 | `WriteProcessMemory` 三件套权限；特征扫描优于写死偏移；数值总被重算就得循环写 |
| [labs/04-pointer-chain](labs/04-pointer-chain) | 地址每次启动都变，怎么还能找到字段 | 指针 = 存着地址的 8 字节格子；链 = 每跳读 8 字节 + 加偏移；**必须扫全进程内存**，只扫模块永远找不到 |
| [labs/05-aob-scan](labs/05-aob-scan) | 游戏更新把偏移打废了怎么办 | 找访问该全局的指令（`48 8B 05 disp32`），`目标 = 指令 + 7 + disp`；候选必须验证链能走通 |
| [labs/06-inject](labs/06-inject) | 怎么让目标进程自己改自己 | `VirtualAllocEx` + `WriteProcessMemory` + `CreateRemoteThread`；机器码里地址只能放非易失寄存器，`call` 前必须对齐栈 |

## 环境

- Windows 10/11 x64
- MinGW-w64 gcc 14.2.0（`gcc --version` 能打印即可）
- 每个实验目录里有 `build.bat`（只编译）和 `run.bat`（编译 + 运行）
- 代码注释和 `printf` 都用中文；**编译必须加 `-fexec-charset=GBK`**，否则 UTF-8 字面量在 936 控制台会乱码
- `.bat` 一律纯 ASCII + CRLF 换行：cmd 按 OEM 码页读批处理，塞中文会把后面的字符一起吞掉

## 靶场

自己写的靶子只够练到指针。真实程序在 [TARGETS.md](TARGETS.md)：从 Cheat Engine 自带教程、
开源单机游戏（AssaultCube / OpenTTD / Xonotic）一路到商业单机，附选择标准与记录模板。

## 守则

1. **注释是承诺，编译产物是事实。** 两者不一致时，以程序打印的地址为准，并立刻改注释。
2. **一个数字叫猜测，五条互相印证才叫证据。**
3. **先跑通，再谈抽象。** 每个实验都先给出可运行的输出，再解释为什么。
4. 靶子自己写、自己编、自己跑。实验里出现的每一个地址，都能在本机复现。
