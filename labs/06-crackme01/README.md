# Lab 06 · crackme 01：定位并读懂校验逻辑

对应阶段：[第 3–4 周 · PE 结构与工具熟练](../../docs/00-roadmap.md)
产出目标：`writeups/` 里的第 1 篇 crackme 题解

## 目的

1. 练「从线索定位校验函数」的完整路径：字符串 → 交叉引用 → 反编译
2. 亲手踩一次**假字符串**的坑，建立「字符串只是线索，不是结论」的直觉
3. 读懂「逐字节异或比较」这种最常见的轻量保护，并反推出原文
4. 对比 `-O0 -g` / `-O2` / `-O2 -s` 三个版本，看清符号和优化各自抹掉了什么

## 文件

| 文件 | 作用 |
|---|---|
| `crackme01.c` | 靶子源码。**做完再打开**，提前看等于白做 |
| `build.ps1` | 一键构建三个难度版本 |

## 构建

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

会生成：

| 文件 | 编译命令 | 难度 | 用途 |
|---|---|:---:|---|
| `crackme_O0.exe` | `-O0 -g` | 入门 | 函数名、变量都在，先看清楚逻辑长什么样 |
| `crackme_O2.exe` | `-O2` | 中等 | 优化过、无调试符号，接近真实形态 |
| `crackme_O2s.exe` | `-O2 -s` | 困难 | 符号表被剥掉，只能从入口硬找 |

## 预期输出

```
C:\...\06-crackme01> .\crackme_O2s.exe
key> 12345678
NO

C:\...\06-crackme01> .\crackme_O2s.exe
key> xxxxxxxx
OK
```

那 8 个 `x` 就是你要找的东西。

## 分析路线

### 第 1 步：先看轮廓，别急着读代码

```powershell
objdump -f .\crackme_O2s.exe
objdump -p .\crackme_O2s.exe | Select-String 'DLL Name|fgets|puts|strlen|GetEnvironment'
```

要回答三个问题：

- 入口地址是多少？PE 默认基址是多少？（提示：`0x140000000`）
- 它导入了哪些函数？**哪些是「读输入」的，哪些是「比内容」的？**
- 有没有导入 `GetEnvironmentVariable` 之类的环境变量相关函数？为什么一个 crackme 需要这个？

### 第 2 步：Ghidra 里从字符串摸进去

1. 导入 `crackme_O2s.exe`，分析用默认选项
2. `Search` → `Program Text` 搜 `key>`（也可以直接看 `Defined Strings` 窗口）
3. 找到引用它的函数 → 右键 `References` → `Show References to`
4. 落点就是 `main`

### 第 3 步：读那个比较循环

`Decompiler` 里应该能看到类似这样的结构（变量名是我换过的，你看到的会是 `local_xx`）：

```c
if (strlen(input) == 8) {
    for (i = 0; i < 8; i = i + 1) {
        if ((input[i] ^ K) != table[i]) goto fail;
    }
    success;
}
```

要看清三件事：**长度是多少、异或的常量是多少、比较的表在哪**。

### 第 4 步：逆运算

异或是对称的：`a ^ k ^ k == a`。所以拿到 `K` 和 `table`，把表里的字节按 `^ K` 逐个还原，就是原文。

`table` 在 Ghidra 里是一串没什么意义的字节，不会出现在字符串列表里——因为它不可打印。

> **注意那条 `ghidra42`。** 它是诱饵：只在设置了 `CRACKME_DEBUG` 环境变量时才会被打印，和真正校验的路径毫无关系。真实软件里这种死代码、过期提示、日志文案遍地都是，**顺着字符串找到的东西，必须用交叉引用和上下文验证一遍。**

### 第 5 步：动态验证

拿到答案后，回到 `-O0 -g` 版本用 gdb 单步确认判断点：

```powershell
gdb -q .\crackme_O0.exe
# b verify
# run
# 输入任意值，在比较处看寄存器
```

或者用 x64dbg 载入 `crackme_O2s.exe`，在 `strlen` / `fgets` 返回处下断点往回跟。

## 自测题

1. `.rdata` 里能直接看到 `ghidra42`，为什么它不是答案？你要用什么证据排除它？
2. 真正的密文表在哪个节？为什么它没有出现在 `Defined Strings` 列表里？
3. `-O2` 版本里 `strlen(input) != 8` 这条判断还在吗？编译器可能把它变成了什么？
4. `-O2 -s` 版本里你怎么从入口找到 `main`？写出你实际走的调用链。
5. 如果把源码里的 `^ 0x3F` 换成 `+ 0x3F`，你的还原方法要怎么改？为什么异或比加法「温和」？

## 进阶

- 改源码：把 8 字节表扩成 16 字节，看看定位难度有没有变化
- 把异或换成「前一个字节参与运算」的滚动式混淆，再逆一次
- 用 `tools/ghidra_scripts/DecompileNamed.java` 把这题的分析过程做一次 headless 复现，贴进题解当证据
- 写题解：套用 [`writeups/_TEMPLATE.md`](../../writeups/_TEMPLATE.md)，结论必须能指回「地址 → 证据」
