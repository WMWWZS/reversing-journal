# 03 · 工具链

> 工具是消耗品，方法论才是资产。但工具有手感，手感靠练。

## 静态分析

| 工具 | 定位 | 备注 |
|---|---|---|
| **IDA Pro** | 行业标准 | 反编译质量最好，商业授权；免费版无 x64 反编译 |
| **Ghidra** | 开源首选 | NSA 出品，需要 JDK 21，支持协作与脚本 |
| **Binary Ninja** | 脚本友好 | API 设计好，适合写自动化分析 |
| **Cutter / Rizin** | 开源替代 | 基于 radare2 生态 |
| **Detect It Easy** | 侦察 | 识别编译器、加壳器、保护壳，第一步必用 |
| **PE-bear / CFF Explorer** | PE 结构 | 看节表、导入导出、重定位 |
| **dogbolt.org** | 在线对比 | 同一文件跑多个反编译器，新手很友好 |

## 动态分析

| 工具 | 平台 | 用途 |
|---|---|---|
| **x64dbg** | Windows | 用户态调试主力，插件生态好 |
| **WinDbg Preview** | Windows | 内核调试必备 |
| **GDB / LLDB** | 跨平台 | Linux / macOS / Android / 嵌入式 |
| **Frida** | 跨平台 | 动态插桩：Hook 函数、改参数、抓调用栈 |
| **Cheat Engine** | Windows | 内存扫描；理解它才能防它 |
| **ReClass.NET** | Windows | 从内存反推结构体布局 |
| **Process Hacker / System Informer** | Windows | 进程、句柄、模块、线程、内存区域 |
| **API Monitor** | Windows | 监控 API 调用参数与返回值 |
| **Wireshark / mitmproxy** | 跨平台 | 协议层分析 |

## 引擎 / 运行时专项

| 目标 | 工具 |
|---|---|
| Unity **Mono** | dnSpy、ILSpy（直接读 C#，几乎无门槛） |
| Unity **IL2CPP** | Il2CppDumper、Il2CppInspector、Cpp2IL |
| Unreal Engine | UE4SS、FModel、SDK dump 类工具 |
| .NET 通用 | dnSpy、de4dot（去混淆） |
| Lua 脚本 | 对应版本的 luac 反编译工具 |

## 我的当前环境

```
编译器   MinGW-w64 GCC        gcc / g++ / gdb / objdump  ✅
版本控制 git 2.5x                                          ✅
待补     x64dbg                                            ⬜
待补     Ghidra 或 IDA Free                                ⬜
待补     Detect It Easy                                    ⬜
待补     ReClass.NET                                       ⬜
```

## 安装建议顺序

1. **Ghidra** 或 **IDA Free** —— 先把反编译看起来（需要 JDK 21）
2. **x64dbg** —— Windows 动态调试
3. **Detect It Easy** —— 拿到文件先判断是什么
4. **ReClass.NET** —— 重建结构体时才用

> 不要一次装十个工具然后一个都不会用。**装一个，用它做完一个题，再装下一个。**

## 常用命令备忘

### objdump

```bash
# 反汇编，Intel 语法，不显示机器码
objdump -d -M intel --no-show-raw-insn target.exe

# 只看某个函数
objdump -d -M intel --no-show-raw-insn target.exe | sed -n '/<my_func>:/,/^$/p'

# 节表
objdump -h target.exe

# 导入表
objdump -p target.exe | sed -n '/The Import Tables/,/^$/p'
```

### gdb

```bash
gdb -q -batch -x script.gdb target.exe

# 常用命令
break func_name          # 函数断点
break file.c:12          # 行断点
info registers           # 看所有寄存器
x/8gx $rsp               # 看栈
x/4dw $rcx               # 按 4 字节看参数指向的内存
p *ptr                   # 解引用结构体
finish                   # 执行到当前函数返回
backtrace                # 调用栈
```

### x64dbg（待补）

```
F2   下断点
F7   单步进入
F8   单步跳过
F9   运行
Ctrl+G 跳转到地址/表达式
```

## 编辑器怎么选（写作工具 ≠ 逆向工具）

先分清三件事，很多人一开始就混淆：

| 你要做的事 | 用什么 |
|---|---|
| 写 lab 代码、做笔记 | **VS Code** |
| 读反汇编、还原逻辑 | **Ghidra / IDA**（这才是逆向的「IDE」） |
| 运行时调试 | **x64dbg / gdb** |

### 本仓库的选择：VS Code

`.vscode/` 里已配好，克隆下来开箱即用：

| 功能 | 快捷键 | 说明 |
|---|---|---|
| 构建当前实验 | `Ctrl+Shift+B` | 运行当前文件所在目录的 `build.ps1` |
| 编译当前文件 -O0 | 任务面板 | 生成 `xxx_O0.exe` |
| 编译当前文件 -O2 | 任务面板 | 生成 `xxx_O2.exe`，用于对比优化差异 |
| objdump 反汇编 | 任务面板 | 直接在输出面板看 |
| 调试 | `F5` | 接入 gdb，可下断点、看寄存器、看调用栈 |

> 任务面板：`Ctrl+Shift+P` → `Tasks: Run Task`

### 为什么不用 Visual Studio

- 本机的 VS 只装了 IDE，**没装 C++ 工作负载**（找不到 `cl.exe`），要用得先装 3–6 GB
- VS 的强项是大型工程；这个阶段是单文件实验，太重
- **但后续建议补装**：Windows 游戏绝大多数是 MSVC 编译的。等学到 PE 结构时，用 MSVC 编译同一份代码再对比反汇编，能直接看到两种编译器的差异——这是极好的学习材料

### 为什么不用 Cursor

本机也装了 Cursor（VS Code 套壳，扩展通用）。写笔记可以，但**读汇编时建议关掉 AI 补全**——它会直接给答案，跳过最重要的思考过程。逆向的眼力是练出来的，不是查出来的。

### 关于「IDE」这个词

严格说，逆向领域没有一个统称的 IDE。分工具链看：

```
VS Code        → 写代码
Ghidra / IDA   → 读代码（静态逆向）
x64dbg / gdb   → 跑代码（动态调试）
```

三者都要有，且不能互相替代。VS Code 再强也看不到反编译后的伪代码。

## 学习资源

- [crackmes.one](https://crackmes.one) —— 分难度的逆向靶场，**最推荐的起点**
- [pwn.college](https://pwn.college) —— 系统化二进制安全课程
- [ROP Emporium](https://ropemporium.com) —— 栈与调用约定实战
- [dogbolt.org](https://dogbolt.org) —— 在线多反编译器对比
