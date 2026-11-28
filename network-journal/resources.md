# 资源清单

> 只列真实存在、且能直接打开的入口。工具状态按本机实测标注（Wireshark 4.0.17 已安装）。

## 书

| 书名 | 说明 |
|---|---|
| **《TCP/IP 详解 卷 1：协议》**（W. Richard Stevens） | 协议字段与行为的权威参考，配合抓包逐章对照 |
| **《计算机网络：自顶向下方法》**（Kurose / Ross） | 分层与传输层的最佳教材，适合建立整体框架 |
| **《HTTP 权威指南》**（Gourley 等） | HTTP/1.1 报文、缓存、连接的细节 |
| **《High Performance Browser Networking》**（Ilya Grigorik） | 免费在线 <https://hpbn.co/>；延迟、TCP、TLS、HTTP/2 的性能视角 |
| **《Wireshark 数据包分析实战》**（Chris Sanders） | 抓包流程与排障手法，适合配合第 8 篇 |
| **《DNS 与 BIND》**（Cricket Liu 等） | DNS 报文、递归、缓存与 TTL 的深入参考 |

> 不要从第一页开始啃。先读当前阶段对应的章节，带着抓包问题去查。

## 抓包与分析工具

| 工具 | 入口 | 本机状态 | 用途 |
|:---:|---|:---:|---|
| Wireshark | <https://www.wireshark.org/download.html> | ✅ 4.0.17（`E:\Wireshark`） | 图形化抓包、过滤器、跟随 TCP 流 |
| Npcap | <https://npcap.com/> | ✅ 已安装 | Windows 抓包驱动，装 Wireshark 时勾选 |
| tcpdump | <https://www.tcpdump.org/> | ⬜ 未安装（Windows 原生没有） | 命令行抓包（Linux/macOS 常用） |
| curl | <https://curl.se/> | ✅ 8.22.0 | HTTP 验证、`-w` 五段计时、`--trace-ascii` |
| Python | <https://www.python.org/> | ✅ 3.11（用 `py`） | 所有 lab 与字节级验证 |

本机已装 Wireshark 4.0.17 + Npcap（`E:\Wireshark`，`tshark`/`dumpcap`/`text2pcap`/`capinfos` 同目录，不在 PATH）。换机器安装时注意：**勾选 Npcap**，否则没有网卡可抓。本机实测**非管理员窗口也能抓 loopback**（见 [`labs/06-zombie-protocol`](labs/06-zombie-protocol/)），但公司电脑可能没有安装权限，此时先用 curl + Python 完成验证。

## 公开练习数据

| 来源 | 入口 | 说明 |
|:---:|---|---|
| Wireshark SampleCaptures | <https://wiki.wireshark.org/SampleCaptures> | 官方示例抓包集合，按协议分类（HTTP、DNS、TCP、ARP 等） |
| Wireshark 用户手册 | <https://www.wireshark.org/docs/> | 过滤器语法与界面操作 |
| tshark.dev 抓包检索 | <https://tshark.dev/search/pcaptable/> | 跨 Wireshark 示例与 PacketLife 的抓包检索 |
| 本仓库自造数据 | [`tools/make-pcap.py`](tools/make-pcap.py) | 生成 `sample.pcap`（1 个 TCP SYN + 1 个 DNS 查询），内容完全可控 |
| 本仓库真实靶场流量 | [`labs/06-zombie-protocol`](labs/06-zombie-protocol/) | 真 TCP 的 ZOMBIE 协议：`run.ps1` 造流量、`mkpcap.py` 造 demo.pcapng、`capture.ps1` 真抓 loopback |
| 本仓库组帧示例 | [`tools/frame-demo.py`](tools/frame-demo.py) | 长度字段 + TLV 的组帧/拆帧与粘包处理 |

> 第三方抓包（尤其是安全事件流量）可能含恶意内容或隐私数据，**只在隔离环境离线分析**，不要在生产机打开，也不要执行其中的载荷。

## RFC 与官方文档入口

| 编号 | 主题 | 链接 |
|:---:|---|---|
| RFC 791 | IPv4 | <https://www.rfc-editor.org/rfc/rfc791.html> |
| RFC 792 | ICMP | <https://www.rfc-editor.org/rfc/rfc792.html> |
| RFC 768 | UDP | <https://www.rfc-editor.org/rfc/rfc768.html> |
| RFC 9293 | TCP（取代 RFC 793） | <https://www.rfc-editor.org/rfc/rfc9293.html> |
| RFC 826 | ARP | <https://www.rfc-editor.org/rfc/rfc826.html> |
| RFC 1034 / 1035 | DNS | <https://www.rfc-editor.org/rfc/rfc1035.html> |
| RFC 1918 | 私有地址 | <https://www.rfc-editor.org/rfc/rfc1918.html> |
| RFC 5737 | 文档专用地址 | <https://www.rfc-editor.org/rfc/rfc5737.html> |
| RFC 6298 | RTO 计算 | <https://www.rfc-editor.org/rfc/rfc6298.html> |
| RFC 5681 | 拥塞控制 | <https://www.rfc-editor.org/rfc/rfc5681.html> |
| RFC 2018 | SACK | <https://www.rfc-editor.org/rfc/rfc2018.html> |
| RFC 7323 | 窗口缩放 / 时间戳 | <https://www.rfc-editor.org/rfc/rfc7323.html> |
| RFC 8446 | TLS 1.3 | <https://www.rfc-editor.org/rfc/rfc8446.html> |
| RFC 9110 | HTTP 语义 | <https://www.rfc-editor.org/rfc/rfc9110.html> |
| RFC 9111 | HTTP 缓存 | <https://www.rfc-editor.org/rfc/rfc9111.html> |
| RFC 9112 | HTTP/1.1 | <https://www.rfc-editor.org/rfc/rfc9112.html> |
| RFC 9113 | HTTP/2 | <https://www.rfc-editor.org/rfc/rfc9113.html> |
| RFC 7541 | HPACK | <https://www.rfc-editor.org/rfc/rfc7541.html> |
| RFC 9114 | HTTP/3 | <https://www.rfc-editor.org/rfc/rfc9114.html> |
| RFC 9204 | QPACK | <https://www.rfc-editor.org/rfc/rfc9204.html> |
| RFC 9000 | QUIC | <https://www.rfc-editor.org/rfc/rfc9000.html> |
| RFC 9002 | QUIC 丢包恢复 | <https://www.rfc-editor.org/rfc/rfc9002.html> |
| RFC 8899 | PLPMTUD（路径 MTU 探测） | <https://www.rfc-editor.org/rfc/rfc8899.html> |

官方入口：

- RFC Editor：<https://www.rfc-editor.org/>
- IETF Datatracker（草案与工作组）：<https://datatracker.ietf.org/>
- HTTP 工作组：<https://httpwg.org/>
- QUIC 工作组：<https://quicwg.org/>
- IANA 协议号：<https://www.iana.org/assignments/protocol-numbers/>
- IANA 端口号：<https://www.iana.org/assignments/service-names-port-numbers/>
- MDN HTTP 文档：<https://developer.mozilla.org/en-US/docs/Web/HTTP>
- Cloudflare 学习中心：<https://www.cloudflare.com/learning/>

## 合法与授权边界

> **只抓自己有权限的网络流量。** 抓包能看到一切明文内容，包括别人的密码、Cookie 和聊天记录——技术上做得到，不等于法律上可以做。

| 边界 | 具体要求 |
|:---:|---|
| **授权范围** | 只抓自己的设备、自己负责的网络，或有书面授权的目标；公司网络先找 IT/安全团队拿书面许可 |
| **公共网络** | 咖啡店、机场、酒店的 Wi-Fi 抓包可能违反服务条款，也可能触犯法律；不做 |
| **不做攻击性测试** | 不扫描、不注入、不中间人劫持他人流量、不重放他人会话；端口扫描同样可能违法或违约 |
| **数据脱敏** | pcap 里常有 `Authorization`、Cookie、Token、内网 IP、个人信息；分享或提交前必须过滤，pcap 默认不入库 |
| **不碰生产环境** | 生产网的抓包要限流、限时长、限定过滤器，避免影响业务；优先在镜像口/测试环境做 |
| **法律底线** | 《网络安全法》《数据安全法》《个人信息保护法》；《刑法》第 285、286 条。具体条文以官方文本为准 |

本仓库的自我约束：

- 所有 lab 只用 RFC 5737 文档专用地址在内存里构造字节，**不发送真实流量**；
- 所有示例命令只读取本机配置（`netsh`、`netstat`、`arp`）或访问公开示例站点；
- 不提供、不链接任何攻击工具与漏洞利用代码。

## 学习节奏参考

- 每读一个协议，就抓一次真实流量对照字段（没有 Wireshark 时用 `py` + 手工解析）；
- 每遇到一个「慢」，就用 [`docs/10`](docs/10-latency-and-troubleshooting.md) 的五段法拆一次；
- 每学完一个阶段，回到 [`TASKS.md`](TASKS.md) 勾掉对应周的复选框，写一条 `notes/`。
