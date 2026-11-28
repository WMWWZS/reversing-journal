# 实验索引

## 每个实验的固定结构（四件套）

1. **前置知识** —— 这个实验需要先会什么，缺哪块、去哪补
2. **游戏语义映射** —— 这个技术对应游戏里的什么东西（"相当于游戏的什么"）
3. **可视化** —— 一张图说清结构或流程，不靠一串数字
4. **实测数字** —— 在自己机器上跑出来的真实输出，可复现

> 09-hook 起按此执行（含 `game-mapping.png` / `hook-steps.png`）；00–08 会逐步补齐。

| 目录 | 回答的问题 | 关键结论 | 状态 |
|---|---|---|---|
| [00-memory-model](00-memory-model) | 地址 / 偏移 / 字节 / 跨进程读到底是什么 | 地址=格子编号；偏移=加法；float 4 字节低位在前；`ReadProcessMemory`=memcpy+handle | 完成 |
| [01-struct-layout](01-struct-layout) | `+0x10` 怎么来的 | 偏移 = 字段顺序 + 大小 + 填充；用 `offsetof` 验 | 既有 |
| [02-find-offset](02-find-offset) | 没源码怎么挖偏移 | 整映像扫描 + 五条互相印证的证据 | 完成 |
| [03-write-memory](03-write-memory) | 怎么跨进程写且留住 | 三件套权限；被重算就得循环写 | 完成 |
| [04-pointer-chain](04-pointer-chain) | 地址每次都变怎么办 | 指针=装地址的格子；链=读 8 字节+加偏移；必须扫全进程 | 完成 |
| [05-aob-scan](05-aob-scan) | 更新后偏移失效怎么办 | 扫 `48 8B 05 disp32`，`目标=指令+7+disp`，候选必须验证 | 完成 |
| [06-crackme01](06-crackme01) | 纯静态分析练手 | 既有实验 | 既有 |
| [06-inject](06-inject) | 让目标自己改自己 | `VirtualAllocEx`+`WriteProcessMemory`+`CreateRemoteThread`；非易失寄存器、栈对齐 | 完成 |
| [07-net-os](07-net-os) | 网络/系统侧的靶场 | 既有实验 | 既有 |
| [08-config-tool](08-config-tool) | 工具怎么指向任意目标 | 配置驱动 + `ptr_size=4/8`；宽度写错会得到假地址 | 完成 |
| [09-hook](09-hook) | 怎么让目标「算不出伤害」 | 五步：存原字节 → VirtualProtectEx → WriteProcessMemory → FlushInstructionCache → 还原权限；补丁长度必须精确到字节 | 完成 |
| [10-projection](10-projection) | 敌人在我屏幕的哪个像素 | 世界坐标 → 相机前/右轴 → 像素；点积法与角度法互验；符号错差 832 像素 | 完成 |

## 与 OS / 网络文档的关系

这组实验用到的句柄、页保护、私有内存、线程都在 [../docs/14-operating-system.md](../docs/14-operating-system.md)；
但不需要前置读完——哪一档知识在哪一步成为硬需求，见
[../docs/16-when-os-and-net.md](../docs/16-when-os-and-net.md)。网络只有换到联机目标才用得上。

## 为什么"攻"的实验属于这份手记

这一组实验全是进攻侧动作，价值在防守侧：**没亲手做过一次，你不知道该盯哪一行。**

| 实验 | 对手会做什么 | 你因此知道该观测什么 |
|---|---|---|
| 03 写内存 | `OpenProcess(VM_WRITE)` + `WriteProcessMemory` | 句柄访问权限记录（`ObRegisterCallbacks` 可见）、目标页写访问 |
| 04 指针链 | 重启后重新解析静态链 | 单点校验不够：同一数值需两处交叉 + 周期性重算 |
| 05 特征扫描 | 用指令字节形状定位全局 | 特征码也会被扫：加壳 / 虚拟化 / 多态抬高成本 |
| 06 注入 | `VirtualAllocEx`(RWX) + `CreateRemoteThread` | 线程起始地址不在任何已加载模块内、RWX 私有内存、`Thread32First` 外来线程、ETW 跨进程事件 |
| 08 配置化 | 同一套代码换配置就能换目标 | 检测方同样可以"配置化"：每个观测点都要落成规则，而不是靠人工看 |

一句话：**检测规则是照着攻击面写的。**