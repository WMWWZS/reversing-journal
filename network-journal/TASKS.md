# 任务清单

> 勾掉一个才算学完一个。每两周复盘一次。
> 原则：**以「能讲出来」验收，不以「看完了」验收。**

---

## 阶段 1（第 1–2 周）做完之后你能回答什么

**这两周不要求你「看完什么」，只要求你能独立回答下面 6 个问题。** 答不上来就是没学会。

| # | 问题 | 对应能力 | 在哪练 |
|:---:|---|---|---|
| 1 | 一次 HTTPS 请求在每一层各加多少字节？以太网帧一共多少字节？ | 封装与头部长度 | [01 篇](docs/01-layers-and-encapsulation.md) 自测 1 |
| 2 | 同一网段和跨网段时，ARP 请求里的 Target IP 分别填谁？ | 二层转发与下一跳 | [02 篇](docs/02-ethernet-arp-ip.md) 自测 2 |
| 3 | MTU 1500、IPv4 无选项、TCP 无选项时 MSS 是多少？带 12 字节选项呢？ | MSS 计算 | [01 篇](docs/01-layers-and-encapsulation.md) 验证 3 |
| 4 | `192.0.2.130/25` 的网络地址、广播地址、可用主机数？ | CIDR 与与运算 | [02 篇](docs/02-ethernet-arp-ip.md) 验证 3 |
| 5 | IPv4 头校验和覆盖哪些字节？为什么每跳都要重算？ | IPv4 校验和 | lab01 自测 1 |
| 6 | `api.example.com` 的 QNAME 编码是多少字节？ | DNS 名字编码 | lab01 自测 2 |

> **检验方式**：合上电脑，用嘴讲出来。讲得清楚才是真懂，讲不清楚就是没懂。

---

## 第 1 周：分层与封装

- [ ] 读 [01 · 分层与封装](docs/01-layers-and-encapsulation.md)，合上文档画出 HTTP → Ethernet 的封装顺序
- [ ] 跑通 [`labs/01-packet-craft`](labs/01-packet-craft/)：`py craft.py`，确认 7 条 `[ok]` 全部出现
- [ ] 手算 75 字节 HTTP 请求的 HTTPS 帧长（150 字节），与 lab 的 IPv4 头对照
- [ ] 用 `curl.exe -v http://example.com/` 看请求/响应头，指出哪些是应用层
- [ ] 在 `notes/` 写 1 条：我原以为分层是「复制打包」，实际是「只加头」

**本周产出**：1 条 `notes/` + lab 跑通的截图或输出。

## 第 2 周：以太网、ARP 与 IPv4

- [ ] 读 [02 · 以太网 / ARP / IPv4](docs/02-ethernet-arp-ip.md)
- [ ] 用 `arp -a` 和 `ipconfig` 找出本机网关的 IP 与 MAC，算出掩码对应的网段
- [ ] 用 Python `ipaddress` 算 `/25`、`/26`、`/30` 的网络地址与可用主机数
- [ ] 用 `ping -f -l 1472 example.com` 验证 DF 位，记录「超时」和「需要分片」两种结果的区别
- [ ] 手算 4000 字节载荷在 MTU 1500 下的三片长度、MF、offset
- [ ] 阶段复盘：不看文档回答上面 6 个问题

**本周产出**：3 条 `notes/` + 一张自己画的封装图。

---

## 第 3 周：TCP 三次握手与状态机

- [ ] 读 [03 · TCP 三次握手](docs/03-tcp-handshake.md)
- [ ] 跑 lab 的 TCP SYN 段，逐字段解释 `seq`、`data offset`、`flags`、`window`
- [ ] 按文档做双窗口实验：`listen.py` + `netstat -ano`，观察 `LISTENING` / `ESTABLISHED`
- [ ] 客户端退出后，找出 `CLOSE_WAIT` 和 `FIN_WAIT_2` 分别出现在哪一侧
- [ ] 用 `netstat -ano | Select-String "TIME_WAIT"` 数本机的 TIME_WAIT，解释原因
- [ ] 在 `notes/` 写 1 条：为什么 CLOSE_WAIT 堆积是代码问题

**本周产出**：1 条带 `netstat` 输出的 `notes/`。

## 第 4 周：TCP 可靠传输与拥塞控制

- [ ] 读 [04 · TCP 可靠传输](docs/04-tcp-reliability.md)
- [ ] `netsh interface tcp show global`，逐项解释 Initial RTO、窗口自动调整、ECN
- [ ] 间隔 5 秒读两次 `netstat -s -p tcp`，算这段时间的重传增量与比例
- [ ] 跑文档里的 RTO 估计脚本，把 `min_rto` 改成 200 再跑一次，对比差异
- [ ] 说清「超时重传」和「快速重传」对 cwnd 的不同处理
- [ ] 阶段复盘：回答「重传一定是丢包吗？还有哪三种可能？」

**本周产出**：1 条 `notes/`（RTO 计算 + 本机参数解析）。

---

## 第 5 周：UDP 与 DNS

- [ ] 读 [05 · UDP 与 DNS](docs/05-udp-and-dns.md)
- [ ] 跑 lab 的 DNS 查询，手写 `www.example.com` 的 QNAME 编码并核对
- [ ] `Resolve-DnsName example.com -Type A` 与 `-Type NS`，记录 TTL 与两条 A 记录
- [ ] 解释 DNS 什么时候走 TCP（至少 3 种场景）
- [ ] 画出客户端 → 递归解析器 → 根 → TLD → 权威的迭代查询图
- [ ] 在 `notes/` 写 1 条：本机 `nslookup` 超时但 `Resolve-DnsName` 正常，为什么

**本周产出**：1 条带真实 DNS 查询结果的 `notes/`。

## 第 6 周：HTTP/1.1 与 TLS 1.3

- [ ] 读 [06 · HTTP 与 TLS](docs/06-http-and-tls.md)
- [ ] `curl.exe -v http://example.com/`，指出请求行、头、空行的位置
- [ ] 用 Python `ssl` 探测 TLS 版本、密码套件、ALPN，记录 `TLSv1.3 / TLS_AES_256_GCM_SHA384 / h2`
- [ ] 数本机根证书：`(Get-ChildItem Cert:\LocalMachine\Root | Measure-Object).Count`
- [ ] 解释 `ETag` + `If-None-Match` 在第 30 秒和第 90 秒分别发生什么
- [ ] 阶段复盘：回答「TLS 1.3 的 1-RTT 省掉了哪一次往返？」

**本周产出**：1 条 `notes/`（TLS 参数 + 证书链笔记）。

---

## 第 7 周：HTTP/2 与 QUIC 基础

- [ ] 读 [07 · HTTP/2 与 HTTP/3](docs/07-http2-http3-quic.md)
- [ ] 用 Python 拼一个 h2 HEADERS 帧头，手读 9 个字节的每个字段
- [ ] 用 ALPN 探测服务器是否支持 `h2`，记录结果
- [ ] `curl.exe --http2` 与 `--http3` 各跑一次，记录「工具不支持」而不是「网络失败」
- [ ] 画出 HTTP/3 的分层：HTTP/3 → QUIC streams → QUIC → UDP → IP
- [ ] 在 `notes/` 写 1 条：为什么 HTTP/2 丢包时可能比 HTTP/1.1 更慢

**本周产出**：1 条 `notes/` + 一张 h2 帧头手绘图。

## 第 8 周：HTTP/3 与 QUIC 深入

- [ ] 列出 QUIC 长包头的字段，说明 DCID / SCID 的作用
- [ ] 说清 QUIC 的连接迁移与 TCP 四元组的区别
- [ ] 列举 QUIC 的 5 种帧类型（PADDING / PING / ACK / CRYPTO / STREAM）及用途
- [ ] 解释 QPACK 为什么比 HPACK 更适合 QUIC
- [ ] 阶段复盘：写出 HTTP/1.1、HTTP/2、HTTP/3 的对比表（传输层、多路复用、队头阻塞、握手 RTT）

**本周产出**：1 条 `notes/`（三代协议对比表）。

---

## 第 9 周：抓包工具与过滤器

- [ ] 确认 Wireshark 装好（本机 `E:\Wireshark`，4.0.17，Npcap 已装）：`& 'E:\Wireshark\tshark.exe' -D` 能列出网卡
- [ ] 读 [08 · 抓包与协议分析](docs/08-capture-and-analysis.md)
- [ ] 跑 `py tools/make-pcap.py` 生成 `sample.pcap`，用 Wireshark 打开
- [ ] 在 `sample.pcap` 上验证：`tcp.flags.syn == 1`、`dns.qry.name == "www.example.com"` 各命中 1 个包
- [ ] 抓一次自己机器的 HTTP 请求，用 `Follow → TCP Stream` 看清请求/响应
- [ ] 用 [lab06](labs/06-zombie-protocol/) 的 `capture.ps1` 抓一次 `127.0.0.1:47000/47001` 的 loopback，确认 `evidence\streams.txt` 里有 `zombie.opcode`
- [ ] 在 `notes/` 写 1 条：捕获过滤器与显示过滤器混用会怎样

**本周产出**：1 个自己的 pcap（不入库）+ 1 条带过滤器的 `notes/`。

## 第 10 周：自定义二进制协议

- [ ] 读 [09 · 自定义二进制协议](docs/09-custom-binary-protocol.md)
- [ ] 跑 `py tools/frame-demo.py`，确认按 3 字节分块能解出 2 帧
- [ ] 把分块大小改成 1 和 7，各跑一次，记录缓冲区变化
- [ ] 跑通 [`labs/06-zombie-protocol`](labs/06-zombie-protocol/)：`run.ps1` 的 13 条自检全部 `PASS`、`VERDICT: PASS`
- [ ] 从 `tap.txt` / `run.txt` 里拆出 ATTACK 帧的 4 段：`total_len(2) + opcode(1) + flags(1) + payload(12)`，并说出 payload 里 3 个字段
- [ ] 用两条不同长度的消息验证长度口径：`total_len` 10/14/22 + 2 = 整帧 12/16/24
- [ ] 用 Ghidra 找到 `send` 的调用者 `FUN_140001be0`，指出 `frame[0] = payload_len + 2` 与 `uVar5 = payload_len + 4`（证据在 `re/evidence/`）
- [ ] 用 `tap.py --hack-claimed` 做改包回血实验：`--trust-client` 血 100 → 1，`--authoritative` 照掉到 75
- [ ] 自己设计一个 12 字节头（magic/version/cmd/len/seq/crc），写编码 + 解码
- [ ] 用「两条不同长度的消息」验证自己设计的长度字段口径
- [ ] 阶段复盘：给出一个陌生 TCP 负载，判断它是定长、分隔符还是长度字段

**本周产出**：一份自定义协议格式表（见 lab06 README）+ 1 条 `notes/`。

---

## 第 11 周：延迟分解

- [ ] 读 [10 · 延迟分解与故障排查](docs/10-latency-and-troubleshooting.md)
- [ ] 用 `curl -w` 对 3 个不同地址各测一次，把五段填进表
- [ ] `netsh interface ipv4 show subinterfaces` 记录本机 MTU，算出 MSS
- [ ] 间隔读两次 `netstat -s -p tcp`，算出重传增量
- [ ] 复述「丢包 / 乱序 / 零窗口 / 伪重传」的区分证据
- [ ] 在 `notes/` 写 1 条：一次延迟拆解，指出瓶颈在哪一段

**本周产出**：一份延迟拆解表 + 1 条 `notes/`。

## 第 12 周：综合排障与复盘

- [ ] 挑一个自己能复现的「慢」场景（本地服务即可），完成五段拆解
- [ ] 写出结论：瓶颈在哪一层、证据是什么、下一步验证什么
- [ ] 用决策树走一遍，确认没有跳步
- [ ] 回看 11 篇文档，把每篇的 3 条自测重新答一遍
- [ ] 用三个检验标准（见 [`docs/00-roadmap.md`](docs/00-roadmap.md)）给自己打分
- [ ] 整理 `notes/` 索引，把可复用的结论提炼成 3 条

**本周产出**：1 份完整的排障报告（`notes/` 里一篇长文）。

---

## 每天怎么安排

| 时长 | 内容 | 说明 |
|---:|---|---|
| 30 分钟 | 读 | 只读当天要用到的部分，不要通读 |
| 60 分钟 | 动手 | 跑命令、改 lab、抓包，**必须动手** |
| 10 分钟 | 写 | 在 `notes/` 留一条，写清卡在哪 |

## 记录规则

每个任务做完，在 `notes/` 里写一条，回答三个问题：

1. 我做了什么？（命令 + 输出）
2. 卡在哪一步？怎么解决的？
3. 学到了什么新东西？

模板：[`notes/_TEMPLATE.md`](notes/_TEMPLATE.md)

## 不要做的事

- ❌ 从《TCP/IP 详解》第一页开始通读
- ❌ 看几小时视频教程却不动手抓一次包
- ❌ 只记「total 慢」，不做五段分解
- ❌ 抓到重传就断言「网络丢包」，不看零窗口和 SACK
- ❌ 把抓包文件直接传到网上（可能含 Cookie、Token、内网地址）
- ❌ 抓不属于自己的网络流量，或在生产网做扫描/注入
- ❌ 收藏一堆工具却一个都没用过
- ❌ 背文档里的数字，却不跑一遍验证
