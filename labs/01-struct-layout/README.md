# Lab 01 · 结构体布局与偏移

对应笔记：[docs/01-memory-layout.md](../../docs/01-memory-layout.md)

## 目的

1. 亲眼看到结构体在内存里怎么排布
2. 把源码 → 汇编 → 内存三者对应起来
3. 理解为什么 `[rax+0x4]` 就是某个字段

## 文件

| 文件 | 作用 |
|---|---|
| `player.h` | 结构体定义（本 lab 的主角） |
| `player.c` | 受击、治疗逻辑 + main |
| `offsets.c` | 让编译器报告每个字段的偏移 |
| `build.ps1` | 一键构建 |
| `demo.gdb` | gdb 调试脚本 |

## 构建

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

会生成：

- `player_O0.exe` —— 未优化，汇编最适合入门
- `player_O2.exe` —— 优化后，真实游戏的形态
- `offsets.exe` —— 打印结构体布局

## 预期输出

### offsets.exe

```
sizeof(Player) = 40  (0x28)
hp      offset =  0  (0x00)
max_hp  offset =  4  (0x04)
x       offset =  8  (0x08)
y       offset = 12  (0x0c)
z       offset = 16  (0x10)
name    offset = 20  (0x14)
flags   offset = 36  (0x24)
```

### player_O0.exe

```
after hit : hp=70
after heal: hp=100
```

### 反汇编

```powershell
objdump -d -M intel --no-show-raw-insn player_O0.exe | Select-String '<take_damage>:' -Context 0,22
objdump -d -M intel --no-show-raw-insn player_O2.exe | Select-String '<take_damage>:' -Context 0,18
```

### gdb

```powershell
gdb -q -batch -x demo.gdb player_O0.exe
```

## 自测题

> 每道题都写清三件事：**考点** · **做完你应该明白什么** · **怎么算真的会了**。
> 先只看题干自己想，答完再对照。

### 自测 1：`hp` 改成 `short`，`max_hp` 的偏移会变成多少？

**考点**：结构体对齐与填充（padding）

**做完你应该明白**
- 字段偏移 **不等于** 前面字段大小之和
- 编译器会插入看不见的填充字节，让每个字段满足自己的对齐要求
- 改一个字段的类型，可能连带改变它**后面所有**字段的偏移

**怎么算真的会了**
你能说出「33 不是 4 的倍数，所以补齐到 36」，而不是只背出 36 这个数字。

**常见错误**
- 算成 33 —— 忘了 4 字节对齐
- 算成 34 —— 把 `short` 的对齐当成了 `int` 的
- 以为只影响 `max_hp` —— 实际上 `name`、`flags` 全都会跟着移位

---

### 自测 2：`heal` 里 `[rax+0x4]` 对应哪个字段？

**考点**：汇编偏移 ↔ 结构体字段的映射

**做完你应该明白**
- 方括号里的数字就是**字节偏移**
- 偏移 0 是第一个字段，偏移 4 是第二个 `int` 字段
- 「读某个偏移」= 「访问某个字段」，即使不知道字段名也能确认它存在

**怎么算真的会了**
给你任意一条 `mov eax, [rcx+0x14]`，你能立刻说出「读第 6 个字段 / `name` 的开头」。

---

### 自测 3：`-O2` 版本里 `if` 去哪了？

**考点**：分支消除（`cmov` 条件传送）

**做完你应该明白**
- `if` 不一定变成跳转，可能变成 `cmovxx` 这种「选择」
- `-O0` 和 `-O2` 指令形态差别巨大，但**语义完全一样**
- 逆向时不能只找 `jmp`，要会读标志位（`ZF` / `SF` / `CF`）

**怎么算真的会了**
看到 `test` 后面跟 `cmovs`，能说出它等价于 `if (x < 0) ...`。

详细版：[notes/2026-11-22-if-disappeared-cmovs.md](../../notes/2026-11-22-if-disappeared-cmovs.md)

---

### 自测 4：gdb 断点下在函数入口，为什么 `p = 0x2`？

**考点**：函数序言（prologue）与调试信息的时序

**做完你应该明白**
- 断点命中时，函数的第一条指令**还没执行**
- 这时按调试信息去读参数位置，读到的是垃圾值
- 断点应该下在「参数已经被存到栈上」之后的那一行

**怎么算真的会了**
你能解释「为什么换一行下断点就好了」，而不是死记「不要下在入口」。

## 进阶

- 在结构体里加一个 `double crit_rate;`，看对齐规则怎么变化
- 加一个内部结构体成员（嵌套），看偏移怎么算
- 用 `#pragma pack(1)` 强制紧凑排列，对比大小变化
