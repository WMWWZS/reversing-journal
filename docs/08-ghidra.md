# 08 · Ghidra 上手

> 装好之后第一次打开 `player_O0.exe` 的完整流程。
> 这一节的目标：让你亲眼看到「汇编 → 伪 C 代码」的转换。

## 环境

| 项 | 值 |
|---|---|
| Ghidra | 12.2（`D:\tools\ghidra\ghidra_12.2_PUBLIC`） |
| JDK | **25**（`D:\tools\jdk-25\jdk-25.0.5+8`） |
| 环境变量 | `GHIDRA_INSTALL_DIR`、`JAVA_HOME` 已指向上面两个目录 |

> ⚠️ Ghidra 12.x **要求 JDK 25 以上**，JDK 21 会直接报「JDK 25+ could not be found」。
> 这就是为什么本机额外装了 JDK 25。

## 启动

**最快的方式：桌面上的 `Ghidra` 图标**（双击即可）。

也可以：

| 方式 | 操作 |
|---|---|
| 桌面快捷方式 | 双击 `Ghidra` |
| 开始菜单 | 搜索 `Ghidra` |
| VS Code | `Ctrl+Shift+P` → `Tasks: Run Task` → `启动 Ghidra` |
| 命令行 | `powershell -ExecutionPolicy Bypass -File .\tools\ghidra.ps1` |

> **为什么快捷方式要多绕一层？**
>
> 快捷方式里的真实命令是：
>
> ```text
> cmd.exe /c start "" "E:\harness\reversing-journal\tools\launch-ghidra.bat"
> ```
>
> 三个原因，每一个都是踩出来的：
>
> 1. **不能直接指向 `ghidraRun.bat`** —— 改过 `JAVA_HOME` 后，**已经运行的资源管理器不会刷新环境变量**，可能仍用旧的 JDK 21 而报错。
> 2. **必须经 `cmd /c start` 分离** —— 否则启动器会占住调用方的控制台句柄，调用方一直挂住不返回。
> 3. **`.bat` 结尾必须 `exit`** —— Windows 资源管理器打开 `.bat` 用的是 **`cmd /K`（不是 `/c`）**，命令跑完会停在提示符，后台白留一个窗口。
>
> 图标来自 `D:\tools\ghidra\ghidra_12.2_PUBLIC\support\ghidra.ico`。

## 第一次分析：player_O0.exe

### 线路 A（省事）：直接打开预置项目

`player_O0.exe` 和 `player_O2.exe` 已经导入并分析好了：

`File` → `Open Project...` → 选

```text
D:\tools\ghidra-projects\learn.gpr
```

打开后左侧 `Symbol Tree` → `Functions` → 双击 `take_damage` 就行。

### 线路 B（推荐）：自己从零走一遍

练手建议走这条。熟悉之后再偷懒。

**1. 建项目**

`File` → `New Project...` → `Non-Shared Project`
→ 目录选 `D:\tools\ghidra-projects`（不存在的话直接在对话框里新建）
→ 项目名 `learn`

**2. 导入文件**

`File` → `Import File...` → 选

```text
E:\harness\reversing-journal\labs\01-struct-layout\player_O0.exe
```

> **找不到文件怎么办？**
> 1. 把上面这个完整路径直接粘贴到文件对话框的「文件名」输入框，回车。
> 2. 这个 `.exe` 是本地编译产物（`*.exe` 被 `.gitignore` 忽略，不会进仓库）。如果真丢了，重跑一次 [`build.ps1`](../labs/01-struct-layout/build.ps1) 就有了。

导入后会弹「Analyze?」→ 选 **Yes**，用默认分析选项即可。

### 3. 等分析完成

底部进度条走完。第一次会慢一点（30 秒到 2 分钟）。

### 4. 找到 take_damage

左侧 `Symbol Tree` → `Functions` → 找 `take_damage`

双击进去，右侧 `Decompiler` 窗口就会显示伪 C 代码。

### 5. 对照三种视图

| 窗口 | 内容 |
|---|---|
| `Listing` | 汇编（和 objdump 看到的一样） |
| `Decompiler` | 伪 C 代码 |
| `Function Graph` | 控制流图（CFG） |

**这一步是本节的核心**：同一个函数，三种表达。你要练的是在它们之间来回翻译。

### 6. 改造成你的理解

- 右键函数名 → `Edit Function Signature`（快捷键 `F`）→ 改成有意义的参数名
- 在函数名上按 `L` 重命名
- 在代码行上按 `;` 加注释
- 对变量右键 → `Rename Variable` → 改成 `hp`、`dmg`

> 改完记得 `Ctrl+S` 保存。Ghidra 会把这些存在项目里。

### 7. 验证偏移

在 `Decompiler` 里能看到类似 `*(int *)(param_1 + 4)` 的表达式。

回去看 [`docs/01-memory-layout.md`](01-memory-layout.md) 的偏移表：

- `+0` → `hp`
- `+4` → `max_hp`
- `+0x14` → `name`

**这就是逆向的全部日常**：从偏移反推结构体。

## 常用快捷键

| 快捷键 | 作用 |
|---|---|
| `G` | 跳转到地址 |
| `L` | 重命名 |
| `T` | 修改数据类型 |
| `;` | 加注释 |
| `Ctrl+E` | 编辑函数签名 |
| `Ctrl+Shift+E` | 打开数据类型管理器 |
| `Alt+←` / `Alt+→` | 前进/后退 |
| `Ctrl+Shift+F` | 全局搜索 |
| `F5` | 刷新反编译窗口 |

## 常见问题

**Q: 打开后反编译窗口是空的？**
A: 等待分析完成，或把光标放在函数体内再按 `F5`。

**Q: 函数名全是 `FUN_140001450`？**
A: 正常。这就是「没有符号」的状态，你的任务就是给它命名。

**Q: 和 objdump 的输出对不上？**
A: Ghidra 显示的是它自己加载后的地址空间。PE 默认基址 `0x140000000`，地址应该一致；如果对不上，用 `G` 跳转核对。

**Q: 能不能分析更大的程序？**
A: 可以，但第一次建议先用这个 20 行的实验程序把流程走顺。真实游戏的分析在 `writeups/` 里记录。

## 无界面反编译（可选）

不想开界面时，可以用 headless 模式配合脚本，直接把伪代码打到控制台：

```powershell
$env:JAVA_HOME = 'D:\tools\jdk-25\jdk-25.0.5+8'
$gh = 'D:\tools\ghidra\ghidra_12.2_PUBLIC'

& "$gh\support\analyzeHeadless.bat" D:\tools\ghidra-projects demo `
  -process player_O0.exe -noanalysis `
  -scriptPath .\tools\ghidra_scripts `
  -postScript DecompileNamed.java take_damage
```

脚本在 [`tools/ghidra_scripts/DecompileNamed.java`](../tools/ghidra_scripts/DecompileNamed.java)，改一下函数名参数就能反编译其他函数。

实测输出（`player_O0.exe`，注意签名是完整的）：

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

> **为什么有 `Player *p` 这种类型名？** 因为编译时加了 `-g`，调试符号还在。
> 换成 `gcc -O2`（无 `-g`）再分析，就只剩 `FUN_140001440` 了。
> 详见 [`notes/2026-11-20-debug-symbols-change-everything.md`](../notes/2026-11-20-debug-symbols-change-everything.md)。

## 下一步

用 Ghidra 打开 [`labs/01-struct-layout/player_O2.exe`](../labs/01-struct-layout/)，对比 `-O0` 和 `-O2` 的反编译结果。

你会看到：**优化后的伪代码反而更难看懂**，因为编译器把中间变量都消掉了。这是逆向里最反直觉的一件事。
