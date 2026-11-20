# 04 · Unity 引擎专项

> Unity 是逆向门槛最低、也是反作弊最容易翻车的引擎。

## 两条技术路线

Unity 打包后，脚本层有两种编译方式，**难度差一个数量级**：

| | Mono | IL2CPP |
|---|---|---|
| 脚本形式 | .NET 程序集（MSIL） | 编译成 C++ 再编译成原生代码 |
| 产物 | `Assembly-CSharp.dll` | `GameAssembly.dll` + `global-metadata.dat` |
| 逆向难度 | 极低（可以直接读 C#） | 中高（需要还原符号） |
| 反混淆手段 | 无（de4dot 可破） | 元数据加密、符号剥离 |

### 判断方法

看游戏目录：

```
游戏根目录/
├── GameName_Data/
│   ├── Managed/            ← 有 Managed 目录 = Mono
│   │   └── Assembly-CSharp.dll
│   ├── il2cpp_data/        ← 有 il2cpp_data 目录 = IL2CPP
│   └── globalgamemanagers
└── GameAssembly.dll        ← 有 = IL2CPP
```

## Mono 路线

### 工具

- **dnSpy**（或 dnSpyEx）—— 直接打开 `Assembly-CSharp.dll`，能看、能改、能保存
- **ILSpy** —— 更干净的 C# 反编译
- **de4dot** —— 去混淆（如果被 obfuscator 处理过）

### 分析起点

1. 打开 `Assembly-CSharp.dll`
2. 搜索关键字：`Player`、`Health`、`Damage`、`Weapon`、`Enemy`
3. 反编译看逻辑
4. 注意 `MonoBehaviour` 子类 —— 它们挂着游戏对象

### 常见结构

```csharp
public class PlayerController : MonoBehaviour
{
    public float health = 100f;
    public float maxHealth = 100f;
    public Vector3 position;
    private CharacterController controller;

    void Update() { ... }
}
```

字段名都还在，这是 Mono 的「优势」，也是为什么正式游戏基本不用 Mono。

## IL2CPP 路线

### 产物结构

| 文件 | 作用 |
|---|---|
| `GameAssembly.dll` | 编译后的原生代码 |
| `global-metadata.dat` | 元数据：类名、方法名、字段名、字符串、方法地址表 |
| `il2cpp_data/Metadata/` | 同上（不同版本位置不同） |

### Il2CppDumper

需要**同时**提供两个文件：

```bash
Il2CppDumper.exe GameAssembly.dll global-metadata.dat output_dir/
```

输出：

- `dump.cs` —— 伪 C# 代码，能直接看到类结构和字段偏移
- `il2cpp.h` —— 结构体定义
- `script.json` —— 给 IDA/Ghidra 用的符号脚本
- `stringliteral.json` —— 字符串字面量

### 关键认知

> `dump.cs` 里每个字段后面都标着偏移，例如：
> ```csharp
> public float health; // 0x18
> ```
> 这个 `0x18` 就是你在内存里要用的偏移。**IL2CPP 的字段偏移是编译期固定的**，这让内存扫描变得容易——也是防护要处理的点。

### 方法地址

IL2CPP 的方法地址在 `global-metadata.dat` 的方法表里，Il2CppDumper 会导出。拿到地址后：

- 在 IDA/Ghidra 里用 `script.json` 自动改名
- 在动态调试器里直接下断点

## 防护视角：Unity 游戏的加固点

| 攻击面 | 防护手段 |
|---|---|
| 元数据明文 | 加密 `global-metadata.dat`，运行时解密 |
| 方法名泄露 | 剥离/重命名符号 |
| 字段偏移固定 | 加偏移混淆、结构体重新排列 |
| Mono 程序集 | 换 IL2CPP + 混淆 |
| 内存扫描 | 数值加密存储、影子副本校验 |
| 关键逻辑在客户端 | **下沉到服务端** |

## 检查清单

分析一个 Unity 游戏时，先回答这几个问题：

- [ ] Mono 还是 IL2CPP？
- [ ] `global-metadata.dat` 有没有加密？（用十六进制看头部魔数）
- [ ] 有没有混淆/保护壳？
- [ ] 关键逻辑（伤害计算、命中判定）在客户端还是服务端？

## 参考

- [Il2CppDumper](https://github.com/Perfare/Il2CppDumper)
- [Il2CppInspector](https://github.com/djkaty/Il2CppInspector)
- [dnSpyEx](https://github.com/dnSpyEx/dnSpy)
