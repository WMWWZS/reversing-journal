# 05 · Unreal Engine 专项

> UE 的逆向核心是**三张全局表**：GObjects、GNames、以及 UWorld 的对象链。

## UObject 体系

UE 里一切都是 `UObject` 的派生类：

```
UObject
 ├── UField
 │    ├── UStruct
 │    │    ├── UClass      ← 类定义
 │    │    └── UFunction   ← 函数定义
 │    └── UEnum
 └── UWorld                 ← 关卡世界
      └── ULevel
           └── AActor       ← 关卡里的对象
                └── APawn
                     └── ACharacter  ← 玩家角色
```

### 关键概念

- **UPROPERTY / UFUNCTION** —— 反射标记，让字段和方法进入元数据
- **UClass** —— 运行时的类对象，包含属性表和方法表
- **FName** —— 字符串池索引，不是普通字符串

## 三张关键表

### 1. GObjects（GUObjectArray）

所有 UObject 实例的全局数组。

```cpp
// 简化结构（不同版本有差异）
struct FUObjectArray {
    TArray<FUObjectItem*> Objects;
    int32 NumElements;
};

struct FUObjectItem {
    UObject* Object;
    int32 Flags;
    int32 ClusterIndex;
    int32 SerialNumber;
};
```

**用途**：遍历所有对象，按类名过滤，找到目标（例如所有 `APlayerController`）。

### 2. GNames（FNamePool）

所有名字的全局池。UE4.23 之后从 `TNameEntryArray` 改成了 `FNamePool`。

**用途**：把对象的 `FName` 索引还原成字符串，才知道「这是什么」。

### 3. UWorld 链

从 `UWorld` 出发找到玩家：

```
UWorld
 └── OwningGameInstance
      └── LocalPlayers[0]          ← ULocalPlayer
           └── PlayerController    ← APlayerController
                └── AcknowledgedPawn ← APawn / ACharacter
```

还有一条更短的常用链：

```
UWorld
 └── PersistentLevel
      └── Actors[]                 ← 关卡里所有 Actor
```

或者通过 `UGameplayStatics::GetPlayerPawn()` 之类的函数。

## 定位流程

1. **找 GObjects 地址**
   - 搜索特征码，或用已知的字符串引用反查
   - 不同版本偏移不同，需要先确定引擎版本
2. **找 GNames 地址**
   - 同上；`FNamePool` 有较明显的结构特征
3. **遍历 GObjects**
   - 用类名过滤，找 `APlayerController` / `ACharacter`
4. **从对象读字段**
   - 用 SDK dump 出来的偏移

## SDK Dump

社区工具能自动从运行中的游戏导出完整 SDK（类、字段、偏移）：

- **UE4SS** —— UE4/UE5 的脚本与 SDK 工具，Lua 脚本可扩展
- **FModel** —— 资源解包
- **Dumper-7** 等社区工具

导出的 SDK 长这样：

```cpp
class ACharacter : public APawn {
public:
    float Health;                    // 0x03A8
    float MaxHealth;                 // 0x03AC
    class USkeletalMeshComponent* Mesh;  // 0x03B0
    ...
};
```

> 拿到 SDK 后，偏移直接用。这就是为什么 UE 游戏的防护重点不在「藏偏移」，而在**服务端权威**。

## 引擎版本判断

- 文件版本号（`Engine/Config` 里的 `EngineVersion`）
- `Chunk` 结构
- `FNamePool` 的布局差异
- 特征字符串：`++UE4+Release-4.27`、`++UE5+Release-5.1`

## 防护视角：UE 游戏的加固点

| 攻击面 | 说明 |
|---|---|
| GObjects 全局可达 | 任何对象都能被遍历到 → 敏感数据不该只放客户端 |
| SDK 可 dump | 偏移公开 → 不能依赖「藏偏移」 |
| 蓝图逻辑 | `.uasset` 可解包 → 关键逻辑下沉服务端 |
| 内存明文 | 血量、坐标直接读 → 加密存储 + 校验 |
| 客户端预测 | 位置校正必须服务端做 |

## 检查清单

- [ ] 引擎版本？（UE4 还是 UE5，小版本是多少）
- [ ] 有没有反 dump 保护？
- [ ] GObjects / GNames 是否被加密或随机化？
- [ ] 关键数值（伤害、经济）在哪里计算？

## 参考

- [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS)
- [FModel](https://github.com/4sval/FModel)
- Unreal Engine 源码（需要 Epic 账号）
