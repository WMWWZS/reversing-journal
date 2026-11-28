# network-journal · 文档索引

> 支线：网络——分层封装、以太网/ARP/IP、TCP 握手与可靠性、UDP/DNS、HTTP/TLS、HTTP2/3、抓包、私有协议、延迟排障。

文件名保持英文，是为了不打断各文档之间的相对链接；**标题与一句话摘要都是中文**，看这一页就能决定读哪篇。

| 文件 | 中文标题 | 一句话 |
|---|---|---|
| [00-roadmap.md](00-roadmap.md) | 学习路线：12 周（支撑线） | 一句话：用 12 周把「能上网」变成「能解释每一个字节、能定位每一秒延迟」——并且能说明白「服务端到底信不信客户端」。 |
| [01-layers-and-encapsulation.md](01-layers-and-encapsulation.md) | 01 · 分层模型与数据封装 | 一句话：一次 HTTP 请求从应用到网线，每一层只在自己的头部加上该层的信息，不多也不少；排障的第一步永远是判断「问题在哪一层」。 |
| [02-ethernet-arp-ip.md](02-ethernet-arp-ip.md) | 02 · 以太网 / ARP / IPv4 | 一句话：同一根线（同一个广播域）里靠 MAC 找到下一跳，跨网段靠 IP 找路由；ARP 就是「我知道你的 IP，但请把 MAC 告诉我」的那句问话。 |
| [03-tcp-handshake.md](03-tcp-handshake.md) | 03 · TCP 三次握手 | 一句话：三次握手不是「打个招呼」，而是双方各自确认「我能发、你能收」并交换初始序列号；少一次会有历史连接的风险，多一次纯属浪费。 |
| [04-tcp-reliability.md](04-tcp-reliability.md) | 04 · TCP 可靠传输 | 一句话：TCP 的「可靠」不是保证不丢，而是保证丢了能发现、能重发，而且不会因为重发把网络压垮——发现靠序列号与 ACK，重发靠 RTO 与快速重传，不压垮靠滑动窗口与拥塞控制。 |
| [05-udp-and-dns.md](05-udp-and-dns.md) | 05 · UDP 与 DNS | 一句话：UDP 只负责把数据报丢出去，不保证顺序也不保证到达；DNS 就用这个最便宜的传输，把「域名 → 地址」这件事问清楚，并且靠 TTL 决定答案能缓存多久。 |
| [06-http-and-tls.md](06-http-and-tls.md) | 06 · HTTP 与 TLS | 一句话：HTTP/1.1 把请求写成「起始行 + 头 + 空行 + 体」的文本，TLS 1.3 用一次往返就谈完密钥，证书链则负责回答「这个公钥真的属于这个域名吗」。 |
| [07-http2-http3-quic.md](07-http2-http3-quic.md) | 07 · HTTP/2、HTTP/3 与 QUIC | 一句话：HTTP/2 用二进制分帧和一条 TCP 连接解决应用层队头阻塞，HTTP/3 干脆把传输层换成基于 UDP 的 QUIC，让每条流真正互不阻塞。 |
| [08-capture-and-analysis.md](08-capture-and-analysis.md) | 08 · 抓包与协议分析 | 一句话：抓包的价值在于证据——先确定看哪一层，再用过滤器把噪声砍掉，最后跟随一条流把「谁先谁后」串成因果。 |
| [09-custom-binary-protocol.md](09-custom-binary-protocol.md) | 09 · 自定义二进制协议：定界与字段还原 | 一句话：TCP 只给你一条连续字节流，消息边界必须由应用协议自己定义；自定义协议的逆向，就是先找出「怎么切」，再还原「每一段是什么」。 |
| [10-latency-and-troubleshooting.md](10-latency-and-troubleshooting.md) | 10 · 延迟分解与故障排查 | 一句话：把「慢」拆成 DNS、TCP、TLS、TTFB、传输五段，每一段都有可测的数字和可验证的抓包证据；拆不开，就只能靠猜。 |

共 11 篇。