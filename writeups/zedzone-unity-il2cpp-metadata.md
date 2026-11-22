# ZED ZONE · Unity IL2CPP 元数据分析

- **日期**：2026-11-22
- **目标**：ZED ZONE（Steam 单机生存游戏，本人已购买）
- **目的**：第一次真实目标分析，走通 IL2CPP 逆向的标准流程
- **声明**：仅做静态结构分析，用于学习与防护研究，不制作、不分发任何外挂

## 一、侦察

### 目录结构

```text
ZED ZONE/
├── ZEDZONE.exe              0.6 MB   ← 启动器，逻辑不在这里
├── GameAssembly.dll        77.5 MB   ← 真正的游戏代码（IL2CPP 编译产物）
├── UnityPlayer.dll         28.6 MB
├── baselib.dll              0.4 MB
└── ZEDZONE_Data/
    └── il2cpp_data/Metadata/global-metadata.dat   18.8 MB
```

看到 `GameAssembly.dll` + `global-metadata.dat` = **Unity IL2CPP**。

### 元数据是否加密

```text
global-metadata.dat 开头：
AF 1B B1 FA | 1D 00 00 00 | ...
```

- 前 4 字节小端读作 `0xFAB11BAF` = **标准 IL2CPP 魔数**
- 接下来 4 字节 `0x1D` = **元数据版本 29**

**结论：未加密。** 可以直接用 Il2CppDumper。

> 如果魔数不是 `0xFAB11BAF`，说明被加密或混淆，需要先脱壳——那是完全不同的难度。

## 二、工具

| 工具 | 版本 | 用途 |
|---|---|---|
| Il2CppDumper | v6.7.46 (win) | 解析 GameAssembly.dll + metadata |
| .NET | 6/7/8 已装 | dumper 运行时 |

命令：

```powershell
Il2CppDumper.exe GameAssembly.dll global-metadata.dat <输出目录>
```

## 三、产物

```text
Dumping... Done!
Generate struct... Done!
Generate dummy dll... Done!

Metadata Version: 29
Il2Cpp Version  : 29.1
CodeRegistration        : 0x183bf5570
MetadataRegistration    : 0x18423d220
```

| 文件 | 大小 | 内容 |
|---|---:|---|
| `dump.cs` | 38 MB | 全部类/字段/方法 + **字段偏移** + **函数 RVA/VA** |
| `il2cpp.h` | 55 MB | C 结构体定义，可直接喂给 IDA/Ghidra |
| `script.json` | 117 MB | 自动命名脚本用的符号表 |
| `stringliteral.json` | 4.5 MB | 全部字符串字面量 |
| `DummyDll/` | — | 只有签名没有方法体的 C# 程序集 |

> `DummyDll/Assembly-CSharp.dll` 可以用 dnSpy / ILSpy 打开浏览类结构，比翻 38 MB 文本舒服。

## 四、真实类结构（摘录）

### 静态配置：`GiantZombieDefinition`

```csharp
public sealed class GiantZombieDefinition : ScriptableObject
{
    public bool  king;          // 0x18
    public int   monsterId;     // 0x1C
    public float height;        // 0x20
    public float health;        // 0x24   ← 基础血量（配置，不是运行时）
    public float walkSpeed;     // 0x28
    public float chaseSpeed;    // 0x2C
    ...
    public float palmDamage;    // 0x5C
    public float stompDamage;   // 0x60
    public float kickDamage;    // 0x64
    ...
    public float gasDps;        // 0xA0
    public float collapseDuration; // 0xA4
    public Vector2 corpseOffset;   // 0xA8
}
```

### 运行实例：`GiantZombieController`

```csharp
public class GiantZombieController
    : BasicCharacterController, ICataclysmBoss, IHordeEscortLeader
{
    private bool  <isCataclysmBoss>k__BackingField;   // 0x970
    private float <cataclysmScale>k__BackingField;    // 0x974
    private float cataclysmHpFloat;                   // 0x978
    private float attackClock;                        // 0x9D0
    private float poiseDamage;                        // 0x9E8
    private float recoil;                             // 0x9EC
    private int   facing;                             // 0x9F4
    ...
}
```

## 五、三个真实世界才有的现象

### 1. 字段偏移能到 `0x970`

因为**继承**：`GiantZombieController` 继承 `BasicCharacterController`，
基类的字段占满前面一大段，子类字段从 `0x970` 才开始。

> 玩具例子里没有继承，所以偏移都是小数字。

### 2. `Definition` ≠ 运行时实例

`GiantZombieDefinition` 是 `ScriptableObject`——**配置资源**，存在资源包里。
运行时控制器通过 `definition` 字段（`GiantZombieController.definition @ 0x9B0`）引用它。

**真实血量不在 Definition 里**，而在基类 `BasicCharacterController` 的某个字段上。
**分析时必须区分「配置值」和「运行状态」。**

### 3. 反射生成的字段名

```csharp
private float <cataclysmHpFloat>k__BackingField; // 0x974
```

`<X>k__BackingField` 是 C# 自动属性（`public float X { get; set; }`）编译后的**后备字段**。
看到这种名字，说明源码里写的是属性，不是字段。

### 4. 中文注释（编码问题）

游戏开发者写了中文 Tooltip，例如：

```csharp
[Tooltip("总开关? 关闭后所有快照碎块效果禁用")]
public bool enabled; // 0x10
```

文件是 UTF-8，用 PowerShell 默认编码读会乱码。**读 dump.cs 必须显式指定 UTF-8。**

## 六、下一步

1. 找到基类 `BasicCharacterController` 里的运行时血量字段和偏移
2. 用 x64dbg 附加运行中的游戏，验证该偏移
3. 记录：从「类定义」到「内存里的实际值」这一步怎么走

## 七、法律与边界

- 只分析自己购买的单机游戏
- 不接触 CS2 / Delta Force / R6 等带反作弊的多人游戏
- 不制作、不传播任何作弊工具
- 目的是理解结构，为防护设计积累认知
