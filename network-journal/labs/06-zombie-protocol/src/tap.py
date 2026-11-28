"""
tap.py - ZOMBIE 协议中间人（lab 里那个“tap”）

它做的事只有三件：
  1. 监听 127.0.0.1:47000；有客户端连上来，就去连 127.0.0.1:47001（真正的服务端）
  2. 双向原样转发字节；每次 recv() 到一块就打一行：时间、方向、长度、前 32 字节 hex
  3. 顺手按协议把帧拆出来，再打一行注解 —— 这样 24 字节的 HP_SYNC 在线上长什么样，
     不用 Wireshark 也看得见

--hack-claimed X：这就是“改包”。C>S 方向的 ATTACK 帧里 claimed_hp 那 4 个字节
被改写成 X，帧长度不变（还是 16 字节），然后才转发给服务端。
用来演示 --trust-client 的漏洞：服务端如果信这个字段，血就由客户端说了算。
协议本身一个字节都没改，改的是“路上经过的包”。

只用标准库。启动方式：
  <python> -u tap.py --listen-port 47000 --target-port 47001

端口为什么是 47000/47001：这台机器上 4000/4001 那对端口里，127.0.0.1:4001
被 QQ.exe 占着，服务端 bind 会拿到 WSA error 10013（见 NOTES.md）。
"""
import argparse
import os
import socket
import struct
import sys
import threading
import time

T0 = time.perf_counter()

OPNAMES = {1: "HELLO", 2: "ATTACK", 3: "HP_SYNC", 4: "SNAPSHOT", 5: "BYE"}

LOCK = threading.Lock()


def log(msg):
    line = "[+%7.3fs] %s" % (time.perf_counter() - T0, msg)
    with LOCK:
        sys.stdout.write(line + "\n")
        sys.stdout.flush()


def errtext(exc):
    """只打数字错误码：Windows 的 OSError 文本是本地化的（GBK 中文），混进日志就乱了。"""
    if isinstance(exc, OSError):
        return "winerror=%s errno=%s" % (getattr(exc, "winerror", None), exc.errno)
    return type(exc).__name__


def hexdump(data, n):
    return " ".join("%02x" % b for b in data[:n])


class Annotator:
    """把某一方向的字节流按 total_len 切成帧，每帧打一行注解。"""

    def __init__(self, tag, hex_bytes=32):
        self.tag = tag
        self.buf = b""
        self.hex_bytes = hex_bytes
        self.frames = 0

    def feed(self, data):
        self.buf += data
        while len(self.buf) >= 4:
            total = struct.unpack_from("<H", self.buf, 0)[0]
            if total < 2 or total > 65533:
                log("%s BAD frame header total_len=%d -> drop %d buffered byte(s)"
                    % (self.tag, total, len(self.buf)))
                self.buf = b""
                return
            need = 2 + total
            if len(self.buf) < need:
                return                      # 半帧，等更多字节
            frame = self.buf[:need]
            self.buf = self.buf[need:]
            self.frames += 1
            log("%s frame #%-2d %5dB  %s  hex=%s"
                % (self.tag, self.frames, len(frame), self.describe(frame),
                   hexdump(frame, self.hex_bytes)))
            if frame[3] != 0:
                log("%s WARN flags=%d != 0" % (self.tag, frame[3]))

    def describe(self, f):
        op = f[2]
        pl = f[4:]
        name = OPNAMES.get(op, "opcode 0x%02x" % op)
        if op == 1 and len(pl) >= 8:
            cid, ver = struct.unpack_from("<II", pl, 0)
            return "%s client_id=%d version=%d" % (name, cid, ver)
        if op == 2 and len(pl) >= 12:
            dmg, seq, claimed = struct.unpack_from("<fIf", pl, 0)
            return "%s dmg=%.2f seq=%d claimed=%.2f" % (name, dmg, seq, claimed)
        if op == 3 and len(pl) >= 20:
            pid, hp, mhp, tick = struct.unpack_from("<IffI", pl, 0)
            alive = pl[16]
            return "%s player=%d hp=%.2f max=%.2f tick=%d alive=%d" % (
                name, pid, hp, mhp, tick, alive)
        if op == 4 and len(pl) >= 2:
            count = struct.unpack_from("<H", pl, 0)[0]
            return "%s count=%d payload=%d" % (name, count, len(pl))
        return name


class ClaimHack:
    """把 C>S 方向 ATTACK 帧里的 claimed_hp 改掉，长度不变。"""

    def __init__(self, value):
        self.value = value
        self.buf = b""

    def push(self, data):
        out = bytearray()
        self.buf += data
        while len(self.buf) >= 4:
            total = struct.unpack_from("<H", self.buf, 0)[0]
            if total < 2 or total > 65533:
                log("HACK BAD frame header total_len=%d -> pass the rest through" % total)
                out += self.buf
                self.buf = b""
                break
            need = 2 + total
            if len(self.buf) < need:
                break                       # 半帧，攒够了再改
            frame = self.buf[:need]
            self.buf = self.buf[need:]
            if frame[2] == 2 and len(frame) >= 16:
                seq = struct.unpack_from("<I", frame, 8)[0]
                old = struct.unpack_from("<f", frame, 12)[0]
                newf = bytearray(frame)
                struct.pack_into("<f", newf, 12, self.value)
                log("HACK C>S ATTACK seq=%d: claimed_hp %.2f -> %.2f  "
                    "(frame stays %dB, only payload bytes 12..15 changed)"
                    % (seq, old, self.value, len(frame)))
                out += newf
            else:
                out += frame
        return bytes(out)

    def flush(self):
        tail = self.buf
        self.buf = b""
        return tail


def send_all(sock, data):
    view = memoryview(data)
    while view:
        n = sock.send(view)
        if n <= 0:
            raise ConnectionError("send() returned 0")
        view = view[n:]


def pump_plain(name, src, dst, ann, hex_bytes, stats):
    while True:
        try:
            data = src.recv(65536)
        except OSError as exc:
            log("%s recv error: %s" % (name, errtext(exc)))
            break
        if not data:
            log("%s EOF (peer closed)" % name)
            break
        stats[name]["chunks"] += 1
        stats[name]["bytes"] += len(data)
        log("%s %5dB  %s" % (name, len(data), hexdump(data, hex_bytes)))
        ann.feed(data)
        try:
            send_all(dst, data)
        except OSError as exc:
            log("%s forward error: %s" % (name, errtext(exc)))
            break
    try:
        dst.shutdown(socket.SHUT_WR)
    except OSError:
        pass


def pump_hack(name, src, dst, ann, hack, hex_bytes, stats):
    while True:
        try:
            data = src.recv(65536)
        except OSError as exc:
            log("%s recv error: %s" % (name, errtext(exc)))
            break
        if not data:
            log("%s EOF (peer closed)" % name)
            break
        stats[name]["chunks"] += 1
        stats[name]["bytes"] += len(data)
        log("%s %5dB  %s" % (name, len(data), hexdump(data, hex_bytes)))
        out = hack.push(data)
        if out:
            ann.feed(out)              # 注解的是改完以后真正转发出去的那份
            try:
                send_all(dst, out)
            except OSError as exc:
                log("%s forward error: %s" % (name, errtext(exc)))
                break
    tail = hack.flush()
    if tail:
        log("%s flush %dB that was held waiting for a full frame" % (name, len(tail)))
        try:
            ann.feed(tail)
            send_all(dst, tail)
        except OSError as exc:
            log("%s flush error: %s" % (name, errtext(exc)))
    try:
        dst.shutdown(socket.SHUT_WR)
    except OSError:
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--listen-host", default="127.0.0.1")
    ap.add_argument("--listen-port", type=int, default=47000)
    ap.add_argument("--target-host", default="127.0.0.1")
    ap.add_argument("--target-port", type=int, default=47001)
    ap.add_argument("--hex-bytes", type=int, default=32)
    ap.add_argument("--hack-claimed", type=float, default=None)
    args = ap.parse_args()

    ls = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind((args.listen_host, args.listen_port))
    ls.listen(4)
    log("tap pid=%d listen %s:%d -> %s:%d  hex_bytes=%d"
        % (os.getpid(), args.listen_host, args.listen_port,
           args.target_host, args.target_port, args.hex_bytes))
    if args.hack_claimed is not None:
        log("tap HACK ON: every ATTACK.claimed_hp will be rewritten to %.2f"
            % args.hack_claimed)

    try:
        cs, addr = ls.accept()
    except OSError as exc:
        log("accept failed: %s" % errtext(exc))
        ls.close()
        return 2
    log("tap accept client %s:%d" % (addr[0], addr[1]))

    try:
        up = socket.create_connection((args.target_host, args.target_port), timeout=5)
    except OSError as exc:
        log("tap connect upstream %s:%d FAILED: %s"
            % (args.target_host, args.target_port, errtext(exc)))
        cs.close()
        ls.close()
        return 2
    up.settimeout(None)
    log("tap connected upstream %s:%d (tap's local port %d)"
        % (args.target_host, args.target_port, up.getsockname()[1]))

    stats = {"C>S": {"chunks": 0, "bytes": 0}, "S>C": {"chunks": 0, "bytes": 0}}
    ann_cs = Annotator("C>S", args.hex_bytes)
    ann_sc = Annotator("S>C", args.hex_bytes)
    hack = ClaimHack(args.hack_claimed) if args.hack_claimed is not None else None

    def cs_pump():
        if hack is not None:
            pump_hack("C>S", cs, up, ann_cs, hack, args.hex_bytes, stats)
        else:
            pump_plain("C>S", cs, up, ann_cs, args.hex_bytes, stats)

    def sc_pump():
        pump_plain("S>C", up, cs, ann_sc, args.hex_bytes, stats)

    t1 = threading.Thread(target=cs_pump, daemon=True)
    t2 = threading.Thread(target=sc_pump, daemon=True)
    t1.start()
    t2.start()
    t1.join()
    t2.join()

    log("tap summary  C>S chunks=%d bytes=%d frames=%d | S>C chunks=%d bytes=%d frames=%d"
        % (stats["C>S"]["chunks"], stats["C>S"]["bytes"], ann_cs.frames,
           stats["S>C"]["chunks"], stats["S>C"]["bytes"], ann_sc.frames))
    cs.close()
    up.close()
    ls.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
