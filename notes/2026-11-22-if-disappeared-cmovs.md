# `-O2` 把 `if` 弄哪去了：`cmovs` 取代分支

- **日期**：2026-11-22
- **标签**：#汇编 #优化 #cmov #自测题3

## 现象

同一份源码：

```c
void take_damage(Player *p, int dmg)
{
    p->hp -= dmg;
    if (p->hp < 0)
        p->hp = 0;
}
```

`-O0` 编译出来有**18 条指令**，能看到 `test` + `jns` 分支；`-O2` 只有 **7 条**，**一条跳转都没有**。

## 逐条对照

### `-O0`：控制流清晰

```asm
mov  eax,[rax]        ; eax = p->hp
sub  eax,[rbp+0x18]   ; eax -= dmg
mov  [rdx],eax        ; p->hp = eax
test eax,eax          ; 测试符号位
jns  14000147e        ; ★ 如果 hp >= 0，跳过
mov  [rax],0x0        ; p->hp = 0
```

`jns` 就是那条 `if`。**汇编里能看到"跳"和"不跳"两条路。**

### `-O2`：分支消失

```asm
140001450  sub   DWORD PTR [rcx],edx    ; ① p->hp -= dmg（直接改内存）
140001452  xor   edx,edx                ; ② edx = 0
140001454  mov   eax,DWORD PTR [rcx]    ; ③ eax = p->hp
140001456  test  eax,eax                ; ④ 设置标志位（SF = 符号位）
140001458  cmovs eax,edx                ; ⑤ ★ 若为负，eax = 0
14000145b  mov   DWORD PTR [rcx],eax    ; ⑥ p->hp = eax
14000145d  ret
```

第 ⑤ 条就是那条 `if`。**`cmovs` = conditional move if sign**：

> 如果 SF=1（结果为负），就把 `edx` 搬到 `eax`；否则 `eax` 不变。

然后第 ⑥ 条**无条件**写回。分支没了。

## `test` + `cmovcc` 是 if 的标准形态

`test eax,eax` 做的是 `eax & eax`，不保存结果，只设置标志位：

- `ZF` = 1 表示结果是 0（即 `eax == 0`）
- `SF` = 1 表示最高位是 1（即有符号数**为负**）

`cmovs` 读的就是 `SF`。

### 条件码速查表

| 后缀 | 触发条件 | 有符号 C 语义 | 无符号 C 语义 |
|---|---|---|---|
| `s` | SF=1 | `< 0` | — |
| `ns` | SF=0 | `>= 0` | — |
| `l` / `ge` | SF≠OF / SF=OF | `<` / `>=` | — |
| `g` / `le` | ZF=0 且 SF=OF / ZF=1 或 SF≠OF | `>` / `<=` | — |
| `b` / `ae` | CF=1 / CF=0 | — | `<` / `>=` |
| `a` / `be` | CF=0 且 ZF=0 / CF=1 或 ZF=1 | — | `>` / `<=` |
| `e` / `ne` | ZF=1 / ZF=0 | `==` / `!=` | 同 |

## 第二个例子：`heal` 也是无分支的

```asm
140001460  add   edx,DWORD PTR [rcx]    ; edx = dmg + hp
140001462  mov   eax,DWORD PTR [rcx+0x4]; eax = p->max_hp
140001465  cmp   edx,eax                ; 比较 hp 和 max_hp
140001467  cmovg edx,eax                ; ★ 若 hp > max_hp，edx = max_hp
14000146a  mov   DWORD PTR [rcx],edx    ; p->hp = edx
14000146c  ret
```

`cmovg` = move if greater（有符号 `>`）。对应源码：

```c
if (p->hp > p->max_hp)
    p->hp = p->max_hp;
```

注意 `[rcx+0x4]` 又出现了——**偏移 4 就是 `max_hp`**，和 `-O0` 版完全一致。

## 编译器为什么要这么做

分支预测失败一次要清空流水线，代价约十几到几十个周期。改成 `cmov` 之后：

- 没有分支 → 没有预测失败
- 指令流是直线 → CPU 流水线友好

代价是要**无条件计算两个结果再选一个**。对于 `hp = 0` 这种极廉价的表达式，非常划算。

## 对逆向的三个影响

### 1. 汇编层看不到"if"，但语义还在

`if` 从**控制流**变成了**数据流**。你要读懂它，得看标志位，而不是找跳转。

### 2. Ghidra 能还原回来

反编译器认识 `cmovcc`，所以 `player_O2.exe` 反编译后仍然会显示：

```c
if (eax < 0) {
  eax = 0;
}
```

**汇编和反编译各有优势**：汇编看的是编译器怎么实现，反编译看的是原始意图。

### 3. 优化程度影响分析难度

| 版本 | 指令数 | 分支 | 分析难度 |
|---|---:|---|---|
| `-O0` | 18 | 有 `jns` | 低 |
| `-O2` | 7 | 无 | 中（要看标志位） |

**真实游戏都是 `-O2` 甚至更激进**，所以这种形态必须练熟。

## 结论

1. `if` 不一定编译成跳转，`cmovcc` 是常见替代。
2. 识别模式：`test` / `cmp` 设置标志 → `cmovxx` 选择 → 写回。
3. 缓冲区溢出之外的另一个经典：`cmov` 让分支预测攻击（如 Spectre）有了缓解手段，也是同理。
4. 读 `-O2` 代码时**别找 `jmp`**，先找 `test` / `cmp` 后面跟什么。

## 自测

- [ ] `cmovl` 和 `cmovb` 有什么区别？什么时候用错会出 bug？
- [ ] 如果把源码改成 `if (p->hp < 0) p->hp = -p->hp;`，`-O2` 还会用 `cmov` 吗？
- [ ] `setne al` 和 `cmovne` 分别对应什么样的 C 代码？
