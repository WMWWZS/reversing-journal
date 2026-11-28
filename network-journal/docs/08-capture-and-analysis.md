# 08 · 抓包与协议分析

> 一句话：抓包的价值在于**证据**——先确定看哪一层，再用过滤器把噪声砍掉，最后跟随一条流把「谁先谁后」串成因果。

## 是什么 / 为什么重要

日志告诉你「应用认为发生了什么」，指标告诉你「平均有多快」，只有抓包能告诉你**链路上实际发生了什么**：

- 请求到底发出去了没有？
- 是 DNS 慢、TCP 握手慢、TLS 慢，还是服务器回得慢？
- 那个「偶发重传」是丢了、乱了，还是接收方窗口为零？

抓包不是「打开 Wireshark 看一堆包」，而是一个有明确步骤的流程：

```text
限定范围（capture filter） → 定位层次（display filter） → 看时序 → 跟随流 → 提取证据（包号 + 字段值）
```

## 机制与结构

### 1. 本机的工具现状

| 工具 | 本机状态 | 说明 |
|:---:|---|---|
| Wireshark / tshark / dumpcap | **已安装 4.0.17**（`E:\Wireshark`，不在 PATH） | 图形界面 + 命令行 + 抓包引擎，Npcap 已装 |
| tcpdump | 未安装（Windows 原生没有） | Linux/macOS 自带；Windows 上需 Npcap 与兼容版本 |
| WPA / xperf / wpr | 已安装（Windows Performance Toolkit 10.0.26100） | 系统级 ETW trace（ETL），不是 pcap 抓包；用 WPA 打开分析 |
| curl | 已安装（8.22.0） | 能看应用层交互，不是抓包 |
| Python 3.11 | 已安装（`py`） | 可手工生成/解析 pcap |
| nmap / ncat | 未安装 | 连通性检查用，需授权 |

> 本机装的是 Wireshark 4.0.17（`E:\Wireshark`，含 Npcap）：`tshark`、`dumpcap`、`text2pcap`、`capinfos` 都在同一目录，不在 PATH，所以本文命令写绝对路径或先 `cd E:\Wireshark`。**下面贴的都是实际跑过的输出，没跑过的会标「需安装」。** 一次完整的真实抓包 + Lua 解析见 [`labs/06-zombie-protocol`](../labs/06-zombie-protocol/)。

### 2. 两种过滤器，别混用

| 类型 | 位置 | 语法 | 作用时机 | 例子 |
|:---:|---|---|---|---|
| 捕获过滤器（capture filter） | 抓包引擎（dumpcap / libpcap） | BPF 语法 | **抓之前**丢弃，减少文件体积 | `tcp port 443 and host 192.0.2.10` |
| 显示过滤器（display filter） | Wireshark / tshark 解析后 | Wireshark 自己的语法 | **抓之后**筛选，不影响已抓数据 | `tcp.port == 443 && ip.addr == 192.0.2.10` |

两者语法完全不同：BPF 用 `and/or/not`，Wireshark 用 `&&/||/!`；BPF 里 `port 443`，显示过滤器里 `tcp.port == 443`。**在 GUI 的过滤栏里输入 BPF 语法会报红。**

### 3. 常用显示过滤器

| 目的 | 过滤器 |
|:---:|---|
| 某个 IP 的所有流量 | `ip.addr == 192.0.2.10` |
| 只看某个方向 | `ip.src == 192.0.2.10` / `ip.dst == 192.0.2.53` |
| 某个端口 | `tcp.port == 443`、`udp.port == 53` |
| 某条 TCP 连接 | `tcp.stream eq 0`（编号在 `Follow` 后可见） |
| 只看 SYN | `tcp.flags.syn == 1 && tcp.flags.ack == 0` |
| 只看 RST | `tcp.flags.reset == 1` |
| 重传 / 快速重传 | `tcp.analysis.retransmission` / `tcp.analysis.fast_retransmission` |
| 零窗口 | `tcp.analysis.zero_window` |
| HTTP 请求 / 错误响应 | `http.request.method == "GET"` / `http.response.code >= 400` |
| DNS 查询名 | `dns.qry.name == "example.com"` |
| DNS 失败 | `dns.flags.rcode != 0` |
| TLS SNI | `tls.handshake.extensions_server_name == "example.com"` |
| 负载里含某串 | `frame contains "GET /"` |
| HTTP/2 头部帧 | `http2.type == 0x1` |

### 4. tcpdump 与 tshark 的常用姿势

```powershell
# tshark 已装（E:\Wireshark\tshark.exe，不在 PATH 就写绝对路径）；tcpdump 本机没有
tcpdump -i eth0 -nn -s 0 -w cap.pcap 'tcp port 443'   # Linux/macOS，本机需另装
tshark -D                                              # 列出网卡
tshark -i 1 -f "tcp port 80" -c 20                     # 抓 20 个包
tshark -r cap.pcap -Y "http.request" -T fields -e http.host -e http.request.uri
tshark -r cap.pcap -z follow,tcp,ascii,0               # 跟随第 0 条流
tshark -r cap.pcap -Y "tcp.analysis.retransmission" -T fields -e frame.number -e tcp.seq
```

`-f` 是捕获过滤器（BPF），`-Y` 是显示过滤器；`-T fields -e` 是「只输出字段」，适合把结果导进表格。

### 5. 跟随 TCP 流

Wireshark 里右键任意一个包 → `Follow` → `TCP Stream`，会弹出该连接的全部应用层数据，客户端与服务器方向用不同颜色区分。这是**看清一次 HTTP 交互最快的方法**：请求头、响应头、响应体按顺序排在一起。

要注意：跟随流看到的是「重新组装后的字节流」，不代表每个 TCP 段一一对应。一个请求可能被拆成多个段，多个响应也可能连在一起。

### 6. 定位问题的过滤器组合

| 症状 | 先看什么 | 过滤器 |
|:---:|---|---|
| 页面打不开 | 有没有 DNS 响应、有没有 RST | `dns \|\| tcp.flags.reset == 1` |
| 建连慢 | SYN → SYN+ACK 的时间差 | `tcp.flags.syn == 1` |
| 传输慢 | 重传、零窗口、窗口更新 | `tcp.analysis.retransmission \|\| tcp.analysis.zero_window` |
| TLS 失败 | 握手在哪一步断 | `tls.handshake.type == 1 \|\| tls.alert_message` |
| 接口 4xx/5xx | 响应码与响应体 | `http.response.code >= 400` |
| 只影响某个域名 | SNI / Host | `tls.handshake.extensions_server_name == "x"` |

## 动手验证

### 验证 1：用 curl 记录一次真实交互

```powershell
curl.exe --trace-ascii trace.txt -s -o NUL --max-time 8 http://example.com/
Get-Content trace.txt -TotalCount 22
```

本机实测（截取发送头部分）：

```text
=> Send header, 75 bytes (0x4b)
0000: GET / HTTP/1.1
0010: Host: example.com
0023: User-Agent: curl/8.22.0
003c: Accept: */*
0049:
```

`0000/0010/0023/003c/0049` 是十六进制偏移，`0049` = 73，最后两字节是 `\r\n`，凑满 75 字节——和 [01 篇](01-layers-and-encapsulation.md) 里算的 HTTP 明文长度完全一致。这就是「没有抓包工具时的最小证据」：字节偏移 + 长度 + 内容。

### 验证 2：生成一个可练习的 pcap

```powershell
# 在仓库根目录执行
py tools\make-pcap.py
```

本机实测输出：

```text
文件      : sample.pcap
大小      : 191 字节
帧数      : 2
链路类型  : 1（Ethernet）
TCP SYN   : 以太网帧 60 字节，IPv4 total_length=40
DNS 查询  : 以太网帧 75 字节，IPv4 total_length=61

[ok] 读回 2 帧，内容与写入一致
[ok] 每帧 IPv4 total_length <= 帧长 - 14，差额是以太网最小帧补齐的 0 字节
[ok] 每帧 IPv4 头校验和重算 == 0x0000
```

验证文件头（前 24 字节是 pcap 全局头，magic 小端写作 `d4 c3 b2 a1`）：

```powershell
Format-Hex -Path .\sample.pcap | Select-Object -First 2
```

预期看到 `D4 C3 B2 A1` 开头，随后是版本 `02 00 04 00`、时区、snaplen `FF FF 00 00`、链路类型 `01 00 00 00`。

### 验证 3：用 tshark 打开 sample.pcap

> Wireshark 4.0.17 已装在 `E:\Wireshark`（`tshark.exe` 也在那里，不在 PATH 就写绝对路径）。

```powershell
& 'E:\Wireshark\tshark.exe' -r sample.pcap
& 'E:\Wireshark\tshark.exe' -r sample.pcap -Y "dns.qry.name == \"www.example.com\""
& 'E:\Wireshark\tshark.exe' -r sample.pcap -Y "tcp.flags.syn == 1"
```

两条过滤器分别命中 1 个包。GUI 里可以继续练：

- 选中任一帧，展开 `Ethernet II → Internet Protocol Version 4 → Transmission Control Protocol`，对照 [02 篇](02-ethernet-arp-ip.md) 的字段表；
- `Statistics → Protocol Hierarchy` 看协议分布；
- `Statistics → Conversations` 看每条连接收发了多少字节；
- 把 `View → Time Display Format` 改成「Seconds Since Beginning of Capture」，手算 SYN 与 SYN+ACK 的时间差，就是一次 RTT 的近似值。

### 验证 4：Windows 自带的另一种抓法

```powershell
netsh trace start capture=yes tracefile=net.etl
# 复现问题
netsh trace stop
```

本机已装 Windows Performance Toolkit（WPA / xperf 10.0.26100，`wpr.exe` 在 `system32`），`netsh trace` 生成的 ETL 可以直接用 WPA 打开分析；但 ETL 不是 pcap，Wireshark 读不了，要转格式才行，而且 `netsh trace` 需要管理员权限。它的价值是在**不能装第三方软件**的机器上留下系统级证据；日常协议分析还是 Wireshark 更直接。

## 常见误区

1. **很多人以为抓包能抓到局域网里所有人的流量，实际交换机会按 MAC 隔离，只有镜像端口、ARP 欺骗或 Wi-Fi 监听模式才能看到别人的流量**，因为普通网卡只收到发给自己的帧。
2. **很多人把捕获过滤器和显示过滤器混着写，实际两者语法完全不同**，因为一个工作在抓包引擎（BPF）、一个工作在解析后的协议树。
3. **很多人以为 HTTPS 抓到了就能看明文，实际没有会话密钥只能看到密文**，因为 TLS 1.3 应用数据是 AEAD 加密的；只有配置 `SSLKEYLOGFILE` 或拥有私钥才能解密。
4. **很多人以为 Wireshark 里一个 TCP 段就是一条业务消息，实际 TCP 是字节流，消息边界要应用协议自己定**，因为粘包和半包是常态（见 [09 篇](09-custom-binary-protocol.md)）。
5. **很多人把 pcap 直接发到群里求助，实际抓包文件常含 Cookie、Token、内网地址等敏感信息**，因为 HTTP 头和 URL 参数默认都是明文，分享前必须脱敏并确认有授权。

## 自测

**1. 只想抓 443 端口、且只保留 TCP 握手，捕获过滤器和显示过滤器分别怎么写？**

- **考点**：两种过滤器的分工
- **应明白**：捕获阶段用 BPF 减少体积，显示阶段再细筛
- **算会**：捕获 `tcp port 443`，显示 `tcp.flags.syn == 1 || (tcp.flags.syn == 1 && tcp.flags.ack == 1)`（或直接 `tcp.flags.syn == 1` 同时覆盖两者）

**2. 在 Follow TCP Stream 里看到两个请求头连续出现，没有任何响应间隔，说明什么？**

- **考点**：连接复用
- **应明白**：这是同一条 keep-alive 连接上先后发的两个请求
- **算会**：能指出「不是重复发包，是复用了一条连接」，并知道可以用 `tcp.stream eq N` 隔离这条流

**3. 抓包里出现 `tcp.analysis.retransmission`，但没有零窗口，下一步查什么？**

- **考点**：重传的归因
- **应明白**：重传可能是真丢包、乱序、伪重传
- **算会**：先看是否有重复 ACK / SACK、再看 RTT 与 RTO 是否异常、最后确认是否 `spurious_retransmission`

## 关联

- 上一篇：[07 · HTTP/2 与 HTTP/3](07-http2-http3-quic.md)
- 下一篇：[09 · 自定义二进制协议](09-custom-binary-protocol.md)
- 抓包怎么用于排障：[10 · 延迟分解与故障排查](10-latency-and-troubleshooting.md)
- 一次完整的真实抓包 + Lua 解析：[`labs/06-zombie-protocol`](../labs/06-zombie-protocol/)（真 TCP 靶场、loopback 抓包、`tshark -Y zombie`）
- 练习数据生成器：[`tools/make-pcap.py`](../tools/make-pcap.py)
- 合法与授权边界：[`resources.md`](../resources.md)
