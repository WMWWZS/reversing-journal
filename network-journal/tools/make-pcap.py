"""生成一个练习用的 pcap 文件（纯 Python 3 标准库，不发送任何流量）。

本脚本把 lab 01 里手工构造的字节加上以太网头，写成一个标准 pcap 文件，
供安装 Wireshark / tshark 之后练习显示过滤器：

    py tools/make-pcap.py
    # 生成 sample.pcap：1 个 TCP SYN + 1 个 DNS 查询

装好 Wireshark 后双击 sample.pcap，或：

    tshark -r sample.pcap
    tshark -r sample.pcap -Y "dns.qry.name == \"www.example.com\""
    tshark -r sample.pcap -Y "tcp.flags.syn == 1"

说明：
- 只使用 RFC 5737 文档专用地址（192.0.2.0/24），不指向任何真实主机。
- pcap 里的时间戳是固定值，方便复现；Wireshark 会显示 2025-11-22 左右的相对时间。
- 以太网帧不写 FCS（抓包 API 通常也不包含 FCS），这是 pcap 的常见形态。
"""

from __future__ import annotations

import ipaddress
import struct
import sys

SRC_IP = "192.0.2.10"
DST_IP = "192.0.2.53"
SRC_MAC = bytes.fromhex("020000000001")   # 本地管理地址，02 开头
DST_MAC = bytes.fromhex("020000000002")
ETHERTYPE_IPV4 = 0x0800
PCAP_MAGIC = 0xA1B2C3D4
PCAP_LINKTYPE_ETHERNET = 1
TS_SEC = 1763800000                        # 固定时间戳，便于复现


def checksum(data: bytes) -> int:
    """RFC 1071 反码求和。"""
    if len(data) % 2:
        data += b"\x00"
    total = 0
    for i in range(0, len(data), 2):
        total += (data[i] << 8) | data[i + 1]
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def pseudo_header(proto: int, length: int) -> bytes:
    return (
        ipaddress.IPv4Address(SRC_IP).packed
        + ipaddress.IPv4Address(DST_IP).packed
        + struct.pack("!BBH", 0, proto, length)
    )


def ipv4(protocol: int, payload: bytes, ident: int) -> bytes:
    total = 20 + len(payload)
    header = struct.pack(
        "!BBHHHBBH4s4s",
        0x45,
        0,
        total,
        ident,
        0x4000,          # DF = 1
        64,
        protocol,
        0,
        ipaddress.IPv4Address(SRC_IP).packed,
        ipaddress.IPv4Address(DST_IP).packed,
    )
    csum = checksum(header)
    return header[:10] + struct.pack("!H", csum) + header[12:] + payload


def tcp_syn() -> bytes:
    header = struct.pack(
        "!HHIIBBHHH",
        53000, 80, 0x1A2B3C4D, 0, (5 << 4) | 0, 0x02, 64240, 0, 0,
    )
    csum = checksum(pseudo_header(6, len(header)) + header)
    return header[:16] + struct.pack("!H", csum) + header[18:]


def udp_dns() -> bytes:
    # DNS 查询：www.example.com A IN
    qname = b"\x03www\x07example\x03com\x00"
    dns = struct.pack("!HHHHHH", 0x7A69, 0x0100, 1, 0, 0, 0) + qname + struct.pack("!HH", 1, 1)
    udp = struct.pack("!HHHH", 53000, 53, 8 + len(dns), 0)
    csum = checksum(pseudo_header(17, 8 + len(dns)) + udp + dns)
    if csum == 0:
        csum = 0xFFFF
    udp = udp[:6] + struct.pack("!H", csum) + udp[8:]
    return udp + dns


def ethernet(ethertype: int, payload: bytes) -> bytes:
    frame = DST_MAC + SRC_MAC + struct.pack("!H", ethertype) + payload
    # 最小帧 60 字节（不含 FCS），不足补 0
    if len(frame) < 60:
        frame += b"\x00" * (60 - len(frame))
    return frame


def write_pcap(path: str, frames: list[bytes]) -> int:
    """写 pcap：24 字节全局头 + 每帧 16 字节记录头 + 帧数据。"""
    out = bytearray()
    out += struct.pack("<IHHiIII", PCAP_MAGIC, 2, 4, 0, 0, 65535, PCAP_LINKTYPE_ETHERNET)
    for i, frame in enumerate(frames):
        out += struct.pack("<IIII", TS_SEC + i, 0, len(frame), len(frame))
        out += frame
    with open(path, "wb") as fh:
        fh.write(out)
    return len(out)


def read_pcap(path: str) -> list[bytes]:
    """最小 pcap 解析器，用来验证写出的文件自洽。"""
    with open(path, "rb") as fh:
        data = fh.read()
    magic, vmaj, vmin, _tz, _sig, snaplen, linktype = struct.unpack("<IHHiIII", data[:24])
    assert magic == PCAP_MAGIC, "pcap magic 不对"
    assert (vmaj, vmin) == (2, 4), "版本不对"
    assert linktype == PCAP_LINKTYPE_ETHERNET, "链路类型不是 Ethernet"
    assert snaplen == 65535, "snaplen 不对"
    frames = []
    pos = 24
    while pos < len(data):
        _s, _us, incl, orig = struct.unpack("<IIII", data[pos : pos + 16])
        assert incl == orig, "抓取长度与原始长度不一致"
        frame = data[pos + 16 : pos + 16 + incl]
        assert len(frame) == incl, "帧数据被截断"
        frames.append(frame)
        pos += 16 + incl
    assert pos == len(data), "尾部有多余字节"
    return frames


def main() -> int:
    syn = tcp_syn()
    dns = udp_dns()
    frames = [
        ethernet(ETHERTYPE_IPV4, ipv4(6, syn, 0x1B2D)),
        ethernet(ETHERTYPE_IPV4, ipv4(17, dns, 0x1B2E)),
    ]

    out = "sample.pcap"
    size = write_pcap(out, frames)

    print("=" * 66)
    print("生成练习 pcap")
    print("=" * 66)
    print(f"文件      : {out}")
    print(f"大小      : {size} 字节")
    print(f"帧数      : {len(frames)}")
    print(f"链路类型  : {PCAP_LINKTYPE_ETHERNET}（Ethernet）")
    print(f"TCP SYN   : 以太网帧 {len(frames[0])} 字节，"
          f"IPv4 total_length={struct.unpack('!H', frames[0][16:18])[0]}")
    print(f"DNS 查询  : 以太网帧 {len(frames[1])} 字节，"
          f"IPv4 total_length={struct.unpack('!H', frames[1][16:18])[0]}")

    # 自测：读回自己写的文件并逐帧校验
    frames_back = read_pcap(out)
    assert len(frames_back) == len(frames), "读回的帧数不一致"
    for idx, (a, b) in enumerate(zip(frames, frames_back)):
        assert a == b, f"第 {idx} 帧内容不一致"
        ip_total = struct.unpack("!H", b[16:18])[0]
        padding = len(b) - 14 - ip_total
        assert padding >= 0, f"第 {idx} 帧 IP 总长超过帧长"
        assert b[14 + ip_total:] == b"\x00" * padding, f"第 {idx} 帧补齐字节不是 0"
        assert checksum(b[14:34]) == 0, f"第 {idx} 帧 IP 头校验和不为 0"
    print()
    print(f"[ok] 读回 {len(frames_back)} 帧，内容与写入一致")
    print("[ok] 每帧 IPv4 total_length <= 帧长 - 14，差额是以太网最小帧补齐的 0 字节")
    print("[ok] 每帧 IPv4 头校验和重算 == 0x0000")
    print()
    print("装好 Wireshark 后：")
    print(f"  tshark -r {out} -Y \"dns.qry.name == \\\"www.example.com\\\"\"")
    print(f"  tshark -r {out} -Y \"tcp.flags.syn == 1\"")
    return 0


if __name__ == "__main__":
    sys.exit(main())
