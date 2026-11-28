# Lab 06 · 逆向一个自定义长度字段协议（ZOMBIE 靶场）

对应笔记：[09 · 自定义二进制协议](../../docs/09-custom-binary-protocol.md)、[08 · 抓包与协议分析](../../docs/08-capture-and-analysis.md)

> 前置：会跑 `py`、装过 `gcc`（MinGW-w64），Wireshark 装在 `E:\Wireshark`（本机实测 4.0.17）。
> 本 lab 的源程序和原始输出来自姊妹仓库 [reversing-journal](https://github.com/WMWWZS/reversing-journal) 的 `labs/07-net-os/net`：
> 同一台机器上真跑出来的 TCP 靶场。搬进这里时只改了目录结构和端口说明，字节一个没动。

## 目的

1. 在一台机器上跑起一个**真的用 TCP 说话**的私有协议靶场（客户端 / 中间人 tap / 服务端三方），不是内存里假装的字节；
2. 从**线上真实字节**里还原出帧结构：`total_len` / `opcode` / `flags` / payload，并证明 `total_len` **不含它自己那 2 个字节**；
3. 亲眼看到「TCP 是字节流」：一张 16006 字节的帧，客户端要 `recv()` 3~4 次才凑齐，而且**每次跑的切点都不一样**；
4. 用 Wireshark Lua 插件把同一段字节流解成字段，再用 Ghidra 反编译客户端，**让抓包和反编译互相印证**：`frame[0] = payload_len + 2`；
5. 在中间人上改 4 个字节（`claimed_hp`），看「信客户端」和「服务端权威」两种服务端的行为差多少——这是协议逆向的安全落点。

**这个 lab 会真的在本机 `127.0.0.1` 上开端口、发 TCP、掉血**，但只连本机回环，不产生任何对外流量。

## 原理

### 1. 帧格式（整个协议就这 4 行）

```text
 偏移  长度  字段        说明
 0     2     total_len   u16 小端，= 1(opcode) + 1(flags) + payload 字节数；**不含这 2 字节自身**
 2     1     opcode      0x01 HELLO / 0x02 ATTACK / 0x03 HP_SYNC / 0x04 SNAPSHOT / 0x05 BYE
 3     1     flags       保留，恒为 0
 4     N     payload     全小端：u32 / f32 / 定长数组
```

由此得到三条互相校验的等式，本 lab 每个数字都靠它们对账：

```text
payload_len = total_len - 2
整帧 bytes  = total_len + 2 = payload_len + 4
```

五种帧的真实长度（后面日志里反复出现）：

| opcode | 名称 | payload | total_len（字节 / hex） | 整帧 |
|:---:|---|---:|---|---:|
| `0x01` | HELLO | 8 | 10 / `0a 00` | **12** |
| `0x02` | ATTACK | 12 | 14 / `0e 00` | **16** |
| `0x03` | HP_SYNC | 20 | 22 / `16 00` | **24** |
| `0x04` | SNAPSHOT | 2 + 16 × count | 2 + 2 + 16 × count | **4 + 2 + 16 × count** |
| `0x05` | BYE | 0 | 2 / `02 00` | **4** |

> **差 2 是这一课最容易记错的地方**：HELLO 的 `total_len` 是 10、整帧是 12；HP_SYNC 的 `total_len` 是 22、整帧是 24。
> 只要把「长度字段 = 整帧」当默认假设，后面所有字段都会错位 2 字节。

`count=1000` 的 SNAPSHOT 是最大的一张帧：payload `2 + 16×1000 = 16002`，`total_len = 16004 = 0x3e84`，整帧 **16006** 字节。

### 2. 端序与 float：`00 00 be 42` 就是 95.0

x86 是小端；float 的 4 个字节按「低位在前」写。这个协议里所有数字都是小端，所有血量/伤害/坐标都是 f32：

| 线上字节 | 反过来读 | 十六进制 | 值 |
|---|---|---|---:|
| `00 00 a0 40` | `0x40A00000` | 40A00000 | **5.0**（damage） |
| `00 00 be 42` | `0x42BE0000` | 42BE0000 | **95.0**（hp） |
| `00 00 c8 42` | `0x42C80000` | 42C80000 | **100.0**（max_hp） |
| `00 00 f0 41` | `0x41F00000` | 41F00000 | **30.0**（越界伤害） |
| `00 00 8c 42` | `0x428C0000` | 428C0000 | **70.0**（REJECT 场景里客户端的预测） |
| `00 00 80 3f` | `0x3F800000` | 3F800000 | **1.0**（tap 改包写入的值） |

### 3. TCP 是字节流：一帧 16006 字节，`recv()` 却切了 3 次

服务端发一张 16006 字节的 SNAPSHOT，客户端 `recv()` 收到的是 `4096 / 8192 / 3718`；同一份代码再跑一次，变成 `4096 / 4096 / 7814`。**总字节数永远是 16006，切点由 TCP 决定，不由发送方决定。**

所以接收端的解码器必须是「缓冲区 + 循环切帧」：攒够 `2 + total_len` 才切一帧，剩下的字节留在缓冲区等下一帧。`src/zombie_cli.c` 的 `conn_next_frame()` 就是干这个的。

### 4. 抓包和反编译是同一件事的两面

- 抓包能证明「线上跑的是什么字节」，但证明不了「代码为什么这么写」；
- 反编译能证明「代码里怎么拼帧」，但证明不了「线上真的这样跑」；
- 两边对上（`0e` = 12 + 2，`uVar5 = payload_len + 4` = 16），才是完整结论。

## 文件

```text
labs/06-zombie-protocol/
├── README.md                 本文
├── src/                      靶场本体（C + Python，零第三方依赖）
│   ├── zombie.h              协议唯一定义：帧结构 + 小端读写 + 日志/收发工具
│   ├── zombie_srv.c          服务端：--authoritative（默认）/ --trust-client
│   ├── zombie_cli.c          客户端：本地预测 + RECONCILE + 每次 recv 打字节数
│   ├── tap.py                中间人：47000 → 47001 双向转发；--hack-claimed 改包
│   ├── build.ps1             编译 4 个 exe（-O0 -g 两个 / -O2 -s 两个）→ evidence/
│   └── run.ps1               一键跑 10 个场景，写 evidence/ 下 7 个 txt，最后 13 条自检 PASS/FAIL
├── capture/                  抓包与 Wireshark Lua 解析
│   ├── zombie.lua            ZOMBIE 协议解析器（注册在 tcp.port 47000/47001）
│   ├── mkpcap.py             现场生成 demo.pcapng（真 ZOMBIE 字节 + 合成以太网/IP/TCP 头）
│   ├── demo-frames.hexdump   给 text2pcap 的输入（文本，可逐字节核对）
│   ├── capture.ps1           dumpcap 抓 loopback + 自动展开
│   ├── expand.ps1            任意 pcapng → dissect.txt / hex.txt / streams.txt
│   └── evidence/             demo-*.txt（合成会话解析）、live-capture.txt（真抓包）
├── re/                       静态逆向
│   ├── analyze.ps1           导入表 → 调用点 → Ghidra 反编译调用者
│   ├── DecompileCallersOf.java  「从导入函数反查谁调用它」的 Ghidra 脚本
│   └── evidence/             objdump -p、Ghidra 反编译原始输出
└── evidence/                 靶场运行的原始 stdout（run.ps1 生成）
```

三个 `evidence/` 目录里的内容全部是**进程原样输出**，没有手改。

## 运行

### 1. 一键跑协议靶场（需要 `gcc` + `py`）

```powershell
cd labs\06-zombie-protocol\src
powershell -NoProfile -ExecutionPolicy Bypass -File .\run.ps1
```

`run.ps1` 会编译 4 个 exe、跑 10 个场景、写 `..\evidence\*.txt`，最后打印 13 条自检。本机最后一次输出：

```text
run.ps1: cleaning up leftover lab processes from a previous run
[1/11] build the 4 executables (build.ps1 -> evidence\build.txt + evidence\imports.txt)
[2/11] scenario A: server 47001 + tap 47000->47001 + client, 5 attacks of 5.0
[3/11] scenario A0: -O0 -g build, 2 attacks, client connects straight to 47001 (no tap)
[4/11] scenario REJECT: client sends --dmg 30, server must refuse and keep hp=100.00
[5/11] scenario netstat: capture netstat/Get-NetTCPConnection while the client is connected
[6/11] scenario T1: --trust-client + tap rewrites claimed_hp to 1.00 (server believes it)
[7/11] scenario T2: --trust-client + tap rewrites claimed_hp to 100.00 (free full heal)
[8/11] scenario T3: same rewrite but server is --authoritative (it ignores claimed_hp)
[9/11] scenario D1: SNAPSHOT count=1000 (16006B frame), server sends 4x4096, client recv-buf 8192
[10/11] scenario D2: SNAPSHOT count=1000 in one send(), client recv-buf 65536 (one recv gets it all)
[11/11] write evidence\*.txt
  PASS  run.txt: server ended at hp=75.00
  PASS  run.txt: client predicted hp == server hp (match=YES)
  PASS  run.txt: 5th HP_SYNC carries hp=75.00
  PASS  run.txt: out-of-range damage was rejected
  PASS  tap.txt: tap decoded the 24-byte HP_SYNC frame
  PASS  tap.txt: tap decoded the 16-byte ATTACK frame
  PASS  netstat.txt: LISTENING captured
  PASS  netstat.txt: ESTABLISHED captured
  PASS  trust-client.txt: server believed claimed_hp=1.00
  PASS  trust-client.txt: server accepted a full heal
  PASS  big-frame.txt: D1 frame needed 2+ recv() calls
  PASS  big-frame.txt: D2 frame arrived in 1 recv() call
  PASS  big-frame.txt: 16006-byte SNAPSHOT seen

evidence written:
  big-frame.txt          10681 bytes
  build.txt               1474 bytes
  imports.txt             5310 bytes
  netstat.txt            27812 bytes
  run.txt                11418 bytes
  tap.txt                 3329 bytes
  trust-client.txt       21758 bytes

VERDICT: PASS
```

### 2. 手动三方（想自己改参数、看每帧实时输出时用）

三个窗口，端口固定 **tap 47000 → server 47001**：

```powershell
cd labs\06-zombie-protocol\src

# 窗口 1：服务端（权威模式）
.\bin\zombie_srv_O2s.exe --port 47001 --bind 127.0.0.1 --authoritative

# 窗口 2：中间人（原样转发 + 每块打 hex + 按协议解帧）
py -u tap.py --listen-port 47000 --target-port 47001 --target-host 127.0.0.1

# 窗口 3：客户端（5 次攻击、每次掉 5 点、走 tap）
.\bin\zombie_cli_O2s.exe --host 127.0.0.1 --port 47000 --attacks 5 --dmg 5
```

常用开关（都不改协议，只为取证方便）：

| 程序 | 开关 | 作用 |
|:---:|---|---|
| 服务端 | `--trust-client` / `--authoritative` | 信不信客户端的 `claimed_hp` |
| 服务端 | `--snapshot-count N` / `--send-chunk N` | HELLO 后发 N 个实体的快照；每次 send 最多 N 字节 |
| 客户端 | `--recv-buf N` / `--dmg F` / `--attacks N` | 一次 recv 的上限 / 伤害 / 攻击次数 |
| tap.py | `--hack-claimed X` | 把路上 ATTACK 帧的 `claimed_hp` 改成 X（帧长不变） |

### 3. Wireshark / tshark（本机装在 `E:\Wireshark`，4.0.17）

**先造一个可复现的 demo 抓包**（仓库里不提交 pcap，随时现场生成）：

```powershell
cd labs\06-zombie-protocol\capture
py mkpcap.py --keep-hexdump
# -> demo.pcapng (9 包，1200 字节) + demo-frames.hexdump（这个 hexdump 是文本，已入库）
```

**用 Lua 解析器看**（命令行）：

```powershell
& 'E:\Wireshark\tshark.exe' -r .\demo.pcapng -X lua_script:.\zombie.lua -Y zombie
& 'E:\Wireshark\tshark.exe' -r .\demo.pcapng -X lua_script:.\zombie.lua -V    # 逐字段展开
```

GUI 里：把 `zombie.lua` 复制到 `%APPDATA%\Wireshark\plugins`（个人 Lua 插件目录）后重启 Wireshark，过滤栏输入 `zombie`；或者 `Tools → Lua → Evaluate` 临时加载。

**抓一次真流量**（抓 `127.0.0.1:47000/47001` 的 loopback）：

```powershell
cd labs\06-zombie-protocol\capture
powershell -ExecutionPolicy Bypass -File .\capture.ps1
# 脚本开始抓包后，另开窗口跑 src\run.ps1（或上面「手动三方」）
# 回来按回车停抓包，脚本自动生成 evidence\dissect.txt / hex.txt / streams.txt
```

- **本机实测：普通（非管理员）PowerShell 也能抓 loopback**。`whoami /groups` 是 `Medium Mandatory Level`，dumpcap 照样抓到 62 个包（见 `capture/evidence/live-capture.txt`）。
- 但最稳的是**管理员窗口**：右键「开始」→「终端(管理员)」。reversing-journal 那边的 `capture.ps1` 也已同步改成「提示后继续」（同一份证据）；本目录同样如此，因为这台机器上 Npcap 没拦普通权限。
- 抓包文件（`*.pcapng`）**已列在 `.gitignore` 里，不会进仓库**；README 里引用的是 `capture/evidence/` 下的文本解析。

### 4. 反编译（Ghidra，可选）

```powershell
cd labs\06-zombie-protocol\re
powershell -NoProfile -ExecutionPolicy Bypass -File .\analyze.ps1
```

需要 `objdump`（MinGW 自带）+ Ghidra + JDK；路径在本机是 `D:\tools\ghidra\ghidra_12.2_PUBLIC`、`D:\tools\jdk-25\...`，可用参数覆盖。不装 Ghidra 也能读已入库的 `re/evidence/`。

## 真实输出

### 一、四帧的线上字节（来自 `evidence/tap.txt`，场景 A）

```text
[+  0.100s] C>S    12B  0a 00 01 00 01 00 00 00 01 00 00 00
[+  0.100s] C>S frame #1     12B  HELLO client_id=1 version=1
[+  0.503s] C>S    16B  0e 00 02 00 00 00 a0 40 01 00 00 00 00 00 be 42
[+  0.504s] C>S frame #2     16B  ATTACK dmg=5.00 seq=1 claimed=95.00
[+  0.505s] S>C    24B  16 00 03 00 01 00 00 00 00 00 be 42 00 00 c8 42 01 00 00 00 01 00 00 00
[+  0.506s] S>C frame #1     24B  HP_SYNC player=1 hp=95.00 max=100.00 tick=1 alive=1
[+  1.555s] C>S     4B  02 00 05 00
[+  1.555s] C>S frame #7      4B  BYE
[+  1.557s] tap summary  C>S chunks=7 bytes=96 frames=7 | S>C chunks=5 bytes=120 frames=5
```

`C>S chunks=7 / bytes=96` = HELLO(12) + 5×ATTACK(16×5=80) + BYE(4) = 96；`S>C chunks=5 / bytes=120` = 5×HP_SYNC(24×5)。**三方账目对得上**：客户端发的、tap 转的、服务端收的帧数一致。

### 二、逐字节拆 ATTACK（16 字节）

```text
0e 00 | 02 | 00 | 00 00 a0 40 | 01 00 00 00 | 00 00 be 42
└─┬─┘  └┬┘  └┬┘  └────┬────┘   └────┬────┘   └────┬────┘
  │     │    │        │             │             └ claimed_hp = 95.0 (f32)
  │     │    │        │             └─────────────── seq        = 1    (u32)
  │     │    │        └───────────────────────────── damage     = 5.0  (f32)
  │     │    └────────────────────────────────────── flags      = 0
  │     └─────────────────────────────────────────── opcode     = 0x02 ATTACK
  └───────────────────────────────────────────────── total_len  = 0x000e = 14（不含自身 2 字节）
```

### 三、逐字节拆 HP_SYNC（24 字节，`evidence/tap.txt` 第 1 条）

```text
16 00 | 03 | 00 | 01 00 00 00 | 00 00 be 42 | 00 00 c8 42 | 01 00 00 00 | 01 | 00 00 00
len=22  op   fl   player_id=1    hp=95.0        max_hp=100.0   tick=1         alive pad[3]
```

### 四、一张 16006 字节的 SNAPSHOT，和它被 recv 切成的几刀

服务端日志（`evidence/big-frame.txt`，D1）：

```text
[+  0.245s] TX  16006B 84 3e 04 00 e8 03 00 00 ... SNAPSHOT count=1000 payload=16002
                 (split into 4 send() calls of <=4096 bytes)
[+  0.246s] TX    send() #1/4   4096 bytes  offset=    0
[+  0.246s] TX    send() #2/4   4096 bytes  offset= 4096
[+  0.246s] TX    send() #3/4   4096 bytes  offset= 8192
[+  0.246s] TX    send() #4/4   3718 bytes  offset=12288
```

帧头拆开：`84 3e` = `total_len 0x3e84 = 16004`；`04` = SNAPSHOT；`00` = flags；`e8 03` = `count 1000`；后面 1000 × 16 字节实体。整帧 `2 + 16004 = 16006`。

客户端这一侧（`evidence/big-frame.txt`，D1）：

```text
[+  0.030s] RX recv n= 4096
[+  0.031s] RX recv n= 8192     ← 服务端的 #2、#3 被 TCP 合成了这一次 recv
[+  0.031s] RX recv n= 3718
[+  0.032s] RX  16006B ... SNAPSHOT count=1000 payload=16002 (payload should be 16002) recv_chunks=3
[+  0.032s]       SNAPSHOT first={id=0 x=0.00 y=0.00 hp=100.00} last={id=999 x=1498.50 y=499.50 hp=1.00}
[+  0.649s] SUMMARY snapshot_frames=1 snapshot_bytes=16006 reconcile=0 recv_calls=4 bytes_left_in_buffer=0
```

**同一份代码、同一条命令，再跑三次（`evidence/big-frame-variance.txt`）**：

```text
manual D1 repeat #1: recv n= 4096 / 4096 / 4096 / 3718   recv_chunks=4
manual D1 repeat #2: recv n= 4096 / 4096 / 7814          recv_chunks=3
manual D1 repeat #3: recv n= 4096 / 4096 / 4096 / 3718   recv_chunks=4
```

**总字节 16006 一次没少，切点却换着来。** 这就是「TCP 给你的是字节流，不是消息」的直接证据。
（对照 D2：服务端一次 `send()` 写完整帧、客户端 `recv-buf 65536`，一次 `recv n=16006`，`recv_chunks=1`。）

### 五、Wireshark 认出了协议（`capture/evidence/demo-*.txt`）

装好 `zombie.lua` 之后，tshark 的 Info 列不再是 `TCP`，而是协议自己写的描述：

```text
    4 0.000003000    127.0.0.1 → 127.0.0.1    ZOMBIE 66 HELLO client_id=1 version=1
    5 0.000004000    127.0.0.1 → 127.0.0.1    ZOMBIE 70 ATTACK damage=5.00 seq=1 claimed_hp=95.00
    6 0.000005000    127.0.0.1 → 127.0.0.1    ZOMBIE 78 HP_SYNC player_id=1 hp=95.00/100.00 tick=1 alive=1
```

`-V` 里的 HP_SYNC 字段（`demo-dissect.txt`）：

```text
ZOMBIE HP_SYNC, 24 bytes on the wire
    Total Length: 22
    Opcode: HP_SYNC (0x03)
    Flags: 0x00
    Player ID: 1
    HP: 95
    Max HP: 100
    Server Tick: 1
    Alive: 1
    Padding: 000000
```

注意 `24 bytes on the wire` 和 `Total Length: 22` 同时出现，差 2 就是「长度字段不含自身」在 Wireshark 里的样子。

`-T fields` 直接出表（`demo-fields.txt`）：

```text
frame.number,tcp.len,zombie.opcode,zombie.total_len,zombie.client_id,zombie.damage,zombie.seq,zombie.claimed_hp,zombie.hp,zombie.max_hp,zombie.tick,zombie.alive
1,0,,,,,,,,,,
2,0,,,,,,,,,,
3,0,,,,,,,,,,
4,12,0x01,10,1,,,,,,,
5,16,0x02,14,,5,1,95,,,,,
6,24,0x03,22,,,,,95,100,1,1
7,0,,,,,,,,,,
8,0,,,,,,,,,,
9,0,,,,,,,,,,
```

`capinfos` 证明容器确实是 text2pcap 写的：`9 packets / 1200 bytes / Capture application: Text2pcap (Wireshark) 4.0.17`。

### 六、真抓一回 loopback：帧长和 demo 不一样（`capture/evidence/live-capture.txt`）

同一份 `zombie.lua` 解析真实抓包（62 个包，`\Device\NPF_Loopback`，非管理员窗口）：

```text
    4 0.000224400    127.0.0.1 → 127.0.0.1    ZOMBIE 56 HELLO client_id=1 version=1
   11 0.407103900    127.0.0.1 → 127.0.0.1    ZOMBIE 60 ATTACK damage=5.00 seq=1 claimed_hp=95.00
   15 0.408787000    127.0.0.1 → 127.0.0.1    ZOMBIE 68 HP_SYNC player_id=1 hp=95.00/100.00 tick=1 alive=1
   51 1.419816600    127.0.0.1 → 127.0.0.1    ZOMBIE 48 BYE
```

为什么 demo 里是 `66 / 70 / 78`，真抓里是 `56 / 60 / 68`？

| 抓法 | 链路层封装 | 链路头 | IP 头 | TCP 头 | 12B HELLO 的帧长 |
|:---:|---|---:|---:|---:|---:|
| demo.pcapng（合成） | Ethernet II | 14 | 20 | 20 | 14+20+20+12 = **66** |
| 真抓 loopback | NULL/Loopback | 4 | 20 | 20 | 4+20+20+12 = **56** |

**差 10 字节，差在链路层头（14 vs 4），不是 TCP 选项。** 这次真抓的 TCP 头就是 20 字节（无选项）。所以「一帧 16 字节的游戏数据，线上跑了 60 字节」，这句话得说清是在哪种链路上量的。

还有一个真实世界的细节：**tap 是两条 TCP 连接**（client→tap:47000、tap→server:47001），所以同一个业务帧在 pcap 里会出现 **两次**——第 4 帧（`65528 → 47000`）和第 9 帧（`65529 → 47001`）是同一个 HELLO，不是重传。对账时按 `opcode + seq` 配，别按帧号。

### 七、反编译：`frame[0] = payload_len + 2`

`re/evidence/zombie_cli_O2s-objdump-p.txt`：客户端 13 个函数来自 `WS2_32.dll`（`WSAStartup / socket / connect / send / recv / select / closesocket / htons / inet_pton / ntohs / getsockname / WSACleanup / WSAGetLastError`）——**socket 能力是借来的，不是编进 exe 的**。

`analyze.ps1` 第 2 步能直接指出 IAT 槽和引用它的指令（真实输出）：

```text
  send IAT slot = 0x14000a608
    140001c4c:	mov    r12,QWORD PTR [rip+0x89b5]        # 0x14000a608
  recv IAT slot = 0x14000a5f8
    140001abf:	call   QWORD PTR [rip+0x8b33]        # 0x14000a5f8
```

Windows 的导入函数是**间接调用**：机器码从 IAT 里取地址再跳，所以反汇编里搜不到 `call send` 的字样；Ghidra 把这种调用标成 `COMPUTED_CALL`，于是「谁调用了 send」可以直接反查：

```text
INFO  DecompileCallersOf.java>   符号: send  地址: EXTERNAL:00000047
INFO  DecompileCallersOf.java>     被引用 @ 14000a608  (DATA)
INFO  DecompileCallersOf.java>     被引用 @ 140001c6a  (COMPUTED_CALL)
INFO  DecompileCallersOf.java>   >>> 调用者函数: FUN_140001be0  入口: 140001be0
```

`FUN_140001be0` 的反编译原文（`re/evidence/zombie_cli_O2s-ghidra-callers.txt`，节选）：

```c
uint FUN_140001be0(SOCKET param_1,undefined1 param_2,longlong param_3,uint param_4,
                  undefined8 param_5)
{
  byte local_168 [4];                  // 4 字节帧头，就在栈上
  undefined1 local_128 [256];

  local_168[0] = (char)param_4 + 2;    // total_len = payload_len + 2（不含自身）
  local_168[1] = 0;                    // 长度高字节
  uVar5 = param_4 + 4;                 // 整帧 = payload_len + 4
  local_168[3] = 0;                    // flags = 0
  local_168[2] = param_2;              // opcode 在第 3 字节
  // ...把 payload 拷到 local_168 + 4（GCC 把 memcpy 展开成 8 字节拷贝）...
  FUN_1400014b0(local_128,local_168,uVar5,0x20);   // hexdump，前 0x20 字节
  while( true ) {
    iVar1 = send(param_1,(char *)(local_168 + iVar4),uVar5 - iVar4,0);
    if (iVar1 < 1) break;
    iVar4 = iVar4 + iVar1;
    if ((int)uVar5 <= iVar4) return 0;  // 循环到发完
  }
  // ...
}
```

把它和第一节的字节表对上（`param_4` 就是 payload 长度）：

| 反编译出来的 | 线上字节 | 结论 |
|---|---|---|
| `local_168[0] = (char)param_4 + 2`（= `frame[0] = payload_len + 2`） | `0e` = 14 = 12 + 2 | `total_len` 不含自身 |
| `local_168[1] = 0` | `00` | 长度高字节 |
| `local_168[2] = param_2`（= `frame[2] = opcode`） | `02` | ATTACK |
| `local_168[3] = 0` | `00` | flags |
| `uVar5 = param_4 + 4`（= `payload_len + 4`） | 16 = 12 + 4 | 整帧长度 |
| 载荷从 `local_168 + 4` 开始 | `00 00 a0 40 …` | 第一个字段是 damage |

收包那侧也能对上：`FUN_140001a20` 里有 `uVar5 = recv(SVar2,(char *)readfds,iVar4,0);`，后面跟着「缓冲区还差多少」的判断——就是第四节那段「不够一帧就继续 recv」的代码。

> `-O2` 会把 `memcpy` 展成按 8 字节倒着拷的 `undefined8` 赋值，还会把 `send` 的调用点变成 `call r12`（寄存器间接调用）。这些是编译器产物，不是源码长这样。

### 八、改 4 个字节：谁说了算（`evidence/trust-client.txt`）

tap 以 `--hack-claimed 1.0` 启动时，会在转发 ATTACK 前把 `claimed_hp` 这 4 个字节改掉，**帧长仍是 16**：

```text
[+  0.511s] HACK C>S ATTACK seq=1: claimed_hp 95.00 -> 1.00  (frame stays 16B, only payload bytes 12..15 changed)
```

（`12..15` 是整帧偏移：帧头 4 字节 + damage 4 字节 + seq 4 字节，正好是第 13~16 个字节。）

| 场景 | 服务端模式 | tap 把 claimed_hp 改成 | 服务端最终血量 |
|:---:|---|---:|---:|
| T1 | `--trust-client` | 1.0 | **1.0** |
| T2 | `--trust-client` | 100.0 | **100.0**（每秒回满） |
| T3 | `--authoritative` | 100.0 | **75.0**（改包无效） |

T1 —— 客户端只该掉到 95，服务端直接信了 1：

```text
[+  0.658s] HP   100.00 -> 1.00  (trust-client: took claimed_hp=1.00 as truth, diff=-99.00)
[+  1.705s] FINAL hp=1.00 max_hp=100.00 alive=1  frames=7 hello=1 attack=5 reject=0 bye=1 unknown=0
```

客户端自己的日志（另一个进程，时间基准不一样）：

```text
[+  0.616s]       RECONCILE tick=1  my_prediction=95.00  server=1.00  delta=-94.00
```

T2 —— 连挨 5 下，血一动不动：

```text
[+  0.663s] HP   100.00 -> 100.00  (trust-client: took claimed_hp=100.00 as truth, diff=0.00)
[+  1.727s] FINAL hp=100.00 max_hp=100.00 alive=1
```

T3 —— 同一个被改过的包，打在权威服务端上，服务端自己算：

```text
[+  0.650s] HP   100.00 -> 95.00  (authoritative: claimed says 100.00, diff=5.00)
[+  0.865s] HP   95.00 -> 90.00  (authoritative: claimed says 100.00, diff=10.00)
[+  1.495s] HP   80.00 -> 75.00  (authoritative: claimed says 100.00, diff=25.00)
[+  1.710s] FINAL hp=75.00 max_hp=100.00 alive=1
```

客户端这边 T3 全是 `HP check ... OK`（它的预测 95/90/…/75 正好等于权威值），**没有 RECONCILE**——因为它没有理由怀疑服务端。

还有第四种：客户端把伤害改成 30（超出单次上限 25），服务端整条请求丢掉（`evidence/run.txt`）：

```text
[+  0.666s] REJECT seq=1 dmg=30.00 out of range (0 < dmg <= 25); hp stays 100.00
[+  0.868s] FINAL hp=100.00 max_hp=100.00 alive=1  frames=3 hello=1 attack=1 reject=1 bye=1 unknown=0
[+  0.610s]       RECONCILE tick=1  my_prediction=70.00  server=100.00  delta=+30.00
```

> **原则**：客户端可以预测，但真相只能由服务端算。**客户端发来的每一个字节，都当成恶意输入。**
> 反过来说，这也是判断「这个协议好不好欺负」的方法：抓包改一个字段 → 看服务端认不认。认 = 有漏洞；不认 = 有校验。

### 九、连接的三态（`evidence/netstat.txt`）

抓包那一刻，server + tap + client 三方都在：

```text
  TCP    127.0.0.1:47000        0.0.0.0:0              LISTENING       45928
  TCP    127.0.0.1:47000        127.0.0.1:60133        ESTABLISHED     45928
  TCP    127.0.0.1:60133        127.0.0.1:47000        ESTABLISHED     36376
  TCP    127.0.0.1:47001        0.0.0.0:0              LISTENING       38592
  TCP    127.0.0.1:47001        127.0.0.1:60134        ESTABLISHED     38592
  TCP    127.0.0.1:60134        127.0.0.1:47001        ESTABLISHED     45928
```

`LISTENING` = 在等电话；`ESTABLISHED` 两行 = 两个方向各一行；`TIME_WAIT` 那一堆是前几次实验留下的，不是 bug。

## 读这个输出，要看什么

| 现象 | 为什么 |
|:---:|---|
| `0e 00 02 00` 四个字节 | `total_len=14`、`opcode=ATTACK`、`flags=0`，整帧 16 |
| `00 00 a0 40` | 小端 float 5.0（`0x40A00000`） |
| 同一帧 `total_len` 比整帧小 2 | 长度字段**不含自身**，这是协议里最容易错的地方 |
| `send()` 4 次，`recv()` 3~4 次 | TCP 是字节流；切点由内核决定，每次都不一样 |
| `recv_chunks=3` / `=1` | 同一帧用了几次 `recv()` 才凑齐；解码器必须维护缓冲区 |
| Wireshark 显示 `ZOMBIE 56/60/68/48` | 真抓 loopback 的链路层是 NULL/Loopback（4 字节），不是 Ethernet（14） |
| 同一个 `opcode+seq` 出现两次 | tap 是两条 TCP 连接，业务帧各走一条；不是重传 |
| `frame[0] = payload_len + 2` | 反编译给出的「协议为什么长这样」，和抓包互为印证 |
| `HP 100.00 -> 1.00` | `--trust-client` 把客户端报的血当真值，改包就回血 |
| `authoritative: claimed says 100.00, diff=25.00` | 权威模式只把客户端的话打进日志，血量自己算 |

## 自测题

> 每道题写清三件事：**考点** · **做完你应该明白什么** · **怎么算真的会了**。

### 自测 1：`0e 00 02 00` 这 4 个字节，能读出几个信息？

**考点**：帧头结构 + 长度口径

**做完你应该明白**
- `total_len = 0x000e = 14`，它数的是「后面还有 14 字节」
- `opcode = 0x02 = ATTACK`，`flags = 0`
- 整帧 = 14 + 2 = 16 **字节**

**怎么算真的会了**
给你任意一帧的前 4 个字节，你能说出它完不完整、是什么操作、后面还差多少字节。

**常见错误**
- 把 `total_len` 当成整帧长度（16），于是所有字段都错 2 字节
- 把 `0e 00` 按大端读成 0x0e00 = 3584，然后怀疑人生

---

### 自测 2：服务端一次 `send()` 了 16006 字节，客户端为什么可能 `recv()` 3 次或 4 次？

**考点**：TCP 是字节流，不是消息流

**做完你应该明白**
- `send()` 只是把字节塞进内核缓冲区，怎么切、怎么合、什么时候到，由 TCP 自己决定
- 同一份代码连跑三次，切点可能是 `4096/8192/3718`、`4096/4096/7814`、`4096/4096/4096/3718`
- 接收端必须「缓冲区 + 循环切帧」：攒够 `2 + total_len` 才切一帧

**怎么算真的会了**
你能说出 `recv_chunks=3` 这个数字是谁数出来的（客户端从第一次 `recv` 到凑齐这一帧用了几次），并写出那个 `while` 循环的伪代码。

**常见错误**
- 以为「一次 `send()` 对应一次 `recv()`」
- 用 `recv()` 的返回值当帧长

---

### 自测 3：`00 00 be 42` 是什么值？为什么不是 0x0000be42？

**考点**：小端 + IEEE-754 float

**做完你应该明白**
- 线上字节是**低位在前**，倒过来读成 `0x42BE0000`
- 按 float 解释是 **95.0**
- 同理 `00 00 c8 42` = 100.0、`00 00 f0 41` = 30.0

**怎么算真的会了**
看到「4 字节、后两个字节是 `42`/`43`/`44`」就能先猜 float，并且能用手算或 Python `struct.unpack('<f', bytes.fromhex('0000be42'))` 验证。

**常见错误**
- 直接按大端 u32 读成 48642，然后和血量对不上

---

### 自测 4：客户端把 `claimed_hp` 改成 1.0，为什么服务端有的版本认、有的不认？

**考点**：权威性 / 信任边界

**做完你应该明白**
- `--trust-client` 用客户端报的 `claimed_hp` 当真相：血从 100 变 1
- `--authoritative` 自己算 `hp -= damage`：同样的包，血照掉到 75
- tap 改包不改帧长（float 还是 4 字节），所以协议层面「看不出被改过」——这正是为什么服务端不能信这个字段

**怎么算真的会了**
你能用 `--hack-claimed` 分别打两种服务端，并解释 `RECONCILE delta` 的符号：T1 里客户端预测 95、服务端给 1，`delta=-94.00`；REJECT 里客户端预测 70、服务端说 100，`delta=+30.00`。

**常见错误**
- 以为「改包成功 = 漏洞存在」，没看服务端到底采信了哪一侧的值

---

### 自测 5：反编译里哪几行能证明「`total_len` 不含它自己」？

**考点**：抓包 ↔ 反编译互相印证

**做完你应该明白**
- `local_168[0] = (char)param_4 + 2`（改名后 `frame[0] = payload_len + 2`）：长度字段 = 载荷 + 2
- `local_168[1] = 0`：长度高字节
- `uVar5 = param_4 + 4`（整帧 = 载荷 + 4）：比长度字段多 2
- 两式相减正好得 2，就是「长度字段不含自身」

**怎么算真的会了**
你能拿 ATTACK 验算：payload 12 → `total_len = 14`、整帧 `uVar5 = 16`，和 `evidence/tap.txt` 的 `0e 00 …` 逐字节对上。

**常见错误**
- 只看抓包不看代码，于是只能说「14 后面还有 14 字节」，说不出为什么
- 把 `param_4` 当成整帧长度（它是 payload 长度）

## 坑

1. **端口 4001 不一定是你的**：一开始按旧约定用 4000/4001，服务端 `bind` 直接 `WSA error 10013`。
   排查顺序：`netsh interface ipv4 show excludedportrange protocol=tcp` 排除 Windows 保留段 → `netstat -ano | findstr 4001` 找到 PID → `Get-Process -Id <pid>` 看是谁。
   本机是 **QQ.exe 从开机起就占着 `127.0.0.1:4001`**，所以换成 tap 47000 / server 47001。**10013 不等于没权限，先看谁在 LISTENING。**
2. **PowerShell 执行策略**：本机默认策略不允许跑未签名 `.ps1`，直接 `.\run.ps1` 会报「未进行数字签名」。
   统一写 `powershell -NoProfile -ExecutionPolicy Bypass -File .\xxx.ps1`（只对这一次进程生效，不改系统设置）。
3. **`py` 启动器会读脚本首行的 shebang**：`mkpcap.py` 首行原来是 `#!/usr/bin/env python3`，`py mkpcap.py` 直接返回 **9009 且没有输出**（启动器去找不存在的 `python3`）；`py -3 mkpcap.py` 才正常。
   本 lab 的两个 Python 脚本已经去掉 shebang（和 lab01 的 `craft.py` 同一处理）。
4. **`Start-Process -ArgumentList` 不会给参数加引号**：BPF 过滤器 `tcp port 47000 or tcp port 47001` 含空格，被拆成多个参数后 dumpcap 报 `Invalid argument: 47001`。
   `capture.ps1` 里对 `-f` / `-w` 的值手动包了引号。
5. **dumpcap 启动后要等它 arm 好再灌流量**：实测只等 1.2 秒就跑靶场，出现 `Packets captured: 0`；等 2.8 秒稳定抓到 62 个包。
   `capture.ps1` 已把等待改成 2.8 秒。
6. **真抓 loopback 的链路层不是 Ethernet**：`capinfos` 显示 `NULL/Loopback`，4 字节族字段 + 20 IP + 20 TCP。
   所以同一条 12 字节 HELLO：demo 里是 66 字节（14 字节以太网头），真抓里是 56 字节。**别拿 demo 的帧长去对真抓包。**
7. **tap 是两条 TCP 连接，同一个业务帧会出现两次**：client→tap:47000 一条、tap→server:47001 一条。pcap 里按 `opcode + seq` 配，别把第二条当重传。
8. **`py` 会多一层进程**：`Start-Process py` 拿到的 PID 是 `py.exe`（netstat.txt 里 `tap=31576 py`），真正占 47000 的是它拉起的 `python.exe` 子进程（`OwningProcess 45928`）。拿 PID 去 taskkill / 对账时要注意这一层。
9. **中文 `.ps1` 必须 UTF-8 with BOM + CRLF**：Windows PowerShell 5.1 会把无 BOM 的 `.ps1` 按 ANSI 解码，中文全乱；本目录含中文的 `.ps1` 都带 BOM。
10. **C 程序的 stdout 重定向到文件是块缓冲**：不 `fflush` 的话，进程被 `Stop-Process` 掐掉时最后几行日志会丢。`zombie.h` 的 `zb_log()` 每行都 flush，`tap.py` 用 `-u` 启动。
11. **pcap 不进仓库**：`.gitignore` 忽略 `*.pcap/.pcapng`（体积 + 可能含敏感信息）。本 lab 的 pcap 用 `mkpcap.py` 现场生成，真抓的 pcap 也只在本地；仓库里只留文本证据。
12. **「抓包必须管理员」在这台机器上不成立**：本机 Npcap 没限制普通权限（reversing-journal 的 `capture.ps1` 也已从「非管理员直接退出」改成「提示后继续」）——`Medium Mandatory Level` 的窗口用 dumpcap 抓 loopback 抓到了 62 个包（`capture/evidence/live-capture.txt`）。
    最稳仍然是管理员窗口；本目录的 `capture.ps1` 因此改成「提示但继续」。
13. **`analyzeHeadless` 不能把同一个文件重复导入同名项目**：Ghidra 的项目是 `<name>.gpr` + `<name>.rep` 两个文件（不是目录），第二次跑会报 `Found conflicting program file in project`。
    `analyze.ps1` 现在会先删掉 `network-journal-lab06.gpr/.rep` 再导入（只删自己这个临时项目）。
14. **`Tee-Object -FilePath` 在 Windows PowerShell 5.1 里写 UTF-16LE**：直接用它会得到 4.7 万字节的「文本」，git 识别成 `Bin`、编辑器打开一片乱码。
    `analyze.ps1` 改成先把 Ghidra 输出收进变量，再用 `[IO.File]::WriteAllLines(..., UTF8)` 落盘，现在 `re/evidence/*.txt` 都是正常的 UTF-8 文本。

## 进阶

- 把 `--attacks` 加到 25，看 `HP_SYNC` 的 `alive` 字段什么时候从 1 变 0（血量到 0 之后服务端怎么处理）；
- 用 `tap.py --hack-claimed` 改 **damage**（不是 claimed），看 `REJECT` 的边界是不是 `0 < dmg <= 25`；
- 写一个「只用一个 `recv()`」的坏解码器（不维护缓冲区），再用 `--recv-buf 4096` 跑 SNAPSHOT，观察它是怎么把帧切碎的；
- 用 `zombie.lua` 的 `pinfo.desegment_len` 处理跨 TCP 段的 16006 字节帧：抓一次 `--snapshot-count 1000` 的真包，`-Y zombie` 应该只出一行 `SNAPSHOT count=1000`；
- 用 x64dbg 在 `ws2_32!send` 下断点看 `RDX` 指向的 16 个字节（Win64: RCX=socket、RDX=buf、R8D=len、R9D=flags），和 `tap.txt` 对照——这是 [reversing-journal 第 15 讲](https://github.com/WMWWZS/reversing-journal/blob/main/docs/15-network.md) 的内容；
- 把 `zombie.lua` 扩展到「按 opcode 高亮错误帧」（`flags != 0`、`total_len` 与 payload 对不上），当一个小型协议检查器。

## 证据文件地图

| 证据 | 看哪一行 |
|---|---|
| `evidence/tap.txt` | 四帧的 hex、方向、长度、`tap summary` 的字节账 |
| `evidence/run.txt` | `FINAL hp=75.00`、`REJECT seq=1 dmg=30.00 …`、`delta=+30.00` |
| `evidence/trust-client.txt` | `took claimed_hp=1.00 as truth`、`took claimed_hp=100.00 as truth`、`authoritative: claimed says 100.00, diff=…` |
| `evidence/big-frame.txt` | `TX send() #1/4…#4/4`、`RX recv n= 4096/8192/3718`、`recv_chunks=3`、D2 的 `recv n=16006` |
| `evidence/big-frame-variance.txt` | 同一条命令三次不同的 `recv` 切点（4096/4096/4096/3718、4096/4096/7814） |
| `evidence/netstat.txt` | `LISTENING` / `ESTABLISHED` / `OwningProcess` |
| `evidence/build.txt` | 4 个 exe 的体积（24.0 KB vs 81.7/84.7 KB）与 gcc 命令 |
| `evidence/imports.txt` | 4 个 exe 从 `WS2_32.dll` 导入的函数块 |
| `capture/evidence/demo-summary.txt` | `ZOMBIE 66/70/78 …` 的 Info 列 |
| `capture/evidence/demo-fields.txt` | `zombie.*` 字段表（`total_len 10/14/22`） |
| `capture/evidence/demo-dissect.txt` | `ZOMBIE HP_SYNC, 24 bytes on the wire` / `Total Length: 22` |
| `capture/evidence/live-capture.txt` | 真抓 loopback：62 包、`ZOMBIE 56/60/68/48`、NULL/Loopback 封装 |
| `capture/evidence/demo-capinfos.txt` | `9 packets / 1200 bytes / Text2pcap 4.0.17` |
| `re/evidence/zombie_cli_O2s-objdump-p.txt` | WS2_32 导入表（13 个函数） |
| `re/evidence/zombie_cli_O2s-ghidra-callers.txt` | `local_168[0] = (char)param_4 + 2`、`uVar5 = param_4 + 4`、`FUN_140001be0` |

## 参考

- 协议本身：本 lab 的 `src/zombie.h`（帧结构、小端读写、端口与默认值）
- 抓包解析器：[Wireshark Lua API](https://www.wireshark.org/docs/wsug_html_chunked/wsluarm.html)（`Proto` / `ProtoField` / TCP desegmentation）
- 字节序与 float：IEEE 754、[docs/09 · 自定义二进制协议](../../docs/09-custom-binary-protocol.md)
- Win64 调用约定与 x64dbg：<https://github.com/WMWWZS/reversing-journal> 的 `docs/13`、`docs/15`
- 原始靶场与全部原始日志：<https://github.com/WMWWZS/reversing-journal/tree/main/labs/07-net-os>
