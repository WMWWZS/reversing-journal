# Labs · 实验

> **看懂 ≠ 会了。** 每个概念都要有一个能编译、能跑、能自己改的实验。

## 约定

每个实验一个目录，编号与 `docs/` 对应，必须包含：

```
lab-name/
├── README.md      # 目的、原理、预期输出、自测题
├── build.ps1      # 一键构建
└── *.c / *.h / *.gdb   # 源码与脚本
```

编译产物（`*.exe`）已被 `.gitignore` 忽略，**不提交二进制**。

## 实验列表

| 编号 | 实验 | 对应笔记 | 状态 |
|:---:|---|---|:---:|
| 01 | 结构体布局与偏移 | [01 · 内存布局](../docs/01-memory-layout.md) | ✅ 完成 |
| 02 | 调用约定与栈帧 | [02 · 调用约定](../docs/02-calling-conventions.md) | ⬜ 待做 |
| 03 | PE 结构解析 | — | ⬜ 待做 |
| 04 | 指针链推导 | — | ⬜ 待做 |
| 05 | 虚函数表与 RTTI | — | ⬜ 待做 |
| 06 | crackme 01：定位校验逻辑 | [00 · 路线图](../docs/00-roadmap.md) | 🟡 进行中 |

## 环境要求

- Windows x64
- MinGW-w64（`gcc`、`gdb`、`objdump`）

检查：

```powershell
gcc --version
gdb --version
objdump --version
```
