# net-lab NOTES

> 给 Lead / 其他 lab 看的真实记录：我跑了什么、看到什么、哪里和预期不一样。
> 所有 evidence\*.txt 里的内容都是进程的原样 stdout，没有手改；本文件里的引用也一样。
> 协议、帧格式、端口都以 `E:\harness\reversing-journal\labs\07-net-os\BRIEF.md` 为准。

---

## 0. 交付清单

```
E:\harness\reversing-journal\labs\07-net-os\net\
├── zombie.h         协议唯一定义（帧结构 + 小端读写 + 日志工具）
├── zombie_srv.c     服务端（--authoritative / --trust-client）
├── zombie_cli.c     客户端（本地预测 + RECONCILE + 每块 recv 打字节数）
├── tap.py           中间人：47000 -> 47001，双向转发 + 每 chunk 时间/方向/长度/hex
│                    另有 --hack-claimed X：路上改 ATTACK.claimed_hp（漏洞演示用）
├── build.ps1        编译 4 个 exe，写 evidence\build.txt + evidence\imports.txt
├── run.ps1          一键跑 10 个场景，写 evidence\ 下 7 个 txt，最后自检 PASS/FAIL
├── bin\             zombie_srv_O0.exe / zombie_srv_O2s.exe / zombie_cli_O0.exe / zombie_cli_O2s.exe
└── evidence\        build.txt imports.txt run.txt tap.txt netstat.txt trust-client.txt big-frame.txt
```

一键跑（**必须带 `-ExecutionPolicy Bypass`**，本机执行策略禁止未签名脚本）：

```powershell
cd E:\harness\reversing-journal\labs\07-net-os\net
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\run.ps1
```

`run.ps1` 最后会打印 13 条自检，我这边最后一次是：

```
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
VERDICT: PASS
```

手动三方各开一个窗口（默认端口就是 47000/47001，不用带参数也行）：

```powershell
.\bin\zombie_srv_O2s.exe --port 47001 --bind 127.0.0.1 --authoritative
<python> -u tap.py --listen-port 47000 --target-port 47001
.\bin\zombie_cli_O2s.exe --host 127.0.0.1 --port 47000 --attacks 5 --dmg 5
```

---

## 1. 端口为什么是 47000/47001（真实排查过程，Lead 要写进教程的那条坑）

最开始按旧约定用 `4000/4001`。服务端起不来：

```
PS> .\bin\zombie_srv_O2s.exe --port 4001 --authoritative
[+  0.001s] bind 127.0.0.1:4001 failed, WSA error 10013
```

`WSA error 10013` = `WSAEACCES`（"以一种不被允许的方式访问套接字"）。**注意它不是说没权限**，
而是端口已经被别的 *监听 socket* 占着时，Windows 用 10013 而不是大家熟悉的 10048 来拒绝 bind。

第一步先排除"Windows 保留/排除端口"这种常见猜测：

```
PS> netsh interface ipv4 show excludedportrange protocol=tcp

Protocol tcp Port Exclusion Ranges
Start Port    End Port
        80          80
      5357        5357
     13508       13508
     50000       50059     * - Administered port exclusions.
```

4001 不在名单里 → 不是保留端口，那就是真有程序占着。直接看谁：

```
PS> netstat -ano | findstr 4001
  TCP    127.0.0.1:4001         0.0.0.0:0              LISTENING       25492

PS> Get-Process -Id 25492 | Select-Object Id,ProcessName,Path,StartTime
   Id ProcessName Path                                    StartTime
25492 QQ          C:\Program Files\Tencent\QQNT\QQ.exe   2026/11/22 18:21:49
```

**结论：QQ.exe（PID 25492）从开机起就占着 `127.0.0.1:4001`，我们改不了它。**
这不是我上次跑剩的僵尸进程（我自己的 `zombie_*` 进程当时一个都不在），也不是端口排除。

所以按 Lead 定的新端口换成 **服务端 47001 / tap 47000**，并做了两件事：

1. `run.ps1` 启动前先探测，**给人话提示而不是抛异常**（探测代码在 `Test-PortTaken`）：

```powershell
if (Test-PortTaken -port $srvPort -what 'server') { $badPort = $true }
if (Test-PortTaken -port $tapPort -what 'tap')    { $badPort = $true }
# 被占时打印：
#   ERROR: the server port 47001 is already LISTENING on this host.
#          owner : pid 1234 (SomeProgram)
#          fix 1 : close that program -> taskkill /PID 1234 /F
#          fix 2 : run the lab on other ports -> ... run.ps1 -SrvPort 47101 -TapPort 47100
```

2. 端口仍然走命令行参数，`run.ps1` 额外支持 `-SrvPort/-TapPort`，换个干净端口随时能跑：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\run.ps1 -SrvPort 47101 -TapPort 47100
```

更换后实测在跑的时候：

```
PS> netstat -ano | findstr 47001
  TCP    127.0.0.1:47001        0.0.0.0:0              LISTENING       7772
  TCP    127.0.0.1:47001        127.0.0.1:49773        ESTABLISHED     7772
```

`run.txt` 的 commands 段保留了这一行说明（只此一处出现旧端口）：

```
note: ports are 47000/47001 (BRIEF), not 4000/4001: on this host QQ.exe owns 127.0.0.1:4001,
so the server got "bind failed, WSA error 10013" there. See NOTES.md.
```

> 提醒 Lead：`BRIEF.md` 里 capture-lab 那一节还是旧端口（`dumpcap -f "tcp port 4000 or tcp port 4001"`，
> net-lab 验收项里也还写着 `findstr 4000`）。这两个地方我没权限改，需要你同步成 47000/47001。

---

## 2. 协议落地：每一帧真实的字节

`total_len` = 后面所有字节数 = `1(opcode) + 1(flags) + payload_len`，
所以**整帧字节数 = 2 + total_len**（这里有个很容易踩的差 2：HELLO 的 payload 是 8，`total_len` 是 10，
但线上整帧是 12 字节）。下面的 hex 全部来自 `evidence\run.txt` / `tap.txt`，不是手写的。

### HELLO（12 字节）

```
[+  0.002s] TX  12B 0a 00 01 00 01 00 00 00 01 00 00 00  HELLO client_id=1 version=1
```

```
0a 00            total_len = 0x000a = 10   （= 1 + 1 + 8）
01               opcode    = 0x01 HELLO
00               flags     = 0
01 00 00 00      client_id = 1
01 00 00 00      version   = 1
```

### ATTACK（16 字节）

```
[+  0.408s] TX  16B 0e 00 02 00 00 00 a0 40 01 00 00 00 00 00 be 42  ATTACK dmg=5.00 seq=1 claimed=95.00
```

```
0e 00            total_len = 14  （= 1 + 1 + 12）
02               opcode    = 0x02 ATTACK
00               flags     = 0
00 00 a0 40      damage     = 0x40a00000 = 5.0f   （float 5.0 的位模式，小端存）
01 00 00 00      seq        = 1
00 00 be 42      claimed_hp = 0x42be0000 = 95.0f
```

### HP_SYNC（24 字节，payload 正好 20）

```
[+  1.448s] TX  24B 16 00 03 00 01 00 00 00 00 00 96 42 00 00 c8 42 05 00 00 00 01 00 00 00  HP_SYNC hp=75.00 max=100.00 tick=5 alive=1
```

```
16 00            total_len = 0x0016 = 22  （= 1+1+20）
03               opcode    = 0x03 HP_SYNC
00               flags     = 0
01 00 00 00      player_id = 1
00 00 96 42      hp        = 0x42960000 = 75.0f
00 00 c8 42      max_hp    = 0x42c80000 = 100.0f
05 00 00 00      tick      = 5
01               alive     = 1
00 00 00         pad[3]
```

### SNAPSHOT（count=1000 时整帧 16006 字节）

```
[+  0.204s] TX  16006B 84 3e 04 00 e8 03 00 00 00 00 ...  SNAPSHOT count=1000 payload=16002
```

```
84 3e            total_len = 0x3e84 = 16004 （= 1+1+2+16*1000）
04               opcode    = 0x04 SNAPSHOT
00               flags     = 0
e8 03            count     = 0x03e8 = 1000
之后 1000 * (u32 id, f32 x, f32 y, f32 hp) = 16000 字节
```

客户端解出来的第一/最后一条实体（证明整包真的到了，而不是只到了个长度）：

```
      SNAPSHOT first={id=0 x=0.00 y=0.00 hp=100.00} last={id=999 x=1498.50 y=499.50 hp=1.00}
```

### BYE（4 字节）

```
[+  1.652s] RX   4B 02 00 05 00  BYE
```

`total_len = 2`（payload 空），正好是 BRIEF 里写的"最小帧 4 字节"。

---

## 3. evidence\ 每个文件里该看哪一行

### `build.txt` — 4 个 exe（两个 -O0 -g，两个 -O2 -s）

```
zombie_cli_O0.exe         86704 bytes    84.7 KB
zombie_cli_O2s.exe        24576 bytes    24.0 KB
zombie_srv_O0.exe         83633 bytes    81.7 KB
zombie_srv_O2s.exe        24576 bytes    24.0 KB
```

差 3.4 倍：`-O2 -s` 把符号表剥了、代码也优化了，这就是"真实游戏形态"给逆向用的那对。

### `imports.txt` — 网络能力是借来的

`objdump -p` 的原始输出，服务端 15 个、客户端 13 个函数来自 `WS2_32.dll`：

```
  	DLL Name: WS2_32.dll
  	vma:     Ordinal  Hint  Member-Name  Bound-To
  	0000a5b8  <none>  0031  WSAGetLastError
  	0000a5c0  <none>  005a  WSAStartup
  	0000a5c8  <none>  00a3  accept
  	0000a5d0  <none>  00a4  bind
  	0000a5d8  <none>  00a5  closesocket
  	0000a5e8  <none>  00b7  inet_ntoa
  	0000a5f8  <none>  00bb  listen
  	0000a608  <none>  00be  recv
  	0000a610  <none>  00c1  send
  	0000a620  <none>  00c5  socket
  -> 15 functions imported from WS2_32.dll
```

### `run.txt` — 三方跑通，hp 100 → 75

服务端最后一行 + 客户端总结：

```
[+  1.652s] FINAL hp=75.00 max_hp=100.00 alive=1  frames=7 hello=1 attack=5 reject=0 bye=1 unknown=0
[+  1.473s] SUMMARY attacks=5 hp_sync=5 predicted=75.00 server_hp=75.00 match=YES
```

5 次 `HP_SYNC` 分别是 95/90/85/80/75，客户端每次都打 `HP check ... OK`。
里面还有两段补充场景：

- `A0.`：换成 `-O0 -g` 的那对 exe、客户端直连 47001（不走 tap），帧字节完全一样（2 次攻击，hp 100→90）。
- `REJECT.`：客户端 `--dmg 30` 超出 `0 < dmg <= 25`，服务端丢掉这次攻击：

```
[+  0.616s] REJECT seq=1 dmg=30.00 out of range (0 < dmg <= 25); hp stays 100.00
[+  0.616s] TX  24B 16 00 03 00 01 00 ... 00 00 c8 42 ...  HP_SYNC hp=100.00 max=100.00 tick=1 alive=1
```

### `tap.txt` — 每一块的时间/方向/长度/hex，外加按协议解出来的帧

```
[+  0.071s] C>S    12B  0a 00 01 00 01 00 00 00 01 00 00 00
[+  0.071s] C>S frame #1     12B  HELLO client_id=1 version=1  hex=0a 00 ...
[+  0.476s] S>C    24B  16 00 03 00 01 00 00 00 00 00 be 42 00 00 c8 42 01 00 00 00 01 00 00 00
[+  0.476s] S>C frame #1     24B  HP_SYNC player=1 hp=95.00 max=100.00 tick=1 alive=1  hex=16 00 ...
[+  1.502s] tap summary  C>S chunks=7 bytes=96 frames=7 | S>C chunks=5 bytes=120 frames=5
```

`C>S chunks=7` = HELLO + 5×ATTACK + BYE，`bytes=96` = 12 + 5×16 + 4；
`S>C chunks=5 / bytes=120` = 5×HP_SYNC(24)。**三方账目对得上**：客户端发的、tap 转的、服务端收的帧数一致。

> 给 capture-lab 用：`tap.txt` / `big-frame.txt` 里的 hex 就是真实运行时的 TCP 字节流，可以直接拿去拼
> `text2pcap` 的 demo.pcapng；过滤端口记得是 **47000 / 47001**。

### `netstat.txt` — LISTENING / ESTABLISHED / PID 三件事同时可见

抓的时候三方都在（客户端故意跑 30 次攻击约 6 秒，保证抓的这一刻连接还在）：

```
known PIDs: server=7772  tap=5640  client=38344

===== netstat -ano | findstr 47000 =====
  TCP    127.0.0.1:47000        0.0.0.0:0              LISTENING       5640
  TCP    127.0.0.1:47000        127.0.0.1:49772        ESTABLISHED     5640
  TCP    127.0.0.1:49772        127.0.0.1:47000        ESTABLISHED     38344

===== netstat -ano | findstr 47001 =====
  TCP    127.0.0.1:47001        0.0.0.0:0              LISTENING       7772
  TCP    127.0.0.1:47001        127.0.0.1:49773        ESTABLISHED     7772
```

`Get-NetTCPConnection` 那张表里同样能看到 `Listen/Established` 和 `OwningProcess`。
`Get-Process` 那张表把 PID 翻成进程名（`python` / `zombie_srv_O2s` / `zombie_cli_O2s`）。

### `trust-client.txt` — 谁说了算：客户端还是服务端

三个场景，**同一份改包**（tap 把 ATTACK 里的 `claimed_hp` 改掉，帧长不变还是 16 字节）：

| 场景 | 服务端模式 | tap 改成 | 服务端最终 hp |
|---|---|---|---|
| T1 | `--trust-client` | `claimed_hp=1.00` | `FINAL hp=1.00` |
| T2 | `--trust-client` | `claimed_hp=100.00` | `FINAL hp=100.00`（满血"回血"） |
| T3 | `--authoritative` | `claimed_hp=100.00` | `FINAL hp=75.00`（改包无效） |

T1 的关键三行（客户端明明只该掉到 75，服务端直接信了 1.00）：

```
[+  0.494s] HACK C>S ATTACK seq=1: claimed_hp 95.00 -> 1.00  (frame stays 16B, only payload bytes 12..15 changed)
[+  0.593s] HP   100.00 -> 1.00  (trust-client: took claimed_hp=1.00 as truth, diff=-99.00)
[+  1.622s] FINAL hp=1.00 max_hp=100.00 alive=1 ...
```

T2 是"改包秒回血"：

```
[+  0.691s] HP   100.00 -> 100.00  (trust-client: took claimed_hp=100.00 as truth, diff=0.00)
... 五次全一样，FINAL hp=100.00
```

T3 是同一发改包打在权威模式上，服务端自己算，日志里只剩"客户端说 100，我说 X，差多少"：

```
[+  0.651s] HP   100.00 -> 95.00  (authoritative: claimed says 100.00, diff=5.00)
[+  0.864s] HP   95.00 -> 90.00  (authoritative: claimed says 100.00, diff=10.00)
...
[+  1.701s] FINAL hp=75.00 ...
```

口径：这段只用来演示"为什么血量必须服务端自己算"，协议一个字节都没为外挂改。

### `big-frame.txt` — 一次 16006 字节的帧，recv 被切成几次

这是 TCP 分段/合流的证据，两组对照（同一张 SNAPSHOT，count=1000）：

**D1：服务端用 4 次 `send()` 写（每次 ≤4096），客户端 recv 缓冲 8192**

```
服务端：
[+  0.197s] TX    send() #1/4   4096 bytes  offset=    0
[+  0.197s] TX    send() #2/4   4096 bytes  offset= 4096
[+  0.197s] TX    send() #3/4   4096 bytes  offset= 8192
[+  0.198s] TX    send() #4/4   3718 bytes  offset=12288

客户端：
[+  0.010s] RX recv n= 4096
[+  0.010s] RX recv n= 8192     <- 服务端的 #2、#3 被 TCP 合成了这一次 recv
[+  0.011s] RX recv n= 3718
[+  0.011s] RX  16006B ... SNAPSHOT count=1000 payload=16002 recv_chunks=3

tap：
[+  0.166s] S>C  4096B / S>C  8192B / S>C  3718B
```

**D2：服务端一次 `send()` 写完整帧，客户端 recv 缓冲 65536**

```
[+  0.035s] RX recv n=16006
[+  0.035s] RX  16006B ... SNAPSHOT count=1000 payload=16002 recv_chunks=1
tap: S>C 16006B（一段）
```

同一个发送方式，recv 的切点**不固定**——我这几轮跑下来见过 `4096 / 8192 / 3718` 和
`4096 / 4096 / 7814` 两种切法。这不是 bug，这正是要讲的：**TCP 给你的是字节流，不是消息**。
所以客户端从第一个字节开始就维护一个 receive buffer，凑够 `2 + total_len` 才切一帧出来
（`conn_next_frame`，`zombie_cli.c`）。`recv_chunks=N` 这个数字就是"这一帧用了几次 recv 才凑齐"。

---

## 4. 我踩到的坑（真实的，都在这台机器上复现过）

1. **端口 4001 被 QQ.exe 占着 → bind WSA 10013。**
   排查顺序：`netsh interface ipv4 show excludedportrange protocol=tcp` 排除保留端口 →
   `netstat -ano | findstr 4001` 看 PID → `Get-Process -Id` 看是谁。最后换到 47000/47001。
   教训：10013 不等于"要管理员"，先看是谁在 LISTENING。

2. **执行策略拦截未签名 .ps1**：直接 `& .\run.ps1` 报 `不能加载文件...未进行数字签名`。
   必须 `powershell -NoProfile -ExecutionPolicy Bypass -File .\run.ps1`。

3. **Windows PowerShell 5.1 读无 BOM 的 .ps1 会按 ANSI 解码**。
   本目录 `.ps1` 里**一个中文都没有**（中文说明全在本文件），python 路径也不是硬敲的，
   而是用 `$env:USERPROFILE` 拼出来的（`C:\Users\风林火山\...` 不能出现在脚本里）。

4. **C 程序的 stdout 重定向到文件时是块缓冲**：不 flush 的话，进程被 `Stop-Process` 掐掉，
   最后几行日志就没了。所以日志函数每行都 `fflush(stdout)`（见 `zombie.h` 的 `zb_log`）。

5. **Python 同理**：`tap.py` 每行 `flush=True`，并且 run.ps1 用 `python -u` 启动。

6. **PowerShell 5.1 的坑：`if (Test-Path -LiteralPath $x -and (...))` 会被当成给 Test-Path 传
   一个叫 `-and` 的参数**，报 `NamedParameterNotFound`。必须写成
   `if ((Test-Path -LiteralPath $x) -and (...))`。第一次跑 run.ps1 就死在这。

7. `Start-Process -ArgumentList @()`（空数组）是非法的，会报"参数为 Null、为空"。
   默认端口跑服务端/客户端时干脆不带 `-ArgumentList`。

8. **证据文件全 ASCII**：`Get-Process ... Path` 会带出 `C:\Users\风林火山\...` 这种中文路径，
   任何工具打开都可能乱码。所以 netstat.txt 里的进程表只留 `Id, ProcessName, StartTime`。

9. **netstat 抓取可能比客户端活得久**：Windows 上 `netstat -ano` 有时要几百毫秒到一两秒，
   如果客户端只跑 5 次攻击（1.5 秒），抓完之后进程就退了，`Get-Process -Id` 会报
   "找不到进程"，表格就空了（我一开始就中招了）。现在 netstat 场景让客户端跑 30 次攻击
   （约 6 秒），并且**先抓进程表再抓 netstat**，稳定了。

10. **清理进程要克制**：`run.ps1` 开头只杀 `zombie_srv*` / `zombie_cli*` 和命令行里带 `tap.py`
    的 python，不用 `taskkill /IM python.exe` 那种大扫除（会误杀别人的工具）。

11. 每个场景都是"新起一个服务端"：服务端只 accept 一个连接，收到 BYE 打印 `FINAL` 就退出；
    监听 socket 设了 `SO_REUSEADDR`，所以下一个场景马上能重绑同一个端口，不用等 TIME_WAIT。

---

## 5. 和 BRIEF 的出入 / 额外提供的东西

- 端口按**新 BRIEF**：服务端 47001、tap 47000→47001、客户端默认连 47000，服务端默认
  `--bind 127.0.0.1`。`zombie.h` 里 `ZB_DEF_SRV_PORT/ZB_DEF_TAP_PORT` 就是这两个数。
- 额外的命令行开关（都不改协议，方便上课和取证）：
  - 服务端：`--bind <ip>`、`--snapshot-count N`、`--send-chunk N`、`--max-hp F`
  - 客户端：`--recv-buf N`（一次 recv 最多收多少，big-frame 那段靠它）、`--join-drain-ms N`、
    `--client-id N`、`--hex-bytes N`
  - tap.py：`--hack-claimed X`（改包演示）、`--hex-bytes N`
- `tap.py --hack-claimed` 属于**中间人**，不是客户端功能：客户端源码里没有任何"作弊"字段，
  改的是路上经过的那 4 个 payload 字节（帧长仍 16），符合 BRIEF "只作漏洞演示"的口径。
- `run.ps1` 支持 `-SrvPort / -TapPort` 换端口（机器上 47000/47001 也被占时用）。
- 没碰 `os\`、`capture\`、`docs\`，没执行任何 git 命令。

---

## 6. 验收对照（BRIEF → 我的证据）

| BRIEF 要求 | 证据文件 | 关键行 |
|---|---|---|
| 编译输出 + 两个 exe 体积 | `build.txt` | 4 个 exit code = 0；83.6KB vs 24.0KB |
| `objdump -p` 里 ws2_32 的导入 | `imports.txt` | `-> 15 functions imported from WS2_32.dll` |
| server+client 完整日志、真实 hex 帧 | `run.txt` | `FINAL hp=75.00` / `SUMMARY ... match=YES` |
| tap 每 chunk 时间/方向/长度/hex | `tap.txt` | `C>S 12B 0a 00 ...` / `S>C frame #1 24B HP_SYNC ...` |
| 运行中 netstat + Get-NetTCPConnection，有 PID | `netstat.txt` | `LISTENING 5640` / `ESTABLISHED 7772` |
| trust-client 改包被信 | `trust-client.txt` | `HP 100.00 -> 1.00 (took claimed_hp=1.00 as truth)` |
| snapshot 1000 的 recv 切分 | `big-frame.txt` | `recv_chunks=3`（4096/8192/3718）vs `recv_chunks=1`（16006） |
