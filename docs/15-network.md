# 15 · 网络通信：从一行 `send()` 到网线上的 12 个字节

> 前置：14 讲操作系统（进程/句柄/内核态）。
> 这一课的全部字节都来自 `labs/07-net-os/net` 里那个真跑起来的靶场（TCP、127.0.0.1、真掉血），
> 原始日志在 `labs/07-net-os/net/evidence/`。抓包解析器在 `labs/07-net-os/capture/`。
>
> 📚 同一个主题的原理版在姊妹仓库 [network-journal](https://github.com/WMWWZS/network-journal)（12 周课 + labs/06-zombie-protocol）；本篇是它的**逆向视角**版本：同一批抓包，换成从 exe 内部看。

## 0. 先看真的：一次攻击，线上跑过去 16 个字节

`tap.py` 是夹在客户端和服务端之间的一个"电线夹子"，它把每个 TCP chunk 原样打印出来：

```text
[+  0.066s] C>S    12B  0a 00 01 00 01 00 00 00 01 00 00 00
[+  0.477s] C>S    16B  0e 00 02 00 00 00 a0 40 01 00 00 00 00 00 be 42
[+  0.479s] S>C    24B  16 00 03 00 01 00 00 00 00 00 be 42 00 00 c8 42 01 00 00 00 01 00 00 00
[+  1.531s] C>S     4B  02 00 05 00
```

四帧，12/16/24/4 字节。（时间戳每次跑都不一样，这只是我这一次的。）这就是"一个联机游戏"在网络上真正的样子：
**没有 JSON，没有 `{"hp":95}`，只有一串十六进制。**

## 一、把一帧拆开：每个字节都有主

以 ATTACK（16 字节）为例：

```text
0e 00 02 00 00 00 a0 40 01 00 00 00 00 00 be 42
└─┬─┘ └┬┘ └┬┘ └───┬───┘ └───┬───┘ └───┬───┘
  │    │   │       │         │         └── claimed_hp = 95.0 (f32)
  │    │   │       │         └──────────── seq        = 1   (u32)
  │    │   │       └────────────────────── damage     = 5.0 (f32)
  │    │   └────────────────────────────── flags      = 0
  │    └────────────────────────────────── opcode     = 0x02 (ATTACK)
  └─────────────────────────────────────── total_len  = 0x000e = 14
```

**关键规则：`total_len` 不含它自己那 2 字节**，它数的是"后面还有多少"。
所以 14 + 2 = 16，正好是这一帧的总长度。四个 opcode 全部对得上：

| opcode | 帧 | total_len | 整帧 | 校验 |
|---|---|---|---|---|
| `0x01` HELLO | `0a 00 01 00 …` | 10 | 12 | 10+2 ✓ |
| `0x02` ATTACK | `0e 00 02 00 …` | 14 | 16 | 14+2 ✓ |
| `0x03` HP_SYNC | `16 00 03 00 …` | 22 | 24 | 22+2 ✓ |
| `0x05` BYE | `02 00 05 00` | 2 | 4 | 2+2 ✓ |

> 这就是"协议逆向"最核心的动作：**从字节里猜结构，再用长度/枚举值验证。**
> 你猜错一个字段大小，长度就对不上——长度字段是自带的答案。

HP_SYNC 的 24 字节拆完是这样（服务端说"你现在 95 血"）：

```text
16 00 | 03 | 00 | 01 00 00 00 | 00 00 be 42 | 00 00 c8 42 | 01 00 00 00 | 01 00 00 00
len   op   fl   player_id=1    hp=95.0        max_hp=100.0   tick=1         alive=1 + 3字节pad
```

## 二、TCP 是**字节流**，不是"消息"——为什么必须有帧头

这是网络编程里最要命的一个认知。先看证据

服务端要发一个 16006 字节的大帧（SNAPSHOT：1000 个实体），它分 **4 次 `send()`** 写出去：

```text
send() #1/4   4096 bytes
send() #2/4   4096 bytes
send() #3/4   4096 bytes
send() #4/4   3718 bytes
```

客户端那边收到的是：

```text
RX recv n= 4096
RX recv n= 8192
RX recv n= 3718     ← 上一次跑同一条命令，这里是 4096 / 4096 / 7814
```

**一共 16006 字节，一次没少，但切法完全不由发送方决定——连每次跑都不一样。**
（`tap` 拦到的 chunk 边界也跟着变，因为 TCP 只是把字节搬过去，不保证"按发送的顺序原样分块"。）

这就是 TCP 的真相：

| 你以为的 | 实际的 |
|---|---|
| `send()` 一次 = 一个包 | `send()` 只是"塞进缓冲区"，怎么切、怎么合、什么时候到，**TCP 自己决定** |
| `recv()` 一次 = 一条消息 | `recv()` 只保证"给你当前能给的字节"，可能半帧、也可能 3 帧黏在一起 |

**所以任何自定义协议都必须自己定帧头**（长度 + opcode），接收端维护一个缓冲区：

```c
// 已经收到的字节不够一帧的 total_len 时，就继续 recv，凑够再解析
if (have < 2) continue;                  // 连长度字段都没齐
total = rd16(buf);                       // 读 total_len（小端）
if (have < 2 + total) continue;          // 这一帧还没到齐
parse_frame(buf, 2 + total);             // 齐了，解析
memmove(buf, buf + 2 + total, have - 2 - total);   // 把剩下的挪到前面
```

> 靶场客户端就是这么写的（`zombie_cli.c` 里的 `recv_frame()`）。
> 不写这段，你的程序在"一次攻击"时看着没事，一到大包就随机崩——这是新手最常见的网络 bug。

## 三、包头到底加了多少字节

`capture/demo.pcapng` 里 tshark 量出来的（这是"一帧 ZOMBIE"在链路层的完整大小）：

```text
   4 …… ZOMBIE 66 HELLO client_id=1 version=1
   5 …… ZOMBIE 70 ATTACK damage=5.00 seq=1 claimed_hp=95.00
   6 …… ZOMBIE 78 HP_SYNC player_id=1 hp=95.00/100.00 tick=1 alive=1
```

减一下就出来了：

| 帧 | 线上总长 | ZOMBIE 载荷 | 差 = 包头 |
|---|---|---|---|
| HELLO | 66 | 12 | 54 |
| ATTACK | 70 | 16 | 54 |
| HP_SYNC | 78 | 24 | 54 |

**54 = 14（以太网头）+ 20（IPv4 头）+ 20（TCP 头）。**

> **同一个协议，换一个抓包点，包头就换一种算法。** `demo.pcapng` 是以太网封装（14 字节），所以是 54；
> 真在 loopback 上抓时，Npcap 用的是 **NULL/Loopback** 封装（开头 4 字节是地址族，不是以太网头），包头只有 **44**。
> 实测证据（`capture/evidence/live-capture.txt`，HELLO 帧 `frame.len=56` = 4+20+20+12）：
>
> ```text
> 4   56  12   0x01  10      ← HELLO
> 11  60  16   0x02  14      ← ATTACK
> 15  68  24   0x03  22      ← HP_SYNC
> ```
>
> 所以别背「包头 54 字节」——先问**在哪抓的**：以太网 54、loopback 44、VLAN 还要再多 4。

结论：**以太网上，你每发 16 字节游戏数据，线路上其实跑 70 字节；loopback 上是 60 字节。** 带宽就是这么吃掉的。

## 四、握手和挥手：连接不是"打开就有"

`netstat -ano` 在靶场运行时的真实输出（`net/evidence/netstat.txt`）：

```text
  TCP    127.0.0.1:47000    0.0.0.0:0           LISTENING     5640
  TCP    127.0.0.1:47000    127.0.0.1:49772     ESTABLISHED   5640
  TCP    127.0.0.1:49772    127.0.0.1:47000     ESTABLISHED   38344
  TCP    127.0.0.1:47000    127.0.0.1:49762     TIME_WAIT     0
```

三行，三个状态，正好是 TCP 的一生：

| 状态 | 谁在 | 人话 |
|---|---|---|
| `LISTENING` | 服务端/tap | "我在 47000 等电话" |
| `ESTABLISHED` | 双方 | "通了，可以说话了"。**注意两行，两个方向各一行，PID 不同** |
| `TIME_WAIT` | 已经关掉的那一方 | "挂了电话，但还要等 2 分钟才允许复用这个号码" |

`TIME_WAIT` 那一堆是前几次实验留下的。**这不是 bug**：主动关闭的一方要等
2×MSL（Windows 默认约 120 秒），确保最后的 ACK 不会丢失、旧连接的迷路包不会污染新连接。

`demo.pcapng` 里完整的 9 个包就是教科书那张图：

```text
1 client→server  SYN        Seq=0            "我想连"
2 server→client  SYN,ACK    Seq=0 Ack=1      "行，我也连"
3 client→server  ACK        Seq=1 Ack=1      "收到"
4-6              HELLO / ATTACK / HP_SYNC     ← 真正的数据
7 client→server  FIN,ACK    Seq=29           "我说完了"
8 server→client  FIN,ACK    Seq=25           "我也说完了"
9 client→server  ACK        Seq=30           "收到"
```

> **逆向联机游戏时，你最先该看的就是这个**：连接什么时候建立（登录）、
> 什么时候保持（心跳包）、什么时候断开（掉线判定）。很多外挂的"防掉线"就是在伪造心跳。

## 五、小端和 float：把 `00 00 a0 40` 看成 `5.0`

这是看包必须会的手算，练三次就成肌肉记忆。

**float 5.0** 的 IEEE-754 编码是 `0x40A00000`（这个可以背下来：5.0 = 0x40A00000）。
x86 是**小端**（低字节在前），所以写到线上是：

```text
0x40A00000  →  00 00 A0 40
```

验证一下另外几个：

| 你看到的字节 | 反过来读 | 十六进制 | 浮点值 |
|---|---|---|---|
| `00 00 be 42` | `0x42BE0000` | 42BE0000 | **95.0** |
| `00 00 c8 42` | `0x42C80000` | 42C80000 | **100.0** |
| `00 00 f0 41` | `0x41F00000` | 41F00000 | **30.0** |
| `00 00 80 3f` | `0x3F800000` | 3F800000 | **1.0** |

> 逆向时看到 `00 00 C8 42` 这种"后半段是 42/43/44"的 4 字节，几乎一定是 float，数值大概在 100~1000。
> 这是从一堆字节里挖出"血量/坐标/伤害"的第一直觉。

## 六、从逆向看同一帧：`send()` 的四个参数

现在换个视角。你拿到一个只有 `.exe` 的客户端（`zombie_cli_O2s.exe`，`-O2 -s`，没有符号），
怎么找到"它在哪里拼这个 ATTACK 帧"？

### 第 1 步：看导入表——联网能力是借来的

`objdump -p zombie_cli_O2s.exe` 的真实输出（`re/evidence/zombie_cli_O2s-objdump-p.txt`）：

```text
	DLL Name: KERNEL32.dll
	DLL Name: api-ms-win-crt-*.dll        （C 运行时的十来个转发 DLL）
	DLL Name: WS2_32.dll
	0000a5c0  <none>  005a  WSAStartup
	0000a5c8  <none>  00a5  closesocket
	0000a5d0  <none>  00a6  connect
	0000a5e0  <none>  00b5  htons
	0000a5e8  <none>  00b9  inet_pton
	0000a5f8  <none>  00be  recv
	0000a600  <none>  00c0  select
	0000a608  <none>  00c1  send
	0000a610  <none>  00c5  socket
```

**这一步就告诉你三件事**：

1. 它用 Winsock（Windows 的 socket API），不是 HTTP 库、也没有加密库 → 协议是裸 TCP 二进制。
2. 有 `select` → 它会在等数据时**带超时**（就是日志里 `WARN no HP_SYNC for tick=1 within 3000 ms` 的来源）。
3. 有 `inet_pton` → 连接地址是字符串解析来的，不是写死的。

> **逆向顺序建议**：先看导入表，比直接扎进反汇编快 10 倍。
> 有 `ws2_32` → 联网；有 `crypt32`/`bcrypt` → 可能有加密；有 `CreateToolhelp32Snapshot` → 可能在枚举进程（反作弊/外挂都在用）。

### 第 2 步：谁调用了 `send`——Ghidra 直接给答案

`-O2 -s` 剥了符号，但导入表还在。**从"外部函数"反查"谁引用它"**，一步就落到发包代码上。
`re/analyze.ps1` 的真实输出：

```text
外部函数: send  地址: EXTERNAL:00000047
  被引用 @ 14000a608  (DATA)            ← 导入表里的那一格（IAT）
  被引用 @ 140001c6a  (COMPUTED_CALL)   ← 真正调用它的指令
  >>> 调用者函数: FUN_140001be0  入口: 140001be0
```

**为什么反汇编里搜不到 `call send` 这四个字母？**
因为 Windows 导入函数是**通过 IAT 间接调用**的：机器码是
`FF 15 xx xx xx xx  →  call qword ptr [rip+0x...]`，从内存里取出函数地址再跳。
Ghidra 把它标记成 `COMPUTED_CALL`——这就是"导入表 → IAT → 间接调用"的实物。

### 第 3 步：反编译出来，协议就摊在脸上了

`FUN_140001be0` 的反编译（原样输出，只做了三处改名方便阅读：
`local_168`→`frame`、`local_128`→`hexbuf`、`FUN_1400014b0`→`hexdump`，参数名按用途起）：

```c
uint FUN_140001be0(SOCKET s, undefined1 opcode, longlong payload, uint payload_len, undefined8 name)
{
  byte frame[4];                       // ← 4 字节帧头，就在栈上！
  undefined1 hexbuf[256];

  frame[0] = (char)payload_len + 2;    // ← total_len = 载荷长度 + 2
  frame[1] = 0;                        //   高字节 0（长度 < 256）
  frame[3] = 0;                        // ← flags = 0
  frame[2] = opcode;                   // ← opcode 在第 3 字节

  uVar5 = payload_len + 4;             // ← 整帧 = 载荷 + 4 字节帧头
  if (payload_len != 0) {
      /* GCC 把 memcpy 展开成按 8 字节倒着拷 */
      *(undefined8 *)(frame + (payload_len - 4)) =
           *(undefined8 *)(payload + payload_len - 8);
      ...
  }
  hexdump(hexbuf, frame, uVar5, 0x20);  // 打日志：前 0x20 字节
  printf("TX  %2dB %s %s", uVar5, hexbuf, name);

  iVar4 = 0;
  while (true) {
      iVar1 = send(s, (char *)(frame + iVar4), uVar5 - iVar4, 0);
      if (iVar1 < 1) break;             // 出错
      iVar4 += iVar1;
      if ((int)uVar5 <= iVar4) return 0; // 全发出去了
  }
  ...
}
```

对照第一节的字节表，每一行都对得上：

| 反编译出来的 | 线上字节 | 结论 |
|---|---|---|
| `frame[0] = payload_len + 2` | `0e` = 14 = 12+2 | total_len 不含自身 |
| `frame[1] = 0` | `00` | 长度高字节 |
| `frame[2] = opcode` | `02` | ATTACK |
| `frame[3] = 0` | `00` | flags |
| 载荷从 `frame+4` 开始 | `00 00 a0 40 …` | 参数从第 4 字节起 |
| `uVar5 = payload_len + 4` | 16 = 12+4 | 整帧长度 |

**这就是"协议逆向"的完整闭环**：抓包得到字节 → 反编译得到代码 → 两边互相印证。
你甚至不需要源码：`frame[0] = 长度 + 2` 这一行，就是协议的全部秘密。

> 顺带两个真实世界细节：
> 1. 那个 `while` 循环是**必须**的——`send` 可能只发出去一部分（尤其大包），
>    所以真实代码永远要循环到发完。新手写的 `send(...)` 裸调用，在局域网里 99% 没事，
>    一上公网就丢包。
> 2. `-O2` 把 `memcpy` 展开成"从尾部倒着按 8 字节拷贝"，你会看到一堆
>    `undefined8` 强转——这是编译器的产物，不是源码长这样。看多了就能一眼认出 `memcpy`。

### 第 4 步：Win64 调用约定——参数在寄存器里

`send` 的原型是：

```c
int send(SOCKET s, const char *buf, int len, int flags);
```

x64 Windows 的调用约定（和你在 13 讲学的 `RCX/RDX` 是同一套）：

| 参数 | 寄存器 | 含义 |
|---|---|---|
| 1 | **RCX** | socket 句柄 |
| 2 | **RDX** | 缓冲区地址（就是那 16 个字节所在的地方） |
| 3 | **R8D** | 长度（16） |
| 4 | **R9D** | flags（0） |

**这就是"动态验证协议"最爽的一刻**：在 `ws2_32!send` 上下断点，看 `RDX` 指向什么。

### 第 5 步：x64dbg 实操（5 分钟，真的能看到那 16 个字节）

1. 打开 `zombie_srv_O2s.exe`（先跑服务端），再跑 `zombie_cli_O2s.exe`
2. x64dbg `文件 → 打开` 选 `zombie_cli.exe`（或附加到它）
3. `符号` 面板 → `ws2_32.dll` → 找 `send` → 右键"在导入上设断点"
   （或者命令行：`bp ws2_32.send`）
4. `F9` 跑，断下时看右上寄存器窗口：

```text
RCX = 00000000000000xx     ← socket 句柄（和 netstat 里的 PID 是同一个进程）
RDX = 000000xxxxxx         ← 缓冲区地址
R8  = 0000000000000010     ← 16
```

5. `Ctrl+G` 跳到 `RDX` 的值 → 右键"在内存窗口显示" → 选十六进制 → 你会看到：

```text
0e 00 02 00 00 00 a0 40 01 00 00 00 00 00 be 42
```

**和 tap 抓到的字节一模一样。** 到这里，"内存里的结构体 → 网络上的字节"这条链就闭合了：

```
结构体字段  →  编译成"往 buffer+偏移 写 4 字节"  →  send(RDX=buffer)  →  线上字节
```

> 顺带一个反作弊视角：**客户端能改的东西，只有 send 之前的缓冲区。**
> 所以在 `send` 下断点改 `RDX` 指向的内容，是外挂最原始的做法；
> 而检测方要防的，就是"有没有人在 send 之前改了不该改的数"。

## 七、信任边界：改一个 float，血从 100 变成 1

这一节是整门课的安全落点。靶场里 `tap.py --hack-claimed 1.0` 会**在转发时改掉线上那 4 个字节**，
帧长度完全不变（float 还是 4 字节），所以协议层面"看不出被改过"。

### 场景 1：服务端说"我信客户端"（`--trust-client`）

```text
[RX]  16B  0e 00 02 00 00 00 a0 40 01 00 00 00 00 00 80 3f   ATTACK dmg=5.00 claimed=1.00
[HP]  100.00 -> 1.00   (trust-client: took claimed_hp=1.00 as truth, diff=-99.00)
```

血量直接变成 1。**这就是"改包回血/锁血"的全部原理**，没有任何高深技术。

连客户端自己都愣了（它本地预测是 95，结果收到 1）：

```text
RECONCILE tick=1  my_prediction=95.00  server=1.00  delta=-94.00
```

### 场景 2：把 claim 改成 100（`--hack-claimed 100.0`）

```text
[RX]  16B  … 00 00 c8 42   ATTACK dmg=5.00 claimed=100.00
[HP]  100.00 -> 100.00   (trust-client: took claimed_hp=100.00 as truth, diff=0.00)
```

挨了 5 下，血一动不动。**这就是"锁血"**：不是不让掉血，是每帧都告诉服务端"我还是满血"。

### 场景 3：同样的包，打 `--authoritative` 服务端

```text
[RX]  16B  … 00 00 c8 42   ATTACK dmg=5.00 claimed=100.00
[HP]  100.00 -> 95.00   (authoritative: claimed says 100.00, diff=5.00)
[RX]  …                   ATTACK dmg=5.00 claimed=100.00
[HP]   95.00 -> 90.00   (authoritative: claimed says 100.00, diff=10.00)
…                        （diff 一路涨到 25.00，血照掉到 75.00）
```

**同一个被改过的包，服务端当没看见 `claimed_hp`，自己算 `100 - 5 = 95`。**
客户端也没理由校正，因为它的预测本来就是对的（95）。

**这就是"客户端预测 + 服务端校正"在真实字节流里长什么样**：
客户端可以猜，猜错了服务端会把真值推回来；而客户端说什么，服务端可以完全不信。

### 场景 4：连"伤害"都校验

客户端发 `--dmg 30`（超出单次上限 25）：

```text
REJECT seq=1 dmg=30.00 out of range (0 < dmg <= 25); hp stays 100.00
```

血量纹丝不动，客户端反而收到一个"你预测 70，其实是 100"的校正（`RECONCILE delta=+30.00`）。

> **原则（抄在任何联机项目的墙上）：客户端发来的每一个字节，都当成恶意输入。**
> 你可以在客户端做预测（为了手感），但**真相只能由服务端算**。
>
> 反过来说，这也是你判断"这个游戏好不好欺负"的方法：
> 抓包改一个字段 → 看服务端认不认。认 = 有漏洞；不认 = 有校验。


## 八、自己抓一次（本机实测：普通窗口也能抓，2 分钟）

```powershell
cd E:\harness\reversing-journal\labs\07-net-os\capture
powershell -ExecutionPolicy Bypass -File .\capture.ps1
# 它开始抓包后，另开一个窗口跑靶场：
cd ..\net ; powershell -ExecutionPolicy Bypass -File .\run.ps1
# 回到抓包窗口按回车停止
```

> 权限：这台机器上**普通（非管理员）窗口实测也能抓 loopback**——`capture/evidence/live-capture.txt` 里
> 是 Medium 完整性级别抓到的 62 个包。脚本在非管理员时只警告、不退出；
> 万一 dumpcap 报 `You do not have permission`，再换管理员窗口。

抓完你会看到同一帧**出现两次**（`frame 4` 和 `frame 9` 都是 HELLO）：
因为 tap 是中间人，客户端→tap、tap→服务端 是**两条 TCP 连接**，各被抓了一遍。

真实抓包的帧长（`tshark -Y zombie -T fields -e frame.len -e tcp.len` 原样）：

```text
4   56  12   0x01  10      ← HELLO
11  60  16   0x02  14      ← ATTACK
15  68  24   0x03  22      ← HP_SYNC
51  48   4   0x05   2      ← BYE
```

**56/60/68，不是 demo 里的 66/70/78。** 差的 10 字节在链路层：

| 抓包点 | 链路层 | 头部 | HELLO 总长 |
|---|---|---|---|
| `demo.pcapng`（text2pcap 合成以太网） | Ethernet II | 14+20+20 = **54** | 66 |
| 真实 loopback（Npcap） | **NULL/Loopback**（4 字节） | 4+20+20 = **44** | 56 |

在 `-V` 里点开一帧，你能逐层看到：

```text
Frame → Null/Loopback → Internet Protocol Version 4 → Transmission Control Protocol → ZOMBIE
```

**这就是「封包」两个字的实物**：一层套一层，每层只关心自己的头。

> 为什么必须管理员？因为从网卡抓包要用 Npcap 驱动，那是内核里的东西（14 讲：用户态碰不到）。
> 这也顺便解释了一件事：**能抓包 = 能拿到明文**，所以现代游戏协议要么 TLS，要么自己加密。

## 九、这一课和 06 讲的关系

`docs/06-network-protocol.md` 是理论总览（协议分类、加密手段、服务端校验清单）。
这一篇是它的**实物版**：

| 06 讲说的 | 这一课看到的 |
|---|---|
| "长度 + opcode + payload" | `0e 00` + `02` + 12 字节，用长度自校验 |
| "TCP 是流" | 16006 字节被 recv 成 4096/4096/7814 |
| "客户端预测 + 服务端校正" | `RECONCILE delta=+94.00` |
| "客户端发来的每个字节都是恶意输入" | `--hack-claimed 1.0` 把血改成 1 |
| "加密只是提高成本" | 明文的话，tap 一行代码就改了 |

## 自测

**1. `0e 00 02 00` 这 4 个字节，能读出几个信息？**

- 考点：帧头结构
- 应明白：`total_len=0x000e=14`（后面还有 14 字节），`opcode=0x02`（ATTACK），`flags=0`，整帧 16 字节
- 算会：给你任意一帧，你能说出它是不是完整的、是什么操作

**2. 服务端 `send()` 了一次 16006 字节，客户端为什么可能收到 3 次？**

- 考点：TCP 是字节流
- 应明白：`send` 只是进缓冲区，切分由 TCP 决定；接收端必须按长度字段自己拼帧
- 算会：你能写出"缓冲区 + 读长度 + 不够就继续读"的循环

**3. `01 00 00 00 00 00 be 42` 后面 4 个字节是什么值？**

- 考点：小端 + float
- 应明白：`00 00 be 42` → `0x42BE0000` → 95.0
- 算会：看到 `00 00 c8 42` 能立刻说出 100.0

**4. 客户端把 `claimed_hp` 改成 1.0，为什么服务端有的版本认、有的不认？**

- 考点：权威性
- 应明白：`--trust-client` 用客户端的值当真相；`--authoritative` 自己算，客户端只影响日志
- 算会：你能说出"为什么服务端必须自己算，客户端只配做预测"

**5. x64dbg 断在 `ws2_32!send`，你要看要发的那 16 个字节，该看哪个寄存器？**

- 考点：Win64 调用约定
- 应明白：第 2 个参数在 `RDX`，是指向缓冲区的指针；长度在 `R8D`
- 算会：你能用 `RDX` 去内存窗口 dump 出那 16 字节，并和抓包对上
