# 02 · 调用约定与栈帧

> 看反汇编的第一步：搞清楚「参数在哪、返回值在哪、栈怎么长」。

## Windows x64 调用约定（Microsoft x64）

### 参数寄存器

| 位置 | 用途 |
|---|---|
| `rcx` | 第 1 个参数（整数/指针） |
| `rdx` | 第 2 个参数 |
| `r8` | 第 3 个参数 |
| `r9` | 第 4 个参数 |
| `[rsp+0x28]` 起 | 第 5 个及以后的参数 |
| `xmm0`–`xmm3` | 前 4 个浮点参数 |
| `rax` | 返回值（整数/指针） |
| `xmm0` | 返回值（浮点） |

### 调用者 / 被调用者保存

- **调用者保存（volatile）**：`rax` `rcx` `rdx` `r8`–`r11`。函数可以随意改，调用者自己负责保存。
- **被调用者保存（non-volatile）**：`rbx` `rbp` `rdi` `rsi` `r12`–`r15` `rsp`。函数必须原样恢复。

> 逆向时看到 `push rbx` / `push r12` 开头的函数，说明它要用这些寄存器，先把它们存起来。

### 影子空间（Shadow Space）

**这是 Windows x64 最容易搞错的地方。**

调用者在 `call` 之前，必须在自己栈顶预留 **32 字节**（`0x20`）空间，给前 4 个参数当「家」。即使参数是用寄存器传的，也要留。

函数入口时的栈布局：

```
[rsp+0x00]  返回地址（call 压进来的）
[rsp+0x08]  第 1 个参数的 home space（对应 rcx）
[rsp+0x10]  第 2 个参数的 home space（对应 rdx）
[rsp+0x18]  第 3 个参数的 home space（对应 r8）
[rsp+0x20]  第 4 个参数的 home space（对应 r9）
[rsp+0x28]  第 5 个参数
```

执行 `push rbp; mov rbp, rsp` 之后：

```
[rbp+0x00]  旧的 rbp
[rbp+0x08]  返回地址
[rbp+0x10]  第 1 个参数的 home space   ← mov [rbp+0x10], rcx
[rbp+0x18]  第 2 个参数的 home space   ← mov [rbp+0x18], edx
```

这就解释了为什么 -O0 的汇编里参数偏移是 `+0x10` 和 `+0x18`。

### 栈对齐

`call` 指令执行后，`rsp` 必须满足 `rsp % 16 == 8`（因为 call 压了 8 字节返回地址，而调用点要求 16 字节对齐）。函数里常见的 `sub rsp, 0x28` 就是为了凑对齐。

## System V AMD64（Linux / macOS）

对比记忆，别搞混：

| | Windows x64 | System V AMD64 |
|---|---|---|
| 整数参数 | `rcx` `rdx` `r8` `r9` | `rdi` `rsi` `rdx` `rcx` `r8` `r9` |
| 浮点参数 | `xmm0`–`xmm3` | `xmm0`–`xmm7` |
| 影子空间 | 有（32 字节） | 无 |
| 红区 | 无 | 有（128 字节） |
| 栈对齐 | `rsp % 16 == 8` | `rsp % 16 == 0` |

> 红区（red zone）：`[rsp-128, rsp)` 这段区域，叶子函数可以直接用，不用先 `sub rsp`。

## ARM64（Android / iOS / 主机）

- 参数：`x0`–`x7`，第 9 个起走栈
- 返回值：`x0`
- 栈指针必须 16 字节对齐
- 没有影子空间

## 常见函数序言模式

### 标准帧指针

```asm
push   rbp
mov    rbp, rsp
sub    rsp, 0x40      ; 分配局部变量空间
...
mov    rsp, rbp
pop    rbp
ret
```

### 省略帧指针（优化后常见）

```asm
sub    rsp, 0x28      ; 直接用 rsp 定位局部变量
...
add    rsp, 0x28
ret
```

这种形态下局部变量通过 `[rsp+0x10]` 这类偏移访问，没有 `rbp` 参照，反推难度更高。

## 识别技巧

| 现象 | 含义 |
|---|---|
| `push rbp; mov rbp,rsp` | 有调试符号或未优化 |
| `sub rsp, 0x28` 开场 | 叶子函数，无帧指针 |
| `push rbx` / `push r12` | 函数使用了被调用者保存寄存器 |
| `mov rax, [rcx]` | 第 1 个参数是个对象指针，读它的首字段 |
| `lea rax, [rip+0x1234]` | 取全局变量 / 字符串地址 |
| `call qword ptr [rax+0x10]` | 虚函数调用（vtable 第 3 项） |
| `cmp; cmov` | 原代码里的三元运算或简单 if |
| `xor eax, eax` | 返回 0 / 置零 |

## 参考

- [Microsoft x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention)
- [System V AMD64 ABI](https://gitlab.com/x86-psABIs/x86-64-ABI)
