# 11 · 标志位：CPU 的「上一次运算体检报告」

> 上一篇讲寄存器是「放数据的抽屉」。标志位不是数据，是**上一次运算留下的判断依据**。

## 为什么需要标志位

看这段汇编：

```asm
test eax,eax
jns  somewhere
```

问题来了：

- `test eax,eax` **没有产生任何结果**——它没把结果存到哪个寄存器里
- 那 `jns` 是根据什么决定跳不跳的？

答案：`test` 把**结果的性质**（是不是 0、是不是负数）记在了一组叫**标志位**的地方，`jns` 去读那个性质。

## 本质：RFLAGS 是一个"位寄存器"

CPU 里有一个特殊寄存器叫 **RFLAGS**（64 位，低 32 位叫 EFLAGS）。它不存数值，**每一位（bit）是一个独立的开关**，表示一种状态：

| 位 | 标志 | 名称 | 什么时候 = 1 | 逆向里用来判断 |
|:---:|---|---|---|---|
| 0 | **CF** | 进位标志 | 加法产生进位 / 减法产生借位 | **无符号**比较大小 |
| 6 | **ZF** | 零标志 | 运算结果等于 0 | `==` |
| 7 | **SF** | 符号标志 | 结果最高位是 1（即有符号数**为负**） | `< 0` |
| 11 | **OF** | 溢出标志 | 有符号运算结果溢出 | 有符号大小比较 |
| 2 | PF | 奇偶标志 | 结果低字节里 1 的个数为偶 | 很少用 |
| 4 | AF | 辅助进位 | BCD 运算用 | 基本不用 |

**逆向只需要盯住 CF / ZF / SF / OF 四个。**

## 最关键的两个指令：`test` 和 `cmp`

它们**都不产生数据结果，只设置标志位**。这是新手最容易懵的地方。

### `test a, b` = 计算 `a & b`，丢弃结果，只留标志

```asm
test eax,eax        ; 等价于 eax & eax，不影响 eax 的值
```

`eax & eax` 的值就是 `eax` 本身，所以：

| 结果 | 标志 |
|---|---|
| `eax == 0` | `ZF = 1` |
| `eax` 最高位是 1（负数） | `SF = 1` |

> **`test eax,eax` 是「这个值是不是 0 / 是不是负数」的标准写法**，你在反汇编里会看到无数次。

### `cmp a, b` = 计算 `a - b`，丢弃差值，只留标志

```asm
cmp edx, eax        ; 计算 edx - eax，但不保存
```

| 关系 | 标志 |
|---|---|
| `edx == eax` | `ZF = 1` |
| `edx < eax`（有符号） | `SF ≠ OF` |
| `edx < eax`（无符号） | `CF = 1` |

## 标志位怎么被使用

三个家族，全靠标志位工作：

| 家族 | 作用 | 例子 |
|---|---|---|
| **`jcc`** | 条件跳转 | `je` `jne` `js` `jns` `jl` `jg` `jb` `ja` |
| **`cmovcc`** | 条件传送 | `cmovs` `cmovg` `cmove` `cmovne` |
| **`setcc`** | 条件置 1 字节 | `sete` `setne` `setl` `setg` |

`cc` = condition code（条件码），就是下面这些后缀。

### 条件码速查表

| 后缀 | 全称 | 触发条件 | C 语义 |
|---|---|---|---|
| `e` / `z` | equal / zero | ZF=1 | `a == b` |
| `ne` / `nz` | not equal | ZF=0 | `a != b` |
| `s` | sign | SF=1 | `< 0` |
| `ns` | not sign | SF=0 | `>= 0` |
| `l` | less | SF≠OF | `a < b`（有符号） |
| `ge` | greater or equal | SF=OF | `a >= b`（有符号） |
| `g` | greater | ZF=0 且 SF=OF | `a > b`（有符号） |
| `le` | less or equal | ZF=1 或 SF≠OF | `a <= b`（有符号） |
| `b` | below | CF=1 | `a < b`（**无符号**） |
| `ae` | above or equal | CF=0 | `a >= b`（无符号） |
| `a` | above | CF=0 且 ZF=0 | `a > b`（无符号） |
| `be` | below or equal | CF=1 或 ZF=1 | `a <= b`（无符号） |

> **有符号和无符号用了两套后缀**：`l/g` 是有符号，`b/a` 是无符号。搞混了会出安全漏洞——这是 C 语言 `signed/unsigned` 问题的机器码来源。

## 回到你的代码

### 例 1：`-O0` 版 `take_damage`

```asm
test eax,eax        ; eax = hp - dmg。若 hp<0 则 SF=1
jns  14000147e      ; SF=0（即 hp >= 0）→ 跳过下面
mov  [rax],0x0      ; hp = 0
```

`jns` = jump if not sign。**它读的就是 SF。**

### 例 2：`-O2` 版 `take_damage`

```asm
test  eax,eax       ; SF = (hp < 0)
cmovs eax,edx       ; SF=1 → eax = 0
```

`cmovs` = conditional move if sign。**同样读 SF，只是"搬数据"而不是"跳转"。**

### 例 3：`-O2` 版 `heal`

```asm
cmp   edx,eax       ; 计算 edx - eax，设标志
cmovg edx,eax       ; ZF=0 且 SF=OF（即 edx > eax）→ edx = eax
```

`cmovg` 读的是 **ZF 和 OF 的组合**，判断的是有符号数的大小关系。

## 三条容易踩的坑

### 坑 1：标志位不在通用寄存器里

你不能 `mov eax, flags`。要看它，得用专门的指令：

```asm
pushfq      ; 把 RFLAGS 压栈
pop  rax    ; 弹到 rax 里看
```

在调试器里就简单了：gdb 里 `info registers eflags`，x64dbg 右下角直接显示。

### 坑 2：不是所有指令都改标志位

| 改标志位 | 不改标志位 |
|---|---|
| `add` `sub` `and` `or` `xor` `test` `cmp` `shl` `shr` | **`mov`** **`lea`** `push` `pop` `call` `ret` |

**`mov` 不设置任何标志位**。所以 `mov eax, 0` 之后，标志位还是上一条指令留下的值——这一点经常导致调试时误判。

### 坑 3：`SF` 和 `CF` 是两套世界观

| | 看的是 | 适合 |
|---|---|---|
| `SF` / `OF` | 最高位 / 有符号溢出 | 有符号数 |
| `CF` | 无符号借位/进位 | 无符号数 |

同一个 `0xFF` 和 `0x01` 相比：

- 按有符号（`0xFF` = -1）→ -1 < 1
- 按无符号（`0xFF` = 255）→ 255 > 1

**同一组字节，标志位告诉你"按哪种世界观比的大小"。**

## 在调试器里观察

gdb：

```text
(gdb) info registers eflags
eflags         0x202    [ IF ]
```

或者单看标志：

```text
(gdb) p $eflags
```

下断点在 `test` 之后、`jns` 之前，单步执行，观察 `eflags` 的变化——这是理解标志位最快的方法。

## 记忆清单

1. **标志位 = 上一条指令留下的性质**，不是数据。
2. `test a,b` 和 `cmp a,b` **不产生结果**，只设置标志。
3. `ZF` 看是不是 0，`SF` 看是不是负数，`CF` 看无符号进位，`OF` 看有符号溢出。
4. `jcc` / `cmovcc` / `setcc` 都靠标志位工作。
5. **`mov` 不改标志位。**

## 自测

**1. `test eax,eax` 之后 `ZF=1`，说明 `eax` 是多少？**

- 考点：ZF 的含义
- 应明白：结果（`eax & eax`）为 0 → `eax` 自己就是 0
- 算会：你能说出"这里在判断 eax 是不是 0"，而不是"test 算了个什么东西"

**2. `cmp eax, ebx` 之后 `CF=1`，按无符号看谁大？**

- 考点：CF 表示无符号借位
- 应明白：`eax - ebx` 借位了，说明 `eax < ebx`
- 算会：你能区分"CF 借位"和"SF 负数"是两回事

**3. 为什么 `mov` 之后标志位还不变？**

- 考点：哪些指令会写标志位
- 应明白：`mov` 只是搬数据，没有"运算"，自然没有可判断的性质
- 算会：你能解释"为什么调试时在 `mov` 后看标志位会看到旧值"
