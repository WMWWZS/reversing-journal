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

## 学习资源

- [crackmes.one](https://crackmes.one) —— 分难度的逆向靶场，**最推荐的起点**
- [pwn.college](https://pwn.college) —— 系统化二进制安全课程
- [ROP Emporium](https://ropemporium.com) —— 栈与调用约定实战
- [dogbolt.org](https://dogbolt.org) —— 在线多反编译器对比
