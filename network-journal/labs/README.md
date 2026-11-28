# Labs · 实验

> **看懂 ≠ 会了。** 每个概念都要有一个能跑、能改、能自己验证的实验。

## 约定

每个实验一个目录，编号与 `docs/` 对应，至少包含：

```text
lab-name/
├── README.md      # 目的、原理、预期输出、自测题
├── run.ps1        # 一键运行（或 build.ps1）
└── *.py           # 源码：纯标准库，不依赖第三方包
```

内容多的 lab 可以再分子目录（如 [lab06](06-zombie-protocol/) 的 `src/` `capture/` `re/` 与各自的 `evidence/`），但「能一键跑、README 贴真实输出」这两条不变。

三条硬规则：

1. **必须在本机真实跑通**，README 里贴的是真实输出，不是「预期大概是这样」；
2. **零第三方依赖**（Python 只用标准库），换一台装了 Python 3 的机器就能跑；
3. **不发送真实流量**：练习数据用 RFC 5737 文档专用地址（`192.0.2.0/24` 等）构造；需要**真 TCP** 的 lab（如 06）只连本机回环 `127.0.0.1`，不开外网连接。

抓包文件（`.pcap`）体积大且可能含 Cookie/Token 等敏感信息，**默认不提交**，用脚本现场生成。

## 实验列表

| 编号 | 实验 | 对应笔记 | 状态 |
|:---:|---|---|:---:|
| 01 | 手工构造 IPv4/TCP/UDP/DNS 字节 | [01 · 分层与封装](../docs/01-layers-and-encapsulation.md)、[02 · 以太网/ARP/IPv4](../docs/02-ethernet-arp-ip.md) | ✅ 完成 |
| 02 | 解析 DNS 响应（含压缩指针） | [05 · UDP 与 DNS](../docs/05-udp-and-dns.md) | ⬜ 待做 |
| 03 | 观察 TCP 状态迁移 | [03 · TCP 三次握手](../docs/03-tcp-handshake.md) | ⬜ 待做 |
| 04 | 解析 HTTP/1.1 报文并处理 chunked | [06 · HTTP 与 TLS](../docs/06-http-and-tls.md) | ⬜ 待做 |
| 05 | 用 pcap 文件练过滤器 | [08 · 抓包与协议分析](../docs/08-capture-and-analysis.md) | 🟡 练习数据可用 [lab06](06-zombie-protocol/) 的 `demo.pcapng`（`py mkpcap.py` 现场生成） |
| 06 | 逆向一个自定义长度字段协议 | [09 · 自定义二进制协议](../docs/09-custom-binary-protocol.md) | ✅ 完成（[labs/06-zombie-protocol](06-zombie-protocol/)：真 TCP 靶场 + Wireshark Lua + Ghidra） |

## 工具

| 工具 | 状态 | 用途 |
|:---:|---|---|
| Python 3.11（`py`） | ✅ 已安装 | 所有 lab |
| curl 8.22.0 | ✅ 已安装 | 应用层验证、`-w` 计时 |
| Wireshark / tshark | ✅ 已安装（4.0.17，`E:\Wireshark`） | lab 05/06：过滤、跟随 TCP 流、Lua 解析；Npcap 已装，非管理员实测也能抓 loopback（见 [lab06](06-zombie-protocol/README.md)） |
| MinGW-w64 gcc 14.2 + objdump 2.44 | ✅ 已安装 | lab06 编译靶场、看导入表 |

> 本机 `python` 是 Microsoft Store 占位 stub，运行会失败（`EXIT=9009`）。统一用 `py`。

辅助脚本（不属于 lab，但会被引用）：

- [`tools/make-pcap.py`](../tools/make-pcap.py)：生成 `sample.pcap`（1 个 TCP SYN + 1 个 DNS 查询），Wireshark 已装，可直接打开练过滤器；
- [`tools/frame-demo.py`](../tools/frame-demo.py)：长度字段 + TLV 的组帧/拆帧与粘包处理演示。

## 运行环境检查

```powershell
py --version
curl.exe --version
& 'E:\Wireshark\tshark.exe' --version | Select-Object -First 1
```

三条都能输出，就能跑通 lab 01 和 lab 06（lab 06 还需要 `gcc --version` 能输出）。
