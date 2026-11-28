"""Lab 01 · 手工构造 IPv4 / TCP / UDP / DNS 报文字节

目的：不依赖任何抓包库，只用 struct / ipaddress / socket 把报文的
每一个字节亲手拼出来，再写解析函数把字节读回成字段字典。
只有亲手拼过一次，看 Wireshark 里那些十六进制才算「认得」。

本脚本不发送任何数据包：只用 RFC 5737 的文档专用地址（TEST-NET-1）
在内存里构造字节。运行不会产生任何网络流量。

用法：
    py craft.py
    # 或者：python craft.py（若本机 python 是 Microsoft Store 占位 stub，
    #                       会直接失败且没有输出，此时必须用 py）
"""

from __future__ import annotations

import ipaddress
import socket
import struct
import sys
import unicodedata

# ---------------------------------------------------------------------------
# 常量：全部使用 RFC 5737 文档专用地址，保证不会指向真实主机
# ---------------------------------------------------------------------------
SRC_IP = "192.0.2.10"      # TEST-NET-1，模拟客户端
DST_IP = "192.0.2.53"      # TEST-NET-1，模拟解析器
SRC_PORT = 53000
DST_PORT_DNS = 53
DST_PORT_HTTP = 80

IPPROTO_TCP = 6
IPPROTO_UDP = 17

# 一个 TCP 段要用的序列号（任意值，只要能对上）
TCP_SEQ = 0x1A2B3C4D
TCP_ACK = 0x00000000
TCP_FLAG_SYN = 0x02
TCP_WINDOW = 64240


# ---------------------------------------------------------------------------
# 基础工具
# ---------------------------------------------------------------------------
def display_width(text: str) -> int:
    """中文字符占 2 列，用它算对齐宽度。"""
    return sum(2 if unicodedata.east_asian_width(ch) in "WF" else 1 for ch in text)


def ljust(text: str, width: int) -> str:
    return text + " " * max(0, width - display_width(text))


def rjust(text: str, width: int) -> str:
    return " " * max(0, width - display_width(text)) + text


def checksum(data: bytes) -> int:
    """RFC 1071 反码求和校验和：16 位一组求和，进位回卷，最后取反。"""
    if len(data) % 2:
        data += b"\x00"
    total = 0
    for i in range(0, len(data), 2):
        total += (data[i] << 8) | data[i + 1]
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def pseudo_header(src: str, dst: str, protocol: int, length: int) -> bytes:
    """IP 伪首部：只参与 TCP/UDP 校验和计算，不会真的出现在报文里。

    结构（IPv4）：源 IP(4) + 目的 IP(4) + 保留 0(1) + 协议(1) + 长度(2)
    """
    return (
        ipaddress.IPv4Address(src).packed
        + ipaddress.IPv4Address(dst).packed
        + struct.pack("!BBH", 0, protocol, length)
    )


def hexdump(data: bytes, base: int = 0, width: int = 16) -> str:
    """把字节打印成「偏移 + 十六进制 + ASCII」三栏对齐的格式。"""
    rows = []
    for off in range(0, len(data), width):
        chunk = data[off : off + width]
        hex_part = " ".join(f"{b:02x}" for b in chunk)
        hex_part = hex_part.ljust(width * 3 - 1)
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        rows.append(f"  {base + off:04x}  {hex_part}  |{ascii_part}|")
    return "\n".join(rows)


def print_field_table(title: str, rows: list[tuple]) -> None:
    """打印字段表：字段名 / 字节偏移 / 字段长度 / 原始值 / 解析值。"""
    print(f"  {title}")
    print("  " + "-" * 74)
    print(f"  {ljust('字段', 16)}{rjust('偏移', 12)}{rjust('长度', 10)}  "
          f"{ljust('原始值(hex)', 20)}解析值")
    for name, off, size, raw, value in rows:
        raw = raw if len(raw) <= 20 else raw[:17] + "..."
        print(f"  {ljust(name, 16)}{rjust(str(off), 12)}{rjust(str(size), 10)}  "
              f"{ljust(raw, 20)}{value}")


# ---------------------------------------------------------------------------
# 构造：逐字节按协议格式拼
# ---------------------------------------------------------------------------
def build_ipv4_header(
    src: str,
    dst: str,
    protocol: int,
    payload_len: int,
    identification: int = 0x1B2D,
    ttl: int = 64,
    tos: int = 0,
) -> tuple[bytes, int, int]:
    """构造 IPv4 头（无选项，IHL=5，共 20 字节），返回 (头字节, 校验和, 总长度)。

    IPv4 头布局：
      0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |Version|  IHL  |Type of Service|          Total Length         |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |         Identification        |Flags|      Fragment Offset    |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |  Time to Live |    Protocol   |         Header Checksum       |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                       Source Address                          |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                    Destination Address                        |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    """
    ihl = 5
    total_len = ihl * 4 + payload_len
    # flags=0b010（DF，不分片），fragment offset=0 → 组装成 0x4000
    flags_frag = 0x4000
    header = struct.pack(
        "!BBHHHBBH4s4s",
        (4 << 4) | ihl,                      # Version=4, IHL=5
        tos,
        total_len,
        identification,
        flags_frag,
        ttl,
        protocol,
        0,                                   # 校验和先填 0
        ipaddress.IPv4Address(src).packed,
        ipaddress.IPv4Address(dst).packed,
    )
    csum = checksum(header)
    # 校验和位于偏移 10，占 2 字节
    header = header[:10] + struct.pack("!H", csum) + header[12:]
    return header, csum, total_len


def build_tcp_header(
    src: str,
    dst: str,
    sport: int,
    dport: int,
    seq: int,
    ack: int,
    flags: int,
    window: int,
    payload: bytes = b"",
) -> tuple[bytes, int]:
    """构造 TCP 头（数据偏移 5，无选项，共 20 字节），返回 (段字节, 校验和)。"""
    data_offset = 5
    tcp = struct.pack(
        "!HHIIBBHHH",
        sport,
        dport,
        seq,
        ack,
        (data_offset << 4) | 0,   # 高 4 位是数据偏移，低 4 位保留
        flags,
        window,
        0,                        # 校验和先填 0
        0,                        # 紧急指针
    )
    length = len(tcp) + len(payload)
    csum = checksum(pseudo_header(src, dst, IPPROTO_TCP, length) + tcp + payload)
    tcp = tcp[:16] + struct.pack("!H", csum) + tcp[18:]
    return tcp + payload, csum


def build_udp_header(
    src: str, dst: str, sport: int, dport: int, payload: bytes
) -> tuple[bytes, int]:
    """构造 UDP 头（8 字节）+ 载荷，返回 (数据报字节, 校验和)。"""
    length = 8 + len(payload)
    udp = struct.pack("!HHHH", sport, dport, length, 0)
    csum = checksum(pseudo_header(src, dst, IPPROTO_UDP, length) + udp + payload)
    # RFC 768：UDP 校验和算出 0 时必须发送 0xFFFF，因为 0 表示「未计算」
    if csum == 0:
        csum = 0xFFFF
    udp = udp[:6] + struct.pack("!H", csum) + udp[8:]
    return udp + payload, csum


# ---------------------------------------------------------------------------
# DNS：QNAME 编码 / 解码 + 查询报文
# ---------------------------------------------------------------------------
def encode_qname(name: str) -> bytes:
    """把域名编码成 DNS 的 QNAME 格式：每个标签前加 1 字节长度，末字节 0。

    www.example.com → 03 'www' 07 'example' 03 'com' 00
    """
    out = bytearray()
    for label in name.rstrip(".").split("."):
        if not label:
            raise ValueError("域名为空或含连续点")
        raw = label.encode("ascii")
        if len(raw) > 63:
            raise ValueError(f"标签超长（{len(raw)} > 63）：{label}")
        out += bytes([len(raw)]) + raw
    out += b"\x00"
    if len(out) > 255:
        raise ValueError("QNAME 超过 255 字节")
    return bytes(out)


def decode_qname(data: bytes, offset: int = 0) -> tuple[str, int]:
    """从 offset 开始解码 QNAME，返回 (域名, 结束后的下一个偏移)。

    本 lab 只处理未压缩的名字；压缩指针（高两位为 11）直接报错，
    因为一个真正正确的解码器必须能识别指针，这里故意留成显式失败。
    """
    labels = []
    pos = offset
    while True:
        if pos >= len(data):
            raise ValueError("QNAME 越界：缺少结束的 0 字节")
        n = data[pos]
        if n == 0:
            pos += 1
            break
        if n & 0xC0:
            raise ValueError("遇到 DNS 压缩指针，本 lab 未实现")
        labels.append(data[pos + 1 : pos + 1 + n].decode("ascii"))
        pos += 1 + n
    return ".".join(labels), pos


def build_dns_query(name: str, qtype: int = 1, qclass: int = 1,
                    dns_id: int = 0x7A69) -> bytes:
    """构造一个标准 DNS 查询报文（QR=0，RD=1）。"""
    # flags: QR=0, Opcode=0, AA=0, TC=0, RD=1 → 0x0100
    flags = 0x0100
    header = struct.pack("!HHHHHH", dns_id, flags, 1, 0, 0, 0)
    question = encode_qname(name) + struct.pack("!HH", qtype, qclass)
    return header + question


# ---------------------------------------------------------------------------
# 解析：把字节读回成字段字典
# ---------------------------------------------------------------------------
def parse_ipv4(buf: bytes) -> dict:
    if len(buf) < 20:
        raise ValueError("IPv4 头不足 20 字节")
    (vihl, tos, total_len, ident, flags_frag, ttl,
     protocol, csum, src, dst) = struct.unpack("!BBHHHBBH4s4s", buf[:20])
    ihl = vihl & 0x0F
    return {
        "version": vihl >> 4,
        "ihl": ihl,
        "header_len": ihl * 4,
        "tos": tos,
        "total_length": total_len,
        "identification": ident,
        "flags": (flags_frag >> 13) & 0x07,
        "fragment_offset": flags_frag & 0x1FFF,
        "ttl": ttl,
        "protocol": protocol,
        "checksum": csum,
        "src": str(ipaddress.IPv4Address(src)),
        "dst": str(ipaddress.IPv4Address(dst)),
        "payload": buf[ihl * 4 :],
    }


def parse_tcp(buf: bytes) -> dict:
    (sport, dport, seq, ack, off_byte, flag_byte, window,
     csum, urgent) = struct.unpack("!HHIIBBHHH", buf[:20])
    data_offset = off_byte >> 4
    return {
        "src_port": sport,
        "dst_port": dport,
        "seq": seq,
        "ack": ack,
        "data_offset": data_offset,
        "header_len": data_offset * 4,
        "flags_raw": flag_byte,
        "flags_syn": bool(flag_byte & 0x02),
        "flags_ack": bool(flag_byte & 0x10),
        "window": window,
        "checksum": csum,
        "urgent": urgent,
        "payload": buf[data_offset * 4 :],
    }


def parse_udp(buf: bytes) -> dict:
    sport, dport, length, csum = struct.unpack("!HHHH", buf[:8])
    if length < 8:
        raise ValueError("UDP length 字段小于 8，报文非法")
    return {
        "src_port": sport,
        "dst_port": dport,
        "length": length,
        "checksum": csum,
        "payload": buf[8:length],
    }


def parse_dns(buf: bytes) -> dict:
    dns_id, flags = struct.unpack("!HH", buf[:4])
    qd, an, ns, ar = struct.unpack("!HHHH", buf[4:12])
    qname, pos = decode_qname(buf, 12)
    qtype, qclass = struct.unpack("!HH", buf[pos : pos + 4])
    return {
        "id": dns_id,
        "flags": flags,
        "rd": bool(flags & 0x0100),
        "qdcount": qd,
        "ancount": an,
        "nscount": ns,
        "arcount": ar,
        "qname": qname,
        "qtype": qtype,
        "qclass": qclass,
        "question_len": pos + 4 - 12,
    }


# ---------------------------------------------------------------------------
# 自测：全部用 assert，失败会直接抛异常
# ---------------------------------------------------------------------------
def run_selftests() -> None:
    print("=" * 74)
    print("自测（assert，全部通过才继续）")
    print("=" * 74)

    # 1) 校验和重算：把校验和字段一起参与计算，结果必须为 0
    assert checksum(ip_header) == 0, "IPv4 头校验和重算不为 0"
    print("[ok] IPv4 头整体重算校验和 == 0x0000")

    # 2) 长度字段自洽
    assert ip_total_len == 20 + tcp_len, "IPv4 total_length 与 TCP 段长度不匹配"
    assert udp_total_len == 8 + len(dns_query), "UDP length 与 DNS 查询长度不匹配"
    print(f"[ok] 长度自洽：IPv4 total_length={ip_total_len}，UDP length={udp_total_len}")

    # 3) 解析回读与构造时的输入一致
    p = parse_ipv4(ip_header)
    assert (p["version"], p["ihl"], p["total_length"], p["protocol"], p["src"], p["dst"]) == (
        4, 5, ip_total_len, IPPROTO_TCP, SRC_IP, DST_IP,
    ), "IPv4 字段回读不一致"
    t = parse_tcp(tcp_seg)
    assert (t["src_port"], t["dst_port"], t["seq"], t["flags_syn"]) == (
        SRC_PORT, DST_PORT_HTTP, TCP_SEQ, True,
    ), "TCP 字段回读不一致"
    u = parse_udp(udp_dgram)
    assert (u["src_port"], u["dst_port"]) == (SRC_PORT, DST_PORT_DNS), "UDP 字段回读不一致"
    print("[ok] IPv4 / TCP / UDP 字段回读与构造输入一致")

    # 4) TCP / UDP 伪首部校验和重算
    assert checksum(pseudo_header(SRC_IP, DST_IP, IPPROTO_TCP, tcp_len) + tcp_seg) == 0, \
        "TCP 校验和重算不为 0"
    assert checksum(pseudo_header(SRC_IP, DST_IP, IPPROTO_UDP, udp_total_len) + udp_dgram) == 0, \
        "UDP 校验和重算不为 0"
    print("[ok] TCP / UDP 伪首部校验和重算 == 0x0000")

    # 5) QNAME 编解码往返
    for name in ("www.example.com", "a.io", "deep.sub.example.org"):
        assert decode_qname(encode_qname(name))[0] == name, f"QNAME 往返失败：{name}"
    print("[ok] QNAME 编码 → 解码往返一致（3 个域名）")

    # 6) DNS 查询解析
    d = parse_dns(dns_query)
    assert (d["qname"], d["qtype"], d["qclass"], d["qdcount"]) == (
        "www.example.com", 1, 1, 1,
    ), "DNS 查询字段解析不一致"
    print("[ok] DNS 查询解析：qname=www.example.com qtype=1(A) qclass=1(IN)")

    # 7) socket 与 ipaddress 的 IP 打包结果必须一致（两种标准库写法互校）
    assert socket.inet_aton(SRC_IP) == ipaddress.IPv4Address(SRC_IP).packed, "IP 打包不一致"
    print("[ok] socket.inet_aton 与 ipaddress 打包结果一致")

    print()
    print("全部自测通过。")


def main() -> int:
    global ip_header, ip_checksum, ip_total_len
    global tcp_seg, tcp_checksum, tcp_len
    global udp_dgram, udp_checksum, udp_total_len
    global dns_query

    print("=" * 74)
    print("Lab 01 · 手工构造报文字节")
    print(f"源 {SRC_IP}  →  目的 {DST_IP}   （RFC 5737 文档专用地址，不发送任何数据）")
    print("=" * 74)

    # ---- 1. DNS 查询报文（先造载荷，后面 UDP 才能算长度和校验和）----
    dns_query = build_dns_query("www.example.com", qtype=1, qclass=1)
    qname_bytes = encode_qname("www.example.com")
    d = parse_dns(dns_query)
    print()
    print("【1】DNS 查询报文 —— QNAME 编码后的字节")
    print(f"  www.example.com 编码为 {len(qname_bytes)} 字节：{qname_bytes.hex(' ')}")
    print(hexdump(dns_query))
    print()
    print_field_table(
        "DNS 报文字段",
        [
            ("id", "0", "2", f"0x{dns_query[0:2].hex()}", f"0x{d['id']:04x}"),
            ("flags", "2", "2", f"0x{dns_query[2:4].hex()}", "QR=0 RD=1"),
            ("qdcount", "4", "2", f"0x{dns_query[4:6].hex()}", "1"),
            ("ancount", "6", "2", f"0x{dns_query[6:8].hex()}", "0"),
            ("qname", "12", str(len(qname_bytes)), qname_bytes.hex(" "), "www.example.com"),
            ("qtype", str(12 + len(qname_bytes)), "2",
             f"0x{dns_query[12 + len(qname_bytes):14 + len(qname_bytes)].hex()}", "1 (A)"),
            ("qclass", str(14 + len(qname_bytes)), "2",
             f"0x{dns_query[14 + len(qname_bytes):16 + len(qname_bytes)].hex()}", "1 (IN)"),
        ],
    )

    # ---- 2. UDP 头 + DNS 负载 ----
    udp_dgram, udp_checksum = build_udp_header(
        SRC_IP, DST_IP, SRC_PORT, DST_PORT_DNS, dns_query
    )
    udp_total_len = len(udp_dgram)
    print()
    print("【2】UDP 头（8 字节）+ DNS 查询")
    print(hexdump(udp_dgram))
    print()
    print_field_table(
        "UDP 字段",
        [
            ("src_port", "0", "2", f"0x{udp_dgram[0:2].hex()}", str(SRC_PORT)),
            ("dst_port", "2", "2", f"0x{udp_dgram[2:4].hex()}", str(DST_PORT_DNS)),
            ("length", "4", "2", f"0x{udp_dgram[4:6].hex()}",
             f"{udp_total_len}（8 + {len(dns_query)}）"),
            ("checksum", "6", "2", f"0x{udp_dgram[6:8].hex()}",
             f"0x{udp_checksum:04x}（含伪首部）"),
        ],
    )

    # ---- 3. TCP SYN 段 ----
    tcp_seg, tcp_checksum = build_tcp_header(
        SRC_IP, DST_IP, SRC_PORT, DST_PORT_HTTP,
        TCP_SEQ, TCP_ACK, TCP_FLAG_SYN, TCP_WINDOW,
    )
    tcp_len = len(tcp_seg)
    print()
    print("【3】TCP SYN 段（20 字节，无选项）")
    print(hexdump(tcp_seg))
    print()
    print_field_table(
        "TCP 字段",
        [
            ("src_port", "0", "2", f"0x{tcp_seg[0:2].hex()}", str(SRC_PORT)),
            ("dst_port", "2", "2", f"0x{tcp_seg[2:4].hex()}", str(DST_PORT_HTTP)),
            ("seq", "4", "4", f"0x{tcp_seg[4:8].hex()}", f"0x{TCP_SEQ:08x}"),
            ("ack", "8", "4", f"0x{tcp_seg[8:12].hex()}", "0x00000000"),
            ("data_offset", "12", "1", f"0x{tcp_seg[12]:02x}", "5（头长 20 字节）"),
            ("flags", "13", "1", f"0x{tcp_seg[13]:02x}", "SYN=1"),
            ("window", "14", "2", f"0x{tcp_seg[14:16].hex()}", str(TCP_WINDOW)),
            ("checksum", "16", "2", f"0x{tcp_seg[16:18].hex()}",
             f"0x{tcp_checksum:04x}（含伪首部）"),
            ("urgent", "18", "2", f"0x{tcp_seg[18:20].hex()}", "0"),
        ],
    )

    # ---- 4. IPv4 头（包住 TCP 段）----
    ip_header, ip_checksum, ip_total_len = build_ipv4_header(
        SRC_IP, DST_IP, IPPROTO_TCP, tcp_len
    )
    p = parse_ipv4(ip_header)
    print()
    print("【4】IPv4 头（20 字节，IHL=5，DF=1）")
    print(hexdump(ip_header))
    print()
    print_field_table(
        "IPv4 字段",
        [
            ("version", "0（bit 4-7）", "4 bit", f"0x{ip_header[0]:02x}", "4"),
            ("ihl", "0（bit 0-3）", "4 bit", f"0x{ip_header[0]:02x}", "5（头长 20 字节）"),
            ("tos", "1", "1", f"0x{ip_header[1]:02x}", "0"),
            ("total_length", "2", "2", f"0x{ip_header[2:4].hex()}", str(ip_total_len)),
            ("identification", "4", "2", f"0x{ip_header[4:6].hex()}",
             f"0x{p['identification']:04x}"),
            ("flags", "6（bit 5-7）", "3 bit", f"0x{ip_header[6]:02x}",
             "DF=1（0x4000 → 不允许分片）"),
            ("fragment_offset", "6（bit 0-4）", "13 bit", f"0x{ip_header[6:8].hex()}", "0"),
            ("ttl", "8", "1", f"0x{ip_header[8]:02x}", "64"),
            ("protocol", "9", "1", f"0x{ip_header[9]:02x}", "6（TCP）"),
            ("checksum", "10", "2", f"0x{ip_header[10:12].hex()}", f"0x{ip_checksum:04x}"),
            ("src", "12", "4", ip_header[12:16].hex(), SRC_IP),
            ("dst", "16", "4", ip_header[16:20].hex(), DST_IP),
        ],
    )

    # ---- 5. 组装后的完整 IP 包 ----
    ip_packet = ip_header + tcp_seg
    print()
    print(f"【5】完整 IPv4 包（头 + TCP SYN），共 {len(ip_packet)} 字节")
    print(hexdump(ip_packet))
    print()
    print(f"  一句话读法：{SRC_IP}:{SRC_PORT} → {DST_IP}:{DST_PORT_HTTP}，"
          f"SYN，seq=0x{TCP_SEQ:08x}，窗口 {TCP_WINDOW}")

    # ---- 6. 小结表 ----
    print()
    print("=" * 74)
    print("小结：三个报文的长度关系")
    print("=" * 74)
    print(f"  {ljust('报文', 24)}{rjust('长度(字节)', 12)}  说明")
    print("  " + "-" * 70)
    print(f"  {ljust('IPv4 头', 24)}{rjust(str(len(ip_header)), 12)}  IHL=5，固定 20 字节")
    print(f"  {ljust('TCP 头', 24)}{rjust(str(len(tcp_seg)), 12)}  无选项，固定 20 字节")
    print(f"  {ljust('IPv4 total_length', 24)}{rjust(str(ip_total_len), 12)}  20 + 20，不算链路层")
    print(f"  {ljust('UDP 头', 24)}{rjust('8', 12)}  固定 8 字节")
    print(f"  {ljust('UDP length', 24)}{rjust(str(udp_total_len), 12)}  8 + {len(dns_query)}（含 DNS 查询）")
    print(f"  {ljust('DNS 查询', 24)}{rjust(str(len(dns_query)), 12)}  12 字节头 + QNAME + 4 字节问题")

    run_selftests()
    print()
    print("提示：把这个包里 IPv4 的 protocol 改成 17，TCP 段换成上面的 UDP 数据报，")
    print("      就是一台主机发 DNS 查询时网卡上出现的全部字节（链路层除外）。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
