# BRIEF · labs/07-net-os（联机僵尸靶场）

> 这份文件是本次实验的**唯一契约**。三个人（net / os / capture）都按这里写的协议、路径、风格干活。
> 有任何和这份文件冲突的想法，以这份文件为准；确实需要改，先告诉 Lead。

## 背景（很重要，别写跑偏）

用户是一个**零基础**的逆向学习者（完整体验见 `E:\harness\re_session\transcript.txt`）。
他已经学过：C 基础、x86-64 寄存器（rcx/rdx/r8/r9）、标志位、栈帧 `[rbp+0x10]`、PE 格式、Ghidra 反编译、x64dbg 下断点。

他最大的抱怨是：**「一直在模拟，没有真实单机靶场」**。
所以这次的铁律：

1. **必须是真东西**：真进程、真 socket、真 TCP、真内核结构、真的掉血。
2. **每条结论都要能被他自己的机器复现**，而且要给出**原样的命令和原样的输出**。
3. **不要写概念科普**。概念只在"解释这条真实输出"的时候出现，且必须落到一个具体的数字/偏移/字节上。
4. 语言：中文，口气像坐在旁边教，短句。术语第一次出现要拆开解释。
5. 不要出现"你可以试试"这种空话；要写"敲这个命令 → 你会看到这行 → 这行里这个数字是 X"。

## 环境（已经核实过，直接用）

| 东西 | 路径 / 值 | 备注 |
|---|---|---|
| 工作区 | `E:\harness` | danger-full-access，随便写 |
| 仓库 | `E:\harness\reversing-journal` | **只有 Lead 能 git commit/push，你别碰 git** |
| gcc | `C:\mingw64\bin\gcc.exe`（14.2.0，x86_64-win32-seh） | 直接用 `gcc`，在 PATH 里 |
| objdump | `C:\mingw64\bin\objdump.exe` | 在 PATH 里 |
| Python | `C:\Users\风林火山\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe` | 只用标准库 |
| Wireshark | `E:\Wireshark\tshark.exe`、`dumpcap.exe`、`capinfos.exe`、`text2pcap.exe` | 不在 PATH，写全路径 |
| Ghidra | `D:\tools\ghidra\ghidra_12.2_PUBLIC\support\analyzeHeadless.bat` | Lead 用 |
| x64dbg | `D:\tools\x64dbg\release\x64\x64dbg.exe` | 用户自己开 |
| 当前权限 | **非管理员**，且审批策略是 never（不许申请提权） | 抓网卡包需要管理员 → 那部分交给用户自己跑，你只准备脚本 |

**坑（一定要避开）**：

- `dumpcap` / `pktmon` 非管理员一定失败（已实测：`You do not have permission to capture`）。**不要反复尝试提权**。
- `tshark -r xxx.pcapng`、`text2pcap` 都**不需要**权限，正常用。
- `.ps1` 里如果有中文，**必须存成 UTF-8 with BOM**，否则 Windows PowerShell 5.1 打开乱码。
  建议：脚本本身输出用中文，但保存时 `pwsh` 用 `Set-Content -Encoding utf8BOM`。
  （最稳的办法：脚本内文字全用 ASCII，中文说明放 README.md。）
- 路径含中文用户名 `风林火山`：PowerShell 里用单引号字符串最安全。
- Windows 下 `gcc` 编译时 `-lpthread` 不需要；用 Win32 `CreateThread` 而不是 pthread（MinGW 的 pthread 也能用，但 Win32 更贴近要讲的内容）。

## 目录与写入范围（严格按这个来，别越界）

```
E:\harness\reversing-journal\labs\07-net-os\
├── BRIEF.md        ← Lead 写（你只读）
├── README.md       ← Lead 写（你只读）
├── net\            ← net-lab 独占
├── os\             ← os-lab 独占
└── capture\        ← capture-lab 独占
```

每个 lab 目录里必须包含：

- 源码（`.c` / `.py` / `.lua`）
- `build.ps1`（一键编译，输出到同目录或 `bin\`）
- `run.ps1`（一键跑，并把原始证据写进 `evidence\`）
- `evidence\*.txt`（**原样粘贴的终端输出**，不要手改、不要总结）
- `NOTES.md`（给 Lead 看的：你实际跑了什么命令、看到什么、哪里跟预期不一样）

## 协议（已冻结，三个 lab 都按这个实现/解析）

名字：**ZOMBIE 协议**。客户端和服务端跑在 127.0.0.1 上，真 TCP。

帧结构（**全部小端 little-endian**）：

```
偏移  大小  字段        说明
0     2    total_len   后面所有字节的个数 = 1(opcode) + 1(flags) + payload_len
2     1    opcode      见下表
3     1    flags       保留，填 0
4     N    payload     见下表
```

最小帧 4 字节（`total_len = 2`，payload 空）。`total_len` 最大 65535。

| opcode | 名字 | payload（小端） | 说明 |
|---|---|---|---|
| `0x01` | `HELLO` | `u32 client_id`, `u32 version` | 客户端上线，version 固定 1 |
| `0x02` | `ATTACK` | `f32 damage`, `u32 seq`, `f32 claimed_hp` | 客户端说"我打了一下，我本地算我剩 claimed_hp" |
| `0x03` | `HP_SYNC` | `u32 player_id`, `f32 hp`, `f32 max_hp`, `u32 tick`, `u8 alive`, `u8[3] pad` | 服务端权威血量（payload 正好 20 字节，整帧 24 字节） |
| `0x04` | `SNAPSHOT` | `u16 count`, 然后 count 个 `{u32 id, f32 x, f32 y, f32 hp}`（每个 16 字节） | 大地图快照，用来演示 TCP 分段：count=1000 → payload 16002 字节 |
| `0x05` | `BYE` | 空 | 断开前告别 |

**服务端两种模式（这是整节课的安全主线）**：

- `--authoritative`（默认）：只信自己。`hp = max(0, hp - damage)`，`claimed_hp` 只用来打日志对比（"客户端说 73.00，我说 78.00，差 5.00"）。
  并且校验 `0 < damage <= 25`，超了回 `REJECT`（用 `HP_SYNC` 原值回＋日志 `REJECT`）。
- `--trust-client`：直接把 `claimed_hp` 当成真血量（**这就是现实中无数联机游戏的漏洞**）。

**客户端**：

- 先 `HELLO`，然后做 `count` 次攻击（默认 5 次，每次 `damage` 默认 5.0，可用 `--dmg` 改）。
- 客户端**自己也维护一份本地预测血量**（`predicted_hp -= damage`），收到 `HP_SYNC` 后和本地预测对比，不一致就打印 `RECONCILE`。
  这就是真实游戏的"客户端预测 + 服务端校正"。
- 每收发一帧，把**前 32 字节的十六进制**打到 stdout（这是文档里要用的真实帧字节）。
- 每帧后 `Sleep(200)`，让 TCP 不至于一股脑合并（也方便抓包肉眼看）。

其他约定：

- 端口：服务端 `47001`；tap 监听 `47000` 转发到 `47001`；客户端默认连 `47000`（可 `--port 47001` 直连）。
  **为什么不用 4000/4001**：实测这台机器上 `127.0.0.1:4001` 被 `QQ.exe`（PID 25492）占着，服务端 bind 会拿到
  WSA error 10013。这个坑要写进 NOTES，并让 run.ps1 启动前先探测端口占用、给出人话提示。
  服务端默认 `--bind 127.0.0.1`。
- 服务端和客户端都要有 `-O0 -g` 和 `-O2 -s` 两个版本（命名：`zombie_srv_O0.exe` / `zombie_srv_O2s.exe`，客户端同理）。
  `-O2 -s` 版是"真实游戏形态"，给逆向用。

## 每个人交付什么（验收标准）

### net-lab → `net\`

1. `zombie.h`、`zombie_srv.c`、`zombie_cli.c`、`tap.py`，`build.ps1`、`run.ps1`。
2. **实际跑通**：server + tap + client 三方同时跑，客户端 5 次攻击，服务端血量 100 → 75，打印对得上。
3. `evidence\`：
   - `build.txt`（编译输出 + 两个 exe 的体积）
   - `imports.txt`（`objdump -p` 里 ws2_32 的导入函数，证明"网络能力是调用别人的 DLL"）
   - `run.txt`（server+client 完整日志，带真实十六进制帧）
   - `tap.txt`（tap 看到的每个 chunk：时间、方向、长度、hex）
   - `netstat.txt`（运行中 `netstat -ano | findstr 47000` 和 `Get-NetTCPConnection`，要能看到 LISTENING / ESTABLISHED 和 PID）
   - `trust-client.txt`（`--trust-client` 模式下客户端发 `--dmg 5` 但 `claimed_hp` 被改成 1.00 时，服务端直接信了 → 这就是"改包秒回血"）
   - `big-frame.txt`（`--snapshot-count 1000` 那次，客户端 `recv` 被切成几次、每次多少字节；tap 那边看到几段）**这条特别重要，是 TCP 分段那一节的证据**
4. `NOTES.md`：真实命令、真实输出摘要、踩到的坑。

### os-lab → `os\`

1. `osprobe.c`（只链接 kernel32）+ `build.ps1` + `run.ps1` + `evidence\`。
   必须打印并解释得清的真实数据：
   - 模块基址（`GetModuleHandle(NULL)`）、一个全局变量地址、`HeapAlloc` 地址、当前函数里局部变量地址 —— 四个数放一起，**看地址空间的分区**
   - `VirtualQuery` 走一遍自己进程的内存区域：至少列出 image / heap / stack 三类区域及 `Protect`
   - PID、TID、`GetProcessHandleCount`
   - 再开一个线程，打印它的 TID 和**自己的栈范围**（用 `__readgsqword(0x08)` / `0x10` 读 TEB 的 StackBase/StackLimit，并注明这是 TEB 偏移）
   - `GetProcessTimes` 前后对比：跑 5000 次 `CreateEvent`+`CloseHandle`（真 syscall），打印 user/kernel 时间差 → 证明"系统调用花的是内核时间"
   - `GetFileType` 对文件句柄 / 管道 / 控制台 返回什么
2. `run.ps1` 要**跑两次**并把两次的地址并排打印（ASLR 证据），写进 `evidence\aslr.txt`。
3. `evidence\`：`run1.txt`、`run2.txt`、`aslr.txt`、`handles.txt`（运行中 `Get-Process -Id x | select Handles,Threads` + `tasklist /v`？tasklist 可以用）、`threads.txt`。
4. `NOTES.md`。

### capture-lab → `capture\`

1. `zombie.lua`：Wireshark/TShark 的 ZOMBIE 协议解析器（按上面的帧格式），要在 `tshark -X lua_script:zombie.lua` 下能正确打印出字段。
2. `capture.ps1`：**给用户自己在管理员 PowerShell 里跑**的抓包脚本。要求：
   - 先检查是不是管理员，不是就打印中文提示并退出
   - `dumpcap -i \Device\NPF_Loopback -f "tcp port 47000 or tcp port 47001" -w zombie.pcapng`
   - 提示用户"现在去另一个窗口跑 net\run.ps1"，回车停止
   - 停完自动调 `tshark` 生成 `dissect.txt`（一条流的完整解析）、`hex.txt`（前 3 帧的十六进制+ASCII）、`streams.txt`（`-T fields -e tcp.seq -e tcp.len -e tcp.flags`）
3. `expand.ps1`：**不需要权限**，给 Lead/用户把已有 pcapng 展开成上面那三个文本。这是我自己验证用的入口。
4. `text2pcap` 生成的**演示 pcap**：`demo.pcapng`。做法：把 net-lab 真实跑出来的帧十六进制（等 net-lab 出了 `tap.txt` 再取；如果你先做，就按协议自己构造一条完整流：SYN/SYN-ACK/ACK + HELLO + ATTACK + HP_SYNC + FIN），用 `text2pcap` 包成真 pcap。
   **必须在 NOTES.md 里明确写：这个 pcap 的 TCP 字节流来自真实运行，头是 text2pcap 合成的，只用来验证 lua 解析器。**
   然后用 `tshark -r demo.pcapng -X lua_script:zombie.lua` 跑通，把输出存 `evidence\demo-dissect.txt`。
5. `README.md`：给用户的抓包步骤（中文、逐步、含"你会看到什么"）。
6. `NOTES.md`。

## 风格样例（照这个写 NOTES / 注释）

```
[+0.000s] connect 127.0.0.1:47000  ok
[+0.001s] TX 24B  02 00 01 00 00 00 05 00 00 00 01 00 00 00 5f 00 00 00 ...  ATTACK dmg=5.0 seq=1 claimed=95.0
[+0.203s] RX 24B  03 00 01 00 ...                                              HP_SYNC hp=95.0
```

## 不允许做的事

- 不许 `git add/commit/push`
- 不许改别人的目录
- 不许编造输出（没有的东西就写"没跑出来，原因如下"）
- 不许写"外挂制作教程"：`--trust-client` 只作为**漏洞演示**存在，文档口径是"为什么服务端必须自己算"
