# 调试符号能省掉 90% 的工作量

- **日期**：2026-11-20
- **标签**：#ghidra #符号 #编译选项

## 现象

用 Ghidra 12.2 无界面分析 `labs/01-struct-layout/player_O0.exe`，反编译 `take_damage` 得到：

```c
void take_damage(Player *p, int dmg)
{
  int *in_RCX;
  int in_EDX;

  *in_RCX = *in_RCX - in_EDX;
  if (*in_RCX < 0) {
    *in_RCX = 0;
  }
  return;
}
```

**函数名 `take_damage` 和类型 `Player *` 都是原样的**，不是 `FUN_140001450`。

## 第一反应

Ghidra 的反编译这么强？连结构体名都能猜出来？

## 实际原因

不是猜的。`player_O0.exe` 是用 **`gcc -O0 -g`** 编译的，`-g` 把 DWARF 调试信息（函数名、类型名、变量名、行号）编进了可执行文件。Ghidra 直接读了这些符号。

验证：`objdump -h player_O0.exe` 能看到 `.debug_info`、`.debug_line` 等节。

而 `player_O2.exe` 用的是 `gcc -O2`，**没有 `-g`**，所以分析出来就是 `FUN_140001440` 这种无名函数。

## 证据

| 文件 | 编译命令 | Ghidra 里的函数名 |
|---|---|---|
| `player_O0.exe` | `gcc -O0 -g` | `take_damage`（带类型） |
| `player_O2.exe` | `gcc -O2` | `FUN_140001440`（无名） |

另外注意：Ghidra 反编译体里仍然是 `in_RCX` / `in_EDX`，说明 MinGW 的 DWARF 没有让 Ghidra 正确关联参数寄存器，函数签名是从符号表读的，函数体还是按寄存器推的。**符号表和代码逻辑是两套信息，别混为一谈。**

## 结论

1. **调试符号 = 逆向的最大捷径。** 拿到带符号的二进制，工作量直接砍掉九成。
2. 反过来看防护：**发布版必须 strip 符号**（`-s` / `Strip` / `/DEBUG` 关掉），否则等于把源码结构送给对手。
3. 真实游戏的正式版几乎都是剥符号 + 优化过的，所以练手时**要故意编译一份不带 `-g` 的**，模拟真实场景。

## 延伸

- 用 Ghidra 打开 `player_O2.exe`，对比 `player_O0.exe`：一个有名有类型，一个全是 `FUN_xxx`
- 试试 `gcc -O0 -g -s`（加 `-s` 剥离符号）看看结果变成什么样
- 想想：游戏厂商除了 strip，还能怎么防止符号泄露？（提示：混淆、符号重命名、IL2CPP 元数据加密）
