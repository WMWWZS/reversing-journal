# capture · 抓包：亲眼看 ZOMBIE 协议在 TCP 上长什么样

这一节只做两件事：

1. 用 Wireshark 的工具把 `127.0.0.1` 上的 ZOMBIE 流量抓下来；
2. 用一段 Lua 插件（`zombie.lua`）让 Wireshark/TShark 直接显示 `HELLO client_id=1 version=1` 这种字段，而不是一坨十六进制。

先记住一个数：**HP_SYNC 一帧 = 24 字节** = 2 字节 `total_len` + 1 字节 opcode + 1 字节 flags + 20 字节 payload。
攻击帧 ATTACK 是 16 字节，HELLO 是 12 字节，BYE 是 4 字节。

---

## 一、先看一个已经抓好的包（不需要管理员，现在就能做）

`demo.pcapng` 是仓库里自带的演示包（一条完整的 TCP 会话：SYN → SYN-ACK → ACK → HELLO → ATTACK → HP_SYNC → FIN）。
它不需要权限，先拿它把流程跑一遍，确认你的工具是通的。

打开 PowerShell，敲：

```powershell
cd E:\harness\reversing-journal\labs\07-net-os\capture
powershell -ExecutionPolicy Bypass -File .\expand.ps1 -Pcap .\demo.pcapng
```

> `-ExecutionPolicy Bypass` 只对这一次运行生效，不改你系统的设置。本机默认执行策略不允许跑未签名的 `.ps1`，不加这一段会报“未对文件进行数字签名”。

你会看到（这是本机真实输出，存在 `evidence\expand-demo.txt`）：

```text
ZOMBIE 解析器：E:\harness\reversing-journal\labs\07-net-os\capture\zombie.lua
输入 pcap ：E:\harness\reversing-journal\labs\07-net-os\capture\demo.pcapng
输出目录 ：E:\harness\reversing-journal\labs\07-net-os\capture\expanded

[1/3] tshark -V -> dissect.txt （会比较慢、文件比较大，正常）
[2/3] tshark -x -> hex.txt （前 3 帧）
[3/3] tshark -T fields -> streams.txt （TCP seq/len/flags）

完成。
  ...\expanded\dissect.txt  (801 行)
  ...\expanded\hex.txt  (15 行)
  ...\expanded\streams.txt  (10 行)
```

然后直接看 Wireshark 解析器给出的结论，敲：

```powershell
& 'E:\Wireshark\tshark.exe' -r .\demo.pcapng -X lua_script:.\zombie.lua
```

你会看到（本机真实输出）：

```text
    1 0.000000000    127.0.0.1 → 127.0.0.1    TCP 54 52341 → 4001 [SYN] Seq=0 Win=65535 Len=0
    2 0.000001000    127.0.0.1 → 127.0.0.1    TCP 54 4001 → 52341 [SYN, ACK] Seq=0 Ack=1 Win=65535 Len=0
    3 0.000002000    127.0.0.1 → 127.0.0.1    TCP 54 52341 → 4001 [ACK] Seq=1 Ack=1 Win=65535 Len=0
    4 0.000003000    127.0.0.1 → 127.0.0.1    ZOMBIE 66 HELLO client_id=1 version=1
    5 0.000004000    127.0.0.1 → 127.0.0.1    ZOMBIE 70 ATTACK damage=5.00 seq=1 claimed_hp=95.00
    6 0.000005000    127.0.0.1 → 127.0.0.1    ZOMBIE 78 HP_SYNC player_id=1 hp=95.00/100.00 tick=1 alive=1
    7 0.000006000    127.0.0.1 → 127.0.0.1    TCP 54 52341 → 4001 [FIN, ACK] Seq=29 Ack=25 Win=65535 Len=0
    8 0.000007000    127.0.0.1 → 127.0.0.1    TCP 54 4001 → 52341 [FIN, ACK] Seq=25 Ack=30 Win=65535 Len=0
    9 0.000008000    127.0.0.1 → 127.0.0.1    TCP 54 52341 → 4001 [ACK] Seq=30 Ack=26 Win=65535 Len=0
```

盯住这 4 个数字对：

| 行 | 你看到的 | 66 / 70 / 78 是怎么来的 |
|---|---|---|
| 4 | `ZOMBIE 66 HELLO client_id=1 version=1` | 14（以太网头）+ 20（IP 头）+ 20（TCP 头）+ **12（ZOMBIE 帧）** = 66 |
| 5 | `ZOMBIE 70 ATTACK damage=5.00 seq=1 claimed_hp=95.00` | 54 + **16** = 70 |
| 6 | `ZOMBIE 78 HP_SYNC player_id=1 hp=95.00/100.00 tick=1 alive=1` | 54 + **24** = 78 |

`54` 是 TCP 层往上的固定开销（14 + 20 + 20）。所以：

- HELLO 帧 12 字节 → `total_len = 10`（因为 `total_len` 不含它自己那 2 字节：`10 = 1 opcode + 1 flags + 8 payload`）；
- ATTACK 帧 16 字节 → `total_len = 14`；
- HP_SYNC 帧 24 字节 → `total_len = 22`。

想看每个字段的原始数值，敲这条（一条命令只拿字段，方便喂脚本）：

```powershell
& 'E:\Wireshark\tshark.exe' -r .\demo.pcapng -X lua_script:.\zombie.lua `
  -T fields -e frame.number -e tcp.len -e zombie.opcode -e zombie.total_len `
  -e zombie.client_id -e zombie.damage -e zombie.seq -e zombie.claimed_hp `
  -e zombie.hp -e zombie.max_hp -e zombie.tick -e zombie.alive `
  -E header=y -E separator=','
```

本机真实输出（`evidence\demo-fields.txt`，原样表头 + 第 1~6 帧；第 7~9 帧是拆连接的 FIN/ACK，`tcp.len` 和 zombie 字段全是空）：

```text
frame.number,tcp.len,zombie.opcode,zombie.total_len,zombie.client_id,zombie.damage,zombie.seq,zombie.claimed_hp,zombie.hp,zombie.max_hp,zombie.tick,zombie.alive
1,0,,,,,,,,,,
2,0,,,,,,,,,,
3,0,,,,,,,,,,
4,12,0x01,10,1,,,,,,,
5,16,0x02,14,,5,1,95,,,,
6,24,0x03,22,,,,,95,100,1,1
```

看懂这三行，协议就通了：

- `tcp.len=12` 表示这段 TCP 带了 12 字节，正好一个 HELLO 帧；
- `zombie.total_len=10` 是帧内写的长度，`10 + 2 = 12` 才是帧的真实字节数；
- ATTACK 的 3 个值 `5 / 1 / 95` 就是 `damage=5.0 / seq=1 / claimed_hp=95.0`（`f32` 浮点，小端）。

---

## 二、在 Wireshark 图形界面里看同一个包

1. 双击 `E:\harness\reversing-journal\labs\07-net-os\capture\demo.pcapng`（或者 Wireshark → 文件 → 打开）。
2. 过滤栏输入 `zombie`，回车 → 只剩 3 个包，`Protocol` 列写着 **ZOMBIE**。
3. 点第 4 个包（HELLO），按顺序展开：
   - `Transmission Control Protocol` → 看 `Src Port: 52341`、`Dst Port: 4001`、`Sequence Number`；
   - `ZOMBIE Protocol` → 看到 `Total Length: 10`、`Opcode: HELLO (0x01)`、`Flags: 0x00`、`Client ID: 1`、`Version: 1`。
4. 点第 6 个包（HP_SYNC）：`Total Length: 22`、`Player ID: 1`、`HP: 95`、`Max HP: 100`、`Server Tick: 1`、`Alive: 1`、`Padding: 000000`。

如果 GUI 里没有 `ZOMBIE Protocol` 这一层，说明插件没装。装上它（一次就够）：

1. Wireshark 菜单 → 帮助 → 关于 Wireshark → 文件夹 → 双击 `Personal Lua Plugins` 那一行，会打开
   `C:\Users\<你的用户名>\AppData\Roaming\Wireshark\plugins`；
2. 把本目录的 `zombie.lua` 复制进去；
3. 重启 Wireshark，再打开 `demo.pcapng`，过滤 `zombie`，就能看到字段了。

> 说明：`zombie.lua` 把自己挂在 TCP 端口 47000/47001（另外为了打开 `demo.pcapng`，也保留了 4000/4001），所以只有这些端口的流量会被它解析，别的流量不受影响。

---

## 三、真抓包：抓你自己跑出来的那一次

### 3.1 为什么要管理员

抓包要经过 Npcap 驱动打开网卡设备 `\Device\NPF_Loopback`。不是管理员时，dumpcap 会直接拒绝：
`You do not have permission to capture`。所以这一步必须用**管理员** PowerShell。
（本机当前是非管理员，所以下面这段抓包流程没法替你实测——脚本本身和抓完之后的展开都实测过，见 `NOTES.md`。）

### 3.2 开一个管理员 PowerShell

任选一种：

- 右键“开始”按钮 → 选 **终端(管理员)**；
- 或者 `Win + X` → **Windows PowerShell (管理员)**；
- 或者任务栏搜索 `PowerShell` → 右键 → **以管理员身份运行**。

窗口标题里要有“管理员”三个字。

### 3.3 跑抓包脚本

```powershell
cd E:\harness\reversing-journal\labs\07-net-os\capture
powershell -ExecutionPolicy Bypass -File .\capture.ps1
```

脚本会按顺序做这些事，你对照着看屏幕：

1. `== 检查管理员权限 ==`
   - 不是管理员 → 打印中文提示并退出（本机实测输出在 `evidence\capture-nonadmin.txt`）；
   - 是管理员 → `   是管理员，继续。`
2. `== 启动 dumpcap ==`：打印网卡 `\Device\NPF_Loopback`、过滤器 `tcp port 47000 or tcp port 47001`、写入文件、最长 300 秒，还会打印一条**等价的手工命令**，脚本出问题时你可以照着敲。
3. `   抓包已经在跑了。` 然后 `== 现在去另一个窗口，把靶场跑起来 ==`。
4. 这时**不要关这个窗口**，去开第二个 PowerShell（普通权限就行）：

   ```powershell
   cd E:\harness\reversing-journal\labs\07-net-os\net
   powershell -ExecutionPolicy Bypass -File .\run.ps1
   ```

   它会拉起 server(47001) + tap(47000) + client，客户端打 5 次。等它跑完、回到提示符。
5. 回到管理员窗口，按 **回车**。脚本会：
   - `== 停止 dumpcap ==`，打印抓包文件大小（0 字节会直接报错）；
   - `== 用 capinfos 看一眼抓到了什么 ==`，打印包数量；
   - `== 展开成 dissect.txt / hex.txt / streams.txt ==`（内部就是调 `expand.ps1`）；
   - 最后打印三个文件的路径和行数。

> 为什么最长 300 秒：防止你忘了按回车，dumpcap 挂一整天。跑完就按回车，不用等它。

### 3.4 抓完在哪

默认都写在 `E:\harness\reversing-journal\labs\07-net-os\capture\evidence\`：

| 文件 | 里面是什么 |
|---|---|
| `zombie.pcapng` | 原始抓包，可以用 Wireshark 打开 |
| `dissect.txt` | `tshark -V` 的完整解析（加载了 `zombie.lua`），搜 `ZOMBIE` 就能看到每一帧 |
| `hex.txt` | 前 3 帧的十六进制 + ASCII（就是 TCP 三次握手那几个包） |
| `streams.txt` | 每个包一行：`tcp.seq` / `tcp.len` / `tcp.flags` / `tcp.time_delta` / `zombie.opcode` |

用命令行快速过滤 ZOMBIE 帧：

```powershell
& 'E:\Wireshark\tshark.exe' -r .\evidence\zombie.pcapng -X lua_script:.\zombie.lua -Y zombie
```

在 Wireshark GUI 里打开 `evidence\zombie.pcapng`，过滤栏输入 `zombie`。

---

## 四、抓完了怎么读（对着真实数字）

### dissect.txt

搜 `ZOMBIE HELLO` / `ZOMBIE ATTACK` / `ZOMBIE HP_SYNC`，每一帧下面就是字段：

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

`24 bytes on the wire` 是这一帧自己（不含 IP/TCP 头）的字节数；`Total Length: 22` 是帧里写的 `total_len`。**两者差 2，就是 `total_len` 不含自身。** 这个设计坑过很多自己写协议解析的人。

### hex.txt

前 3 帧是 TCP 握手。第 1 帧的字节长这样：

```text
0000  02 00 00 00 00 02 02 00 00 00 00 01 08 00 45 00   ..............E.
0010  00 28 10 00 40 00 80 06 ec cd 7f 00 00 01 7f 00   .(..@...........
0020  00 01 cc 75 0f a1 00 00 03 e8 00 00 00 00 50 02   ...u..........P.
0030  ff ff d1 e1 00 00                                 .......
```

按段切开：

| 偏移 | 字节 | 是什么 |
|---|---|---|
| 0x00–0x0D (14 字节) | `02 00 00 00 00 02 02 00 00 00 00 01 08 00` | 以太网头：目的 MAC、源 MAC、类型 `0x0800`=IPv4 |
| 0x0E–0x21 (20 字节) | `45 00 00 28 ... 7f 00 00 01 7f 00 00 01` | IPv4 头：总长 0x28=40、协议 6=TCP、源/目的都是 127.0.0.1 |
| 0x22–0x35 (20 字节) | `cc 75 0f a1 ... 50 02` | TCP 头：源端口 0xcc75=52341、目的端口 0x0fa1=4001、`50`=头长 20 字节、`02`=SYN 标志 |
| 0x36 起 | （没有） | SYN 不带数据，`tcp.len=0` |

> 注意：`demo.pcapng` 里 Ethernet/IP/TCP 头是我们合成的，见 `NOTES.md`；**你自己抓的 `zombie.pcapng` 里这些头是 Windows 真实协议栈发的**。

### streams.txt

每行一个包，重点看两列：

- `tcp.len`：这一段 TCP 带了多少字节。`0` 表示纯 ACK/握手/FIN；
- `tcp.seq`：下一段的 `seq` = 上一段 `seq + tcp.len`。

把 `tcp.len` 加起来，就是一次 `send()` 的总字节数。比如大 SNAPSHOT 那次：`16006` 字节（4 字节帧头 + 16002 字节 payload，`count=1000`），如果被拆成几段，你会看到几行非零 `tcp.len`，加起来正好 16006。

---

## 五、常见问题

| 现象 | 原因 / 怎么办 |
|---|---|
| 脚本说“这个脚本要在管理员 PowerShell 里跑” | 你开的是普通窗口。按第三节重新开管理员窗口。 |
| `未对文件 ... 进行数字签名` | 少了 `-ExecutionPolicy Bypass`。用 `powershell -ExecutionPolicy Bypass -File .\capture.ps1`。 |
| `dumpcap 启动后马上退出了` | 大概率还是权限问题；先确认窗口标题有“管理员”。也可以手工跑脚本打印的那条等价命令看报错。 |
| 抓到的文件 0 字节 | dumpcap 没抓到东西：靶场没跑、或端口不是 47000/47001。看第二个窗口里 server 是否打印了监听。 |
| 没有 `zombie.*` 字段，也没有 ZOMBIE Protocol | `-X lua_script:` 路径不对；或者 GUI 里没把 `zombie.lua` 放进插件目录。用 `expand.ps1` 会自动找同目录的 `zombie.lua`。 |
| 抓包文件里没有 47000 端口的流量 | 客户端可能用了 `--port 47001` 直连。过滤器是 `tcp port 47000 or tcp port 47001`，两个端口都抓。 |
| 47000/47001 被占用 | `netstat -ano` 找到占着 47001 的 PID，`taskkill /PID <pid> /F`，或用 `-SrvPort/-TapPort` 换端口。 |

---

## 六、这个目录里有什么

| 文件 | 用途 |
|---|---|
| `zombie.lua` | Wireshark/TShark 的 ZOMBIE 协议解析器；字段名都是 `zombie.*` |
| `capture.ps1` | 管理员抓包脚本（检查权限 → dumpcap → 提示跑靶场 → 回车停 → 自动展开） |
| `expand.ps1` | 不需要权限，把任意 pcapng 展开成 `dissect.txt` / `hex.txt` / `streams.txt` |
| `mkpcap.py` | 生成 `demo.pcapng`（完整 Ethernet/IP/TCP 帧 + 真实 ZOMBIE 帧字节 → text2pcap 写容器） |
| `selftest-snapshot.py` | 自测：16006 字节 SNAPSHOT 跨两个 TCP 段，验证解析器的分段重组 |
| `demo.pcapng` | 演示包（9 个包：握手 + HELLO + ATTACK + HP_SYNC + 拆连接） |
| `demo-frames.hexdump` | `demo.pcapng` 的 text2pcap 输入（想看每帧字节可以翻这个） |
| `selftest-snapshot.pcapng` | 上面那个自测生成的包 |
| `evidence\` | 本机真实跑出来的输出（见 `NOTES.md`） |
