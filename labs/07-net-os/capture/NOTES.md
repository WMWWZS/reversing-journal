# capture-lab · NOTES（给 Lead 看的：实际跑了什么、真输出、踩到的坑）

任务板：`task-3`。目录：`labs/07-net-os/capture/`（只写这个目录）。
本机：Windows 11，**非管理员**，Wireshark 4.0.17（`E:\Wireshark`，不在 PATH），PowerShell **5.1**。

---

## 1. 交付物（全部实测过）

| 文件 | 状态 | 验证方式 |
|---|---|---|
| `zombie.lua` | 完成，**实测触发** | `tshark -r demo.pcapng -X lua_script:zombie.lua` 输出 `ZOMBIE` 协议列 + `zombie.*` 字段（见 §3） |
| `capture.ps1` | 完成 | 非管理员→中文提示退出（实测）；`-ExistingPcap` 展开路径实测；**真抓包那段没法测（非管理员）** |
| `expand.ps1` | 完成 | 对 `demo.pcapng` 真跑，生成 801 行 / 15 行 / 10 行三个 txt（实测） |
| `demo.pcapng` | 完成 | 9 包，`capinfos` 认作 `Capture application: Text2pcap (Wireshark) 4.0.17`；全字段解析通过 |
| `README.md` | 完成 | 用户视角中文步骤 |
| `NOTES.md` | 本文件 | — |

额外自测文件（不是 BRIEF 要求，但能证明解析器扛得住 TCP 分段）：

- `mkpcap.py` —— 生成 `demo.pcapng` 的构造脚本（完整 Ethernet/IPv4/TCP 帧 → `text2pcap -l 1 -F pcapng` 写容器）
- `demo-frames.hexdump` —— 上面那一步给 text2pcap 的输入（ASCII hexdump，可以逐字节核对）
- `selftest-snapshot.py` + `selftest-snapshot.pcapng` —— 16006 字节 SNAPSHOT 帧拆成两个 TCP 段，验证 `zombie.lua` 的分段重组
- `expanded/` —— README 里那条不带 `-OutDir` 的命令的默认输出目录（已生成，801+15+10 行）

`evidence/` 下的原始输出：

```
demo-dissect.txt        tshark -V 全解析（带 lua），37515 B / 801 行
demo-summary.txt        tshark 默认输出（一行一个包）
demo-fields.txt         -T fields 的 zombie.* 字段表
demo-capinfos.txt       capinfos 输出（9 包 / 538 B data / Text2pcap 4.0.17）
expand-demo.txt         expand.ps1 的完整控制台输出（exit 0）
capture-nonadmin.txt    capture.ps1 在非管理员窗口的完整输出（exit 1）
capture-existing-demo.txt  capture.ps1 -ExistingPcap 的完整输出（exit 0）
snapshot-reassembly.txt 16006 B 跨段重组自测
text2pcap-T-limitation.txt  text2pcap -T 为什么不能直接做 demo（带真实 -V）
demo-expand/            expand.ps1 -OutDir evidence\demo-expand 的产物
demo-capture-test/      上面 -ExistingPcap 的产物
```

---

## 2. 最要紧的一句：这个 pcap 的哪些字节是真的

- **ZOMBIE payload 字节：逐字节来自真实运行。** 取自 net-lab 真实跑的 `net\evidence\tap.txt`（我动手时它还没发布，先读到的是 `net\_runtmp\d2-tap.txt` 的同一次真实运行日志；后来 `tap.txt` 发布，四条 hex 与我一字不差，已核对）。原文四行：

```text
[+  0.119s] C>S frame #1     12B  HELLO client_id=1 version=1  hex=0a 00 01 00 01 00 00 00 01 00 00 00
[+  0.523s] C>S frame #2     16B  ATTACK dmg=5.00 seq=1 claimed=95.00  hex=0e 00 02 00 00 00 a0 40 01 00 00 00 00 00 be 42
[+  0.525s] S>C frame #1     24B  HP_SYNC player=1 hp=95.00 max=100.00 tick=1 alive=1  hex=16 00 03 00 01 00 00 00 00 00 be 42 00 00 c8 42 01 00 00 00 01 00 00 00
[+  1.561s] C>S frame #7      4B  BYE  hex=02 00 05 00
```

- **Ethernet / IPv4 / TCP 头是合成的**（`mkpcap.py` 造，IP/TCP 校验和算对了）。
- **整个 TCP 会话是合成的**：SYN/SYN-ACK/ACK/FIN 的标志、seq/ack 号、端口 52341→4001、两端都是 127.0.0.1。真实靶场是 client→tap(127.0.0.1:4000)→server(127.0.0.2:4001) 两条连接，demo 只合成一条，目的只是让 lua 有东西可解析。
- `demo.pcapng` 的**容器确实是 text2pcap 写的**（`capinfos`：`Capture application: Text2pcap (Wireshark) 4.0.17`）。

> BRIEF 要求的那句话写在这里：**这个 pcap 的 TCP 字节流（ZOMBIE 帧）来自真实运行；IP/TCP 头是 text2pcap/构造脚本合成的，只用来验证 lua 解析器。**

---

## 3. 真实跑过的命令 + 原样输出

### 3.1 构造 demo.pcapng

```powershell
$py = 'C:\Users\风林火山\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe'
cd E:\harness\reversing-journal\labs\07-net-os\capture
& $py mkpcap.py --keep-hexdump
```

`mkpcap.py` 内部最后一步是：

```text
E:\Wireshark\text2pcap.exe -l 1 -F pcapng -N synth-loopback demo-frames.hexdump demo.pcapng
Read 9 potential packets, wrote 9 packets (1192 bytes including overhead).
```

脚本打印的 9 帧（前 3 帧握手 + 3 个 ZOMBIE 帧 + 3 帧拆连接）：

```text
 1  SYN      client -> server   54 B   ...                       （54 = 14+20+20，无 payload）
 2  SYN-ACK  server -> client   54 B
 3  ACK      client -> server   54 B
 4  HELLO    client -> server   66 B   ... 0a 00 01 00 01 00 00 00 01 00 00 00
 5  ATTACK   client -> server   70 B   ... 0e 00 02 00 00 00 a0 40 01 00 00 00 00 00 be 42
 6  HP_SYNC  server -> client   78 B   ... 16 00 03 00 ...（整帧 24 B）
 7  FIN      client -> server   54 B
 8  FIN-ACK  server -> client   54 B
 9  ACK      client -> server   54 B
```

### 3.2 lua 解析器被真的触发了

```powershell
& 'E:\Wireshark\tshark.exe' -r .\demo.pcapng -X lua_script:.\zombie.lua
```

原样输出（`evidence\demo-summary.txt`）：

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

`-V` 里 HP_SYNC 的字段（`evidence\demo-dissect.txt` 原样摘录）：

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

注意两个数字：**`24 bytes on the wire` 是整帧；`Total Length: 22` 是帧里写的 `total_len`**。差 2 就是总长不含自身。这个解析器就是按这个定义实现的，demo 也卡着 `22+2=24`、`14+2=16`、`10+2=12` 校验过。

字段提取（`-T fields`，原样，`evidence\demo-fields.txt`）：

```text
frame.number,tcp.len,zombie.opcode,zombie.total_len,zombie.client_id,zombie.damage,zombie.seq,zombie.claimed_hp,zombie.hp,zombie.max_hp,zombie.tick,zombie.alive
1,0,,,,,,,,,,
2,0,,,,,,,,,,
3,0,,,,,,,,,,
4,12,0x01,10,1,,,,,,,
5,16,0x02,14,,5,1,95,,,,
6,24,0x03,22,,,,,95,100,1,1
7,0,,,,,,,,,,
8,0,,,,,,,,,,
9,0,,,,,,,,,,
```

### 3.3 校验和 / 容器

```powershell
& 'E:\Wireshark\tshark.exe' -r .\demo.pcapng -o tcp.check_checksum:TRUE -o ip.check_checksum:TRUE -V
```

9 个包的 IP/TCP 校验和全是 `[correct]`（18 处，全部 Good），所以不是"乱造的包"被 tshark 糊弄过去。

```text
File type:           Wireshark/... - pcapng
File encapsulation:  Ethernet
Number of packets:   9
Data size:           538 bytes
Capture application: Text2pcap (Wireshark) 4.0.17 (v4.0.17-0-g882448bcf104)
Number of interfaces in file: 1
```

### 3.4 expand.ps1（三个 txt）

```powershell
powershell -ExecutionPolicy Bypass -File .\expand.ps1 -Pcap .\demo.pcapng
```

原样输出（`evidence\expand-demo.txt`，exit 0）：

```text
ZOMBIE 解析器：...\capture\zombie.lua
输入 pcap ：...\capture\demo.pcapng
输出目录 ：...\capture\expanded

[1/3] tshark -V -> dissect.txt （会比较慢、文件比较大，正常）
[2/3] tshark -x -> hex.txt （前 3 帧）
[3/3] tshark -T fields -> streams.txt （TCP seq/len/flags）

完成。
  ...\expanded\dissect.txt  (801 行)
  ...\expanded\hex.txt  (15 行)
  ...\expanded\streams.txt  (10 行)
```

`streams.txt` 实际内容（节选）：

```text
frame.number,ip.src,tcp.srcport,tcp.dstport,tcp.seq,tcp.len,tcp.flags,tcp.time_delta,zombie.opcode
"1","127.0.0.1","52341","4001","0","0","0x0002","0.000000000",
"4","127.0.0.1","52341","4001","1","12","0x0018","0.000001000","0x01"
"5","127.0.0.1","52341","4001","13","16","0x0018","0.000001000","0x02"
"6","127.0.0.1","4001","52341","1","24","0x0018","0.000001000","0x03"
```

### 3.5 分段重组自测（16006 字节 SNAPSHOT 拆两段）

```powershell
& $py selftest-snapshot.py
& 'E:\Wireshark\tshark.exe' -r .\selftest-snapshot.pcapng -X lua_script:.\zombie.lua
```

原样输出（`evidence\snapshot-reassembly.txt`）：

```text
    5 0.000004000    127.0.0.1 → 127.0.0.1    TCP 8054 4001 → 52341 [PSH, ACK] Seq=1 Ack=13 Win=65535 Len=8000 [TCP segment of a reassembled PDU]
    6 0.000005000    127.0.0.1 → 127.0.0.1    ZOMBIE 8060 SNAPSHOT count=1000
...
字段个数 = 1000        （-Y zombie.snapshot.id -T fields -e zombie.snapshot.id 数出来的）
```

`zombie.lua` 里用 `pinfo.desegment_len / desegment_offset` 做重组；真实靶场 `--snapshot-count 1000` 那次（`tcp.len` 合计 16006）会走同一条代码路径。

### 3.6 capture.ps1（非管理员 + 跳过抓包两条路径）

```powershell
# 非管理员：应打印中文提示后退，exit 1（evidence\capture-nonadmin.txt）
powershell -ExecutionPolicy Bypass -File .\capture.ps1

# 跳过抓包、只展开已有 pcapng，exit 0（evidence\capture-existing-demo.txt）
powershell -ExecutionPolicy Bypass -File .\capture.ps1 -ExistingPcap .\demo.pcapng
```

非管理员时真实输出（原文）：

```text
== 检查管理员权限 ==

这个脚本要在【管理员】PowerShell 里跑，因为 dumpcap 抓网卡需要管理员权限。
当前不是管理员。请这样做：
  1) 右键“开始”按钮 -> 终端(管理员)；或者 Win+X -> Windows PowerShell (管理员)
  ...
exit code = 1
```

**真抓包（dumpcap）那段本机没法测**：当前非管理员，`dumpcap` 会 `You do not have permission to capture`，按 BRIEF 要求没有反复撞权限、也没有尝试提权。脚本里这段（Start-Process dumpcap → 提示 → Read-Host → Stop-Process → capinfos → 调 expand.ps1）是照 README 的流程写的，但**没有真实执行过**；`expand.ps1`/`capinfos`/tshark 那半段都是实测过的。

---

## 4. 踩到的坑（这几条是本文最有用的部分）

### 坑 1：`text2pcap -T` 的哑 TCP 头不能表达 SYN / seq

BRIEF 说"用 `text2pcap` 包成真 pcap"，第一反应是 `text2pcap -T 52341,4001`。实测发现它生成的 TCP 头是写死的：

```text
Sequence Number (raw): 0
Acknowledgment Number: 0
Flags: 0x000 (<None>)      <- Syn: Not set, Fin: Not set, Push: Not set
```

（完整证据：`evidence\text2pcap-T-limitation.txt`，输入在 `evidence\text2pcap-T-input.txt`。）

所以 `-T` 根本做不出 SYN/SYN-ACK/FIN 会话。**最终做法**：`mkpcap.py` 自己造完整 Ethernet/IP/TCP 帧（端口、seq/ack、flags、校验和都自己算），写成 text2pcap 的 hexdump，再让 **text2pcap** 写 pcapng 容器（`-l 1 -F pcapng`）。BRIEF 的"text2pcap 生成"没有丢：`capinfos` 里 `Capture application: Text2pcap (Wireshark) 4.0.17`。

### 坑 2：text2pcap 4.0.17 的 hexdump 注释行 bug

一开始给每个包写了一条 `#TEXT` 注释，标签形如 `packet 1`。结果 text2pcap 只写出 152 / 16562 字节（`frame.len` 变成 2,3,4,...,9 这种），而且不报错。
逐字节二分后定位：**注释行以"空格+单个数字"结尾时**（`packet 1`、`hello 1`、`x 1` 都触发；`packet`、`packet 54`、`pN` 不触发）解析被带偏。
解决：hexdump 里只保留文件头 3 行注释，**每包不再写文本标签**；包的描述改为脚本打印到 stdout。这个坑写进了 `mkpcap.py` 的注释里。

### 坑 3：PowerShell 5.1 的 `Split-Path -LiteralPath ... -Parent` 是非法组合

`capture.ps1`/`expand.ps1` 第一版里用 `Split-Path -LiteralPath $pcapPath -Parent`。在 PS 5.1 下（6 个参数集，`-LiteralPath` 只在 `LiteralPathSet` 里，和 `-Parent` 不同集）直接抛：

```text
Parameter set cannot be resolved using the specified named parameters.
```

而且**只在没传 `-OutDir` 时**才走到这行——所以"`expand.ps1 -Pcap demo.pcapng` 挂了、加个 `-OutDir` 又好了"，害我查了很久。
修法：改成 `[System.IO.Path]::GetDirectoryName($pcapPath)`。修完以后 README 里那条裸命令实测 exit 0。

### 坑 4：执行策略不允许跑未签名 .ps1

本机（默认策略）直接 `.\expand.ps1` 会报"未对文件进行数字签名"。所有实测都用：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\xxx.ps1 ...
```

`-ExecutionPolicy Bypass` 只影响这一次进程，不改系统设置。README 里统一这么写。

### 坑 5：中文 .ps1 + BOM + PS 5.1

按 BRIEF：脚本里的中文保存成 **UTF-8 with BOM + CRLF**（已核对前 3 字节 `ef bb bf`），PS 5.1 下 `Write-Host` 中文显示正常（`evidence\expand-demo.txt` 里就是原样中文输出）。`.lua` / `.py` 保持纯 ASCII（非 ASCII 字节数实测都是 0），中文只在 `.md` 和 `.ps1` 里。

### 坑 6：lua 的 TCP 分段

`SNAPSHOT count=1000` 的整帧 16006 字节会跨段。解析器里不能用 `buffer` 长度当帧长，必须：

```lua
if frame_len > rem then
    pinfo.desegment_offset = offset
    pinfo.desegment_len = frame_len - rem
    return
end
```

否则大帧会被截断成 "SNAPSHOT (truncated payload)"。用 `selftest-snapshot.pcapng` 验证通过（1000 个实体字段都解出来了）。

---

## 5. lua 字段清单（给文档/过滤器用）

注册在 `tcp.port` 的 **4000 和 4001**（`DissectorTable.get("tcp.port"):add(...)`），所以只解析这两个端口；SYN/FIN 这类无 payload 的包不会进 dissector。

| 字段 | 类型 | 出现帧 |
|---|---|---|
| `zombie.total_len` | u16 DEC | 全部 |
| `zombie.opcode` | u8 HEX（带名字表） | 全部 |
| `zombie.flags` | u8 HEX | 全部 |
| `zombie.client_id` / `zombie.version` | u32 DEC | HELLO |
| `zombie.damage` / `zombie.seq` / `zombie.claimed_hp` | f32 / u32 / f32 | ATTACK |
| `zombie.player_id` / `zombie.hp` / `zombie.max_hp` / `zombie.tick` / `zombie.alive` / `zombie.pad` | u32/f32/f32/u32/u8/bytes | HP_SYNC |
| `zombie.count` / `zombie.snapshot.id` / `.x` / `.y` / `.hp` | u16 / u32,f32,f32,f32 | SNAPSHOT |
| `zombie.payload` | bytes | 未知/损坏 opcode 兜底 |

`-Y zombie` 可以过滤出全部 ZOMBIE 帧；`-T fields -e zombie.opcode` 这种可以直接喂脚本。

小差异：`tshark -V` 里 `ProtoField.float` 显示是 `Damage: 5`（Wireshark 省略 `.00`），Info 列是脚本 format 出来的 `damage=5.00`；`-T fields` 输出 `5`。同一个浮点，三种显示，不是数据不一致。

---

## 6. 复现（一键）

```powershell
$py = 'C:\Users\风林火山\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe'
cd E:\harness\reversing-journal\labs\07-net-os\capture

# 1) 重新生成 demo.pcapng（可选，仓库里已有）
& $py mkpcap.py --keep-hexdump

# 2) 解析器验收
& 'E:\Wireshark\tshark.exe' -r .\demo.pcapng -X lua_script:.\zombie.lua

# 3) 三个 txt
powershell -ExecutionPolicy Bypass -File .\expand.ps1 -Pcap .\demo.pcapng

# 4) 分段重组自测
& $py selftest-snapshot.py
& 'E:\Wireshark\tshark.exe' -r .\selftest-snapshot.pcapng -X lua_script:.\zombie.lua -Y zombie

# 5) 真抓包（要管理员窗口）
powershell -ExecutionPolicy Bypass -File .\capture.ps1
```

---

## 7. 局限 / 需要 Lead 注意的

1. **管理员抓包流程没实测**（本机非管理员，也没有尝试提权）。脚本的检查/展开/落盘逻辑都测了；`dumpcap` 那一段只能靠用户实跑。README 里把"你会看到什么"写成了分步说明，没有编造 dumpcap 的逐行输出。
2. `demo.pcapng` 是**合成会话**（见 §2），不能拿它当"真实抓包证据"；真实抓包请用 `capture.ps1` 或 `expand.ps1` 处理用户的 `zombie.pcapng`。
3. `selftest-snapshot.pcapng` 的 SNAPSHOT 内容是**造的**（x/y/hp 按公式生成），只验证"16006 字节跨段能重组 + count=1000 能解出 1000 个实体"，不代表服务端真实快照内容。
4. 真实靶场是 client→tap(4000)→server(127.0.0.2:4001) 两条连接；demo 只合成了一条 127.0.0.1:4001 的连接。解析器按端口注册、对两条连接都适用，真实抓包里会看到两个 TCP 流（tap 是两个 connect 的桥）。
5. 本目录没有改 `net/`、`os/`、`docs/`，也没碰 git。
6. `capture.ps1` 把 `-w` 指向 `evidence\zombie.pcapng`（BRIEF 原文写的是 `-w zombie.pcapng`）：这样抓包产物直接落在 `evidence\` 下，后续三个 txt 也同在那一层，README 里说明了这个路径。
7. `demo.pcapng` 里 `zombie.total_len` 的三个校验值：HELLO `10+2=12`、ATTACK `14+2=16`、HP_SYNC `22+2=24`，`-T fields` 实测全对（§3.2）。
