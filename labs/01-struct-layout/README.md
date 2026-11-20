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

1. `Player` 里 `hp` 的偏移是 0，`max_hp` 是 4。如果把 `hp` 改成 `short`（2 字节），`max_hp` 的偏移会变成多少？**先预测，再跑 `offsets.exe` 验证。**
2. 在 `heal` 的反汇编里，`[rax+0x4]` 对应源码里的哪个字段？
3. `take_damage` 的 -O2 版本只有 7 条指令，`if` 语句去哪了？
4. gdb 断点下在函数入口时读到 `p = 0x2`，为什么是错的？下在哪里才对？

## 进阶

- 在结构体里加一个 `double crit_rate;`，看对齐规则怎么变化
- 加一个内部结构体成员（嵌套），看偏移怎么算
- 用 `#pragma pack(1)` 强制紧凑排列，对比大小变化
