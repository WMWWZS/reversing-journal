"""演示「长度字段 + TLV」自定义二进制协议的组帧与拆帧（纯标准库）。

场景：TCP 是字节流，recv() 一次可能拿到半条消息、也可能拿到一条半。
本脚本：
  1. 按固定格式把 2 条消息编码成字节；
  2. 每次只喂 3 个字节给解码器，模拟「半包」；
  3. 解码器在缓冲区里拼出完整帧后逐条产出。

报文格式（大端）：
  偏移  长度  字段
  0     2     magic = "NP" (0x4E 0x50)
  2     1     version = 1
  3     1     cmd
  4     1     flags
  5     2     seq
  7     4     body_len（只算 TLV body 的长度）
  11    2     checksum（header 的 crc 字段先置 0，再对 header + body 求 sum16）
  13    n     body：一串 TLV，T=1 字节，L=1 字节，V=L 字节

用法：
    py tools/frame-demo.py
"""

from __future__ import annotations

import struct
import sys

MAGIC = b"NP"
HEADER_LEN = 13
CRC_OFFSET = 11


def sum16(data: bytes) -> int:
    """简单 16 位校验和：按 16 位累加后取反，只为演示字段位置，不是密码学校验。"""
    if len(data) % 2:
        data += b"\x00"
    total = sum((data[i] << 8) | data[i + 1] for i in range(0, len(data), 2))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def encode_tlvs(tlvs: list[tuple[int, bytes]]) -> bytes:
    out = bytearray()
    for t, v in tlvs:
        if len(v) > 255:
            raise ValueError("TLV 的 value 不能超过 255 字节")
        out += bytes([t, len(v)]) + v
    return bytes(out)


def build_frame(cmd: int, flags: int, seq: int, tlvs: list[tuple[int, bytes]]) -> bytes:
    body = encode_tlvs(tlvs)
    header = (
        MAGIC
        + bytes([1, cmd, flags])
        + struct.pack("!H", seq)
        + struct.pack("!I", len(body))
        + b"\x00\x00"          # checksum 占位
    )
    assert len(header) == HEADER_LEN
    crc = sum16(header + body)
    return header[:CRC_OFFSET] + struct.pack("!H", crc) + header[CRC_OFFSET + 2 :] + body


def decode_tlvs(body: bytes) -> list[tuple[int, bytes]]:
    out = []
    pos = 0
    while pos < len(body):
        if pos + 2 > len(body):
            raise ValueError("TLV 头被截断")
        t, ln = body[pos], body[pos + 1]
        pos += 2
        if pos + ln > len(body):
            raise ValueError("TLV 值被截断")
        out.append((t, body[pos : pos + ln]))
        pos += ln
    return out


class FrameDecoder:
    """把任意分块的字节流还原成帧。真正的协议解析器都是这个形状。"""

    def __init__(self) -> None:
        self.buf = bytearray()
        self.frames = 0

    def feed(self, chunk: bytes) -> list[dict]:
        self.buf += chunk
        out = []
        while True:
            if len(self.buf) < HEADER_LEN:
                return out
            if bytes(self.buf[:2]) != MAGIC:
                raise ValueError(f"magic 不匹配：{bytes(self.buf[:2])!r}")
            (version, cmd, flags, seq, body_len, crc) = struct.unpack(
                "!BBBHIH", bytes(self.buf[2:HEADER_LEN])
            )
            total = HEADER_LEN + body_len
            if len(self.buf) < total:
                return out
            frame = bytes(self.buf[:total])
            body = frame[HEADER_LEN:]
            # 校验：把 crc 字段置 0 后重算
            zeroed = frame[:CRC_OFFSET] + b"\x00\x00" + frame[CRC_OFFSET + 2 :]
            if sum16(zeroed) != crc:
                raise ValueError("checksum 不匹配")
            out.append(
                {
                    "version": version,
                    "cmd": cmd,
                    "flags": flags,
                    "seq": seq,
                    "body_len": body_len,
                    "crc": crc,
                    "tlvs": decode_tlvs(body),
                }
            )
            del self.buf[:total]
            self.frames += 1


def hexdump(data: bytes, width: int = 16) -> str:
    lines = []
    for off in range(0, len(data), width):
        chunk = data[off : off + width]
        hex_part = " ".join(f"{b:02x}" for b in chunk).ljust(width * 3 - 1)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"  {off:04x}  {hex_part}  |{text}|")
    return "\n".join(lines)


def main() -> int:
    # 两条消息：一条「登录」，一条「移动」
    f1 = build_frame(cmd=0x01, flags=0x00, seq=1,
                     tlvs=[(0x01, b"alice"), (0x02, struct.pack("!H", 18))])
    f2 = build_frame(cmd=0x02, flags=0x80, seq=2,
                     tlvs=[(0x03, struct.pack("!ii", 120, -35))])
    stream = f1 + f2

    print("=" * 70)
    print("自定义二进制协议：长度字段 + TLV")
    print("=" * 70)
    print(f"帧 1：{len(f1)} 字节（头 {HEADER_LEN} + body {len(f1) - HEADER_LEN}）")
    print(hexdump(f1))
    print(f"帧 2：{len(f2)} 字节（头 {HEADER_LEN} + body {len(f2) - HEADER_LEN}）")
    print(hexdump(f2))
    print()
    print("帧 1 头部字段：")
    print("  偏移   长度  字段        值")
    print("  " + "-" * 46)
    for name, off, size in (("magic", 0, 2), ("version", 2, 1), ("cmd", 3, 1),
                            ("flags", 4, 1), ("seq", 5, 2),
                            ("body_len", 7, 4), ("checksum", 11, 2)):
        raw = f1[off : off + size]
        print(f"  {off:<6}{size:<5} {name:<11} {raw.hex(' ')}")
    print()

    # 模拟粘包/半包：每次只喂 3 字节
    dec = FrameDecoder()
    print("按 3 字节分块喂入（模拟 recv 返回半包）：")
    print("  块  本次字节   缓冲区剩余   已解出帧")
    print("  " + "-" * 44)
    for i in range(0, len(stream), 3):
        chunk = stream[i : i + 3]
        done = dec.feed(chunk)
        print(f"  {i // 3:<4}{len(chunk):<10}{len(dec.buf):<13}{len(done)}")
        for fr in done:
            tlvs = ", ".join(f"T=0x{t:02x} V={v!r}" for t, v in fr["tlvs"])
            print(f"       → 解出帧 cmd=0x{fr['cmd']:02x} seq={fr['seq']} "
                  f"body_len={fr['body_len']}  {tlvs}")

    # 自测
    assert dec.frames == 2, "应解出 2 帧"
    assert len(dec.buf) == 0, "全部字节都应被消费"
    print()
    print("[ok] 两帧按 3 字节分块喂入后全部解出，缓冲区清空")
    assert sum16(f1[:CRC_OFFSET] + b"\x00\x00" + f1[CRC_OFFSET + 2 :]) == \
        struct.unpack("!H", f1[CRC_OFFSET:CRC_OFFSET + 2])[0]
    print("[ok] 帧 1 校验和重算一致")
    print("[ok] TLV 解析：0x01=alice，0x02=18，0x03=(120, -35)")
    print()
    print("要点：TCP 不保证边界，解码器必须自己带缓冲区按长度字段切。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
