# Lab 07 · 联机僵尸靶场（网络 + 操作系统 + 逆向）

> 这是 `docs/14-operating-system.md` 和 `docs/15-network.md` 的**动手配套**。
> 不是模拟：真进程、真 socket、真 TCP、真的掉血。
>
> 同一批实验已分别并入两个专题仓库：[os-journal](https://github.com/WMWWZS/os-journal) labs/02（内核/地址空间视角）与 [network-journal](https://github.com/WMWWZS/network-journal) labs/06（协议/抓包视角）。这里保留的是**逆向视角**的完整版。
> 全程 60~90 分钟。做完你应该能说清楚三句话：
>
> 1. 我发的 16 个字节，在网线上是什么样？
> 2. 它们在对方内存里变成了什么？
> 3. 我在 x64dbg 里怎么一眼看到它们？

## 0. 前置知识

| 需要会 | 来自 | 用在哪一步 |
|---|---|---|
| 进程/线程、地址空间、句柄 | [docs/14-operating-system.md](../../docs/14-operating-system.md) | 看懂 `osprobe.exe` 的输出 |
| TCP 三次握手与字节流 | [docs/15-network.md](../../docs/15-network.md) | 理解"16 个字节在网线上" |
| 会跑 python 脚本、能开两个终端 | 本机操作 | 跑 `tap.py` 和靶场 |

**不需要**：处理器架构细节、内核驱动。

## 0.5 游戏里这对应什么

| 联机游戏里 | 这个靶场里 | 你能看到什么 |
|---|---|---|
| 客户端按 W，上报"我在往前走" | 客户端发 16 字节 | 抓包看到原始字节 |
| 服务端算伤害/位置 | 服务端接收并回写血量 | 两边内存里的同一份数值 |
| 「为什么我这边显示不同步」 | 延迟与丢包 | 用自己的手把延迟造出来 |
| 「改了本地数值为什么没用」 | 服务端权威 | 亲手验证本地改、服务端不认 |

---
## 目录里有什么

```
labs/07-net-os/
├── net/        ← 靶场本体：真 TCP 客户端/服务端 + 一条"电线夹子" tap.py
├── os/         ← osprobe.exe：打印一个真进程的地址空间/TEB/句柄/syscall
├── capture/    ← Wireshark 抓包脚本 + ZOMBIE 协议解析器(lua) + demo.pcapng
├── re/         ← 静态逆向取证：导入表 → send 调用点 → Ghidra 反编译
└── BRIEF.md    ← 协议规格书（三个 lab 共同遵守的契约）
```

## 协议长这样（先看一眼，后面全围绕它）

```
偏移 0   u16 total_len    后面还有多少字节（= 1 + 1 + 载荷长度）
偏移 2   u8  opcode       0x01 HELLO / 0x02 ATTACK / 0x03 HP_SYNC / 0x04 SNAPSHOT / 0x05 BYE
偏移 3   u8  flags        保留
偏移 4   payload          字段见 BRIEF.md
```

一帧真实数据：

```
0e 00 02 00 00 00 a0 40 01 00 00 00 00 00 be 42
   ↑     ↑  └──5.0──┘ └──seq=1──┘ └──claimed 95.0──┘
  len=14  ATTACK                                  = 16 字节整帧
```

---

## 第 0 步：编译（1 分钟）

```powershell
cd E:\harness\reversing-journal\labs\07-net-os\net
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

> 为什么要 `-ExecutionPolicy Bypass`：这台机器默认策略会拒绝跑本地未签名 `.ps1`。
> 它只影响这一次进程，不改系统设置。

产物（都是 `-O0 -g` 和 `-O2 -s` 两套）：

```
bin\zombie_srv_O0.exe   bin\zombie_srv_O2s.exe     ← 服务端
bin\zombie_cli_O0.exe   bin\zombie_cli_O2s.exe     ← 客户端（-O2 -s = 真实游戏形态，用来逆向）
```

---

## 第 1 步：跑起来，看它掉血（5 分钟）

```powershell
cd E:\harness\reversing-journal\labs\07-net-os\net
powershell -NoProfile -ExecutionPolicy Bypass -File .\run.ps1
```

它会依次起 **服务端(47001) → tap(47000→47001) → 客户端**，你要看到：

```text
[+  0.002s] listen 127.0.0.1:47001 ok, waiting for client
[+  0.188s] RX  12B 0a 00 01 00 01 00 00 00 01 00 00 00  HELLO client_id=1 version=1
[+  0.598s] RX  16B 0e 00 02 00 00 00 a0 40 01 00 00 00 00 00 be 42  ATTACK seq=1 dmg=5.00 claimed=95.00
[+  0.599s] HP   100.00 -> 95.00  (authoritative: claimed says 95.00, diff=0.00)
[+  0.599s] TX  24B 16 00 03 00 01 00 00 00 00 00 be 42 00 00 c8 42 01 00 00 00 01 00 00 00  HP_SYNC hp=95.00
...
[+  1.652s] FINAL hp=75.00 max_hp=100.00  frames=7 hello=1 attack=5 reject=0 bye=1
```

**100 → 75**，五次攻击，一帧不多一帧不少。这就是你以后要逆向的东西的最小形态。

> 端口被占了？`run.ps1` 会先探测。这台机器上 `4001` 曾被 `QQ.exe` 占着（真实案例），
> 所以靶场默认用 `47000/47001`。

---

## 第 2 步：把"网络字节"和"内存"接上（10 分钟）

**2.1 看 tap 记录**：`net\evidence\tap.txt`

```
[+  0.101s] C>S    12B  0a 00 01 00 01 00 00 00 01 00 00 00
[+  0.504s] C>S    16B  0e 00 02 00 00 00 a0 40 01 00 00 00 00 00 be 42
[+  0.505s] S>C    24B  16 00 03 00 01 00 00 00 00 00 be 42 00 00 c8 42 01 00 00 00 01 00 00 00
[+  1.547s] C>S     4B  02 00 05 00
```

**2.2 看内核怎么说**：`net\evidence\netstat.txt`

```
  TCP    127.0.0.1:47000    0.0.0.0:0           LISTENING     5640
  TCP    127.0.0.1:47000    127.0.0.1:49772     ESTABLISHED   5640
  TCP    127.0.0.1:49772    127.0.0.1:47000     ESTABLISHED   38344
  TCP    127.0.0.1:47000    127.0.0.1:49762     TIME_WAIT     0
```

自己验证一遍（趁 `run.ps1` 还没结束）：

```powershell
netstat -ano | findstr 4700
Get-Process -Id <上面的PID> | Select-Object ProcessName,Handles,Threads
```

**2.3 自己改一个包**（这就是"外挂"的最小原型）：

```powershell
# 终端 1：服务端信客户端
.\bin\zombie_srv_O2s.exe --port 47001 --trust-client
# 终端 2：tap 在转发时把 claimed_hp 改成 1.0
& $py -u .\tap.py --listen-port 47000 --target-port 47001 --hack-claimed 1.0
# 终端 3：客户端照常打
.\bin\zombie_cli_O2s.exe --port 47000 --attacks 5 --dmg 5
```

你会看到服务端血量 `100.00 -> 1.00`。**把 `--trust-client` 换成 `--authoritative` 再跑一次**，
同样的包，血量照掉到 75——这就是"服务端权威"和"客户端权威"的区别，也是防作弊的全部地基。

（`$py` = `C:\Users\风林火山\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe`）

---

## 第 3 步：操作系统这一层（15 分钟）

```powershell
cd ..\os
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\run.ps1     # 跑 3 次 + 1 次改名副本
```

对着 `os\evidence\aslr.txt` 回答三个问题：

1. 为什么 exe 基址 3 次都一样，栈地址 3 次都不一样？
2. 把 exe 复制一份改名再跑，为什么基址变了？
3. `kernel32`/`ntdll` 的基址为什么对所有进程都一样？

再打开 `os\evidence\run1.txt`，找到：

- `A1`~`A6`：六个地址分别属于"栈 / 堆 / 镜像 / 系统 DLL"
- `B3`：栈 + guard page（栈是怎么长大的）
- `D1`/`D2`：从 TEB 读出来的栈边界（`gs:0x08` / `gs:0x10`）
- `E`：160 万次系统调用的 `kernel 750 ms`

> **最重要的一条**：`A1` 那个基址 + RVA = x64dbg 里的地址。
> 跑出来的基址每次开机才变，所以**偏移能写死，绝对地址不能写死**。

---

## 第 4 步：逆向自己刚写的程序（20 分钟）

```powershell
cd ..\re
powershell -NoProfile -ExecutionPolicy Bypass -File .\analyze.ps1
```

三份证据：

| 文件 | 看什么 |
|---|---|
| `evidence\zombie_cli_O2s-objdump-p.txt` | 导入表里有 `WS2_32.dll` 和 `send/recv/select` |
| `evidence\zombie_cli_O2s-ghidra-callers.txt` | `send` 被 `FUN_140001be0` 调用，反编译出 `frame[0]=len+2` |

然后对照 `docs/15-network.md` 第六节那张"反编译 ↔ 线上字节"的表，一行一行对。

**最后一步（自己动手，5 分钟）**：x64dbg 断在 `ws2_32!send`

1. 先跑服务端，再开 x64dbg 打开 `zombie_cli_O2s.exe`
2. 命令行输入 `bp ws2_32.send`，`F9`
3. 断下后看 `RDX`（缓冲区指针）、`R8`（长度=16）
4. `Ctrl+G` 跳 `RDX` → 内存窗口 → 十六进制

**你应该看到 `0e 00 02 00 00 00 a0 40 …`，和 tap 抓到的一模一样。**

---

## 第 5 步（可选）：真抓包（本机实测：普通窗口也能抓）

Wireshark 在这台机器的 `E:\Wireshark`。**这台机器上普通（非管理员）窗口实测也能抓 loopback**——
`capture\evidence\live-capture.txt` 就是 Medium 完整性级别抓到的 62 个包（脚本非管理员时只警告不退出）。

```powershell
cd E:\harness\reversing-journal\labs\07-net-os\capture
powershell -ExecutionPolicy Bypass -File .\capture.ps1
# 它开始抓后，另开一个窗口：cd ..\net ; powershell -ExecutionPolicy Bypass -File .\run.ps1
# 回到抓包窗口按回车停止
```

抓完会自动调 `zombie.lua` 生成 `dissect.txt`。真实抓包的帧长是 **56/60/68**：

```text
ZOMBIE 56 HELLO client_id=1 version=1
ZOMBIE 60 ATTACK damage=5.00 seq=1 claimed_hp=95.00
ZOMBIE 68 HP_SYNC player_id=1 hp=95.00/100.00 tick=1 alive=1
```

比 demo 里的 66/70/78 小 10 字节，因为 loopback 的链路层是 NULL/Loopback（4 字节）而不是以太网（14 字节）——
**同一个协议，换个抓包点，包头算法就变**（详见 `docs/15-network.md` 第三、八节）。

如果 dumpcap 报 `You do not have permission`，换管理员窗口重跑；没有抓包权限也能学：
`capture\demo.pcapng` 里的 ZOMBIE 字节是真的（取自真实运行），只有 IP/TCP 头是演示合成的。

---