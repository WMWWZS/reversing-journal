# -*- coding: utf-8 -*-
"""mkpcap.py -- build the ZOMBIE demo capture (demo.pcapng).

Why this script exists
----------------------
`text2pcap` can prepend a dummy TCP header (`-T 52341,47001`), but that
dummy header is hard-coded: raw seq=0, ack=0, flags=0x000.  We checked it on
this machine, so a SYN/SYN-ACK/FIN session cannot be expressed that way.

This script therefore builds **complete Ethernet II / IPv4 / TCP frames**
itself -- real ports, real seq/ack numbers, SYN / SYN-ACK / ACK / PSH+ACK
/ FIN+ACK flags, correct IP and TCP checksums -- writes them as a
text2pcap hexdump, and lets **text2pcap** write the pcapng container:

    python mkpcap.py --keep-hexdump
        -> demo-frames.hexdump              (our frames, ASCII hexdump)
        -> text2pcap -l 1 -F pcapng ...      (Wireshark writes demo.pcapng)

The ZOMBIE payload bytes (HELLO / ATTACK / HP_SYNC / BYE) are copied
verbatim from a real run of the src/ programs -- see the provenance comment
above the frame constants.  Only the Ethernet / IPv4 / TCP headers and the
TCP connection itself (52341 -> 47001, 127.0.0.1) are synthesized; the demo
is used only to validate the Lua dissector.

Standard library only.  Python 3.7+.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys

# --------------------------------------------------------------------------
# Fixed endpoints.  One TCP connection: client 52341 -> server 47001.
# (The real lab goes client -> tap.py:47000 -> server:47001, i.e. two TCP
# connections bridged by the tap.  A single client<->server flow is enough to
# exercise the dissector.  The Lua dissector is registered on both 47000 and
# 47001 anyway.)
# --------------------------------------------------------------------------
CLIENT_MAC = bytes([0x02, 0x00, 0x00, 0x00, 0x00, 0x01])
SERVER_MAC = bytes([0x02, 0x00, 0x00, 0x00, 0x00, 0x02])
CLIENT_IP = bytes([127, 0, 0, 1])
SERVER_IP = bytes([127, 0, 0, 1])
CLIENT_PORT = 52341
SERVER_PORT = 47001

# Initial sequence numbers (arbitrary but consistent).
CLIENT_ISN = 1000
SERVER_ISN = 2000


# --------------------------------------------------------------------------
# ZOMBIE frames -- exactly the frozen layout, all little-endian.
#   total_len(2) opcode(1) flags(1) payload(...)
#   total_len = 1 (opcode) + 1 (flags) + len(payload)
#
# The bytes below are copied VERBATIM from a real run of this lab's
# zombie_srv.exe / zombie_cli.exe / tap.py on this machine (real TCP sockets
# on 127.0.0.1).  Source: ..\evidence\tap.txt -- the four hex strings below
# match C>S frame #1, C>S frame #2, S>C frame #1 and C>S frame #7 of that file
# byte for byte (timestamps differ on every run, so only the hex is quoted):
#
#   C>S 12B HELLO    0a 00 01 00 01 00 00 00 01 00 00 00
#   C>S 16B ATTACK   0e 00 02 00 00 00 a0 40 01 00 00 00 00 00 be 42
#   S>C 24B HP_SYNC  16 00 03 00 01 00 00 00 00 00 be 42 00 00 c8 42 01 00 00 00 01 00 00 00
#   C>S  4B BYE      02 00 05 00
#
# Only the Ethernet / IPv4 / TCP headers and the TCP session itself
# (ports 52341 -> 47001, 127.0.0.1 seq/ack) are synthesized here.
# --------------------------------------------------------------------------
HELLO_FRAME = bytes.fromhex("0a 00 01 00 01 00 00 00 01 00 00 00")
# ATTACK: f32 damage=5.0, u32 seq=1, f32 claimed_hp=95.0  -> 12-byte payload, 16-byte frame
ATTACK_FRAME = bytes.fromhex("0e 00 02 00 00 00 a0 40 01 00 00 00 00 00 be 42")
# HP_SYNC: u32 player_id=1, f32 hp=95.0, f32 max_hp=100.0, u32 tick=1,
#          u8 alive=1, u8[3] pad -> 20-byte payload, 24-byte frame
HP_SYNC_FRAME = bytes.fromhex(
    "16 00 03 00 01 00 00 00 00 00 be 42 00 00 c8 42 01 00 00 00 01 00 00 00")
# BYE, empty payload -> 4-byte frame
BYE_FRAME = bytes.fromhex("02 00 05 00")


def check_frames():
    """Fail loudly if the frozen wire format constants are not self-consistent."""
    for name, frame, opcode in (
            ("HELLO", HELLO_FRAME, 0x01),
            ("ATTACK", ATTACK_FRAME, 0x02),
            ("HP_SYNC", HP_SYNC_FRAME, 0x03),
            ("BYE", BYE_FRAME, 0x05)):
        total_len = struct.unpack("<H", frame[0:2])[0]
        assert total_len == len(frame) - 2, "%s: total_len mismatch" % name
        assert frame[2] == opcode, "%s: opcode mismatch" % name
        assert frame[3] == 0x00, "%s: flags must be 0" % name
    # ZOMBIE frame sizes required by the lab README (12 / 16 / 24 / 4 bytes)
    assert len(ATTACK_FRAME) == 16, len(ATTACK_FRAME)
    assert len(HP_SYNC_FRAME) == 24, len(HP_SYNC_FRAME)


check_frames()


def zombie_frame(opcode, payload=b""):
    """Build one ZOMBIE frame.  Only used by selftest-snapshot.py (big SNAPSHOT)."""
    total_len = 2 + len(payload)
    return struct.pack("<HBB", total_len, opcode, 0x00) + payload


def checksum16(data):
    """Internet checksum: one's complement of the one's complement sum."""
    if len(data) % 2:
        data += b"\x00"
    total = 0
    for i in range(0, len(data), 2):
        total += (data[i] << 8) | data[i + 1]
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def ipv4_header(src_ip, dst_ip, payload_len, ip_id):
    total_len = 20 + payload_len
    hdr = struct.pack("!BBHHHBBH4s4s",
                      0x45,          # version 4, IHL 5
                      0x00,          # DSCP/ECN
                      total_len,
                      ip_id,
                      0x4000,        # Don't Fragment
                      128,           # TTL
                      6,             # protocol TCP
                      0x0000,        # checksum placeholder
                      src_ip, dst_ip)
    csum = checksum16(hdr)
    hdr = hdr[:10] + struct.pack("!H", csum) + hdr[12:]
    return hdr


def ethernet_frame(src_mac, dst_mac, ip_packet):
    return dst_mac + src_mac + struct.pack("!H", 0x0800) + ip_packet


def build_packet(direction, seq, ack, flags, payload, ip_id):
    """direction is 'C2S' or 'S2C'."""
    if direction == "C2S":
        src_mac, dst_mac = CLIENT_MAC, SERVER_MAC
        src_ip, dst_ip = CLIENT_IP, SERVER_IP
        src_port, dst_port = CLIENT_PORT, SERVER_PORT
    else:
        src_mac, dst_mac = SERVER_MAC, CLIENT_MAC
        src_ip, dst_ip = SERVER_IP, CLIENT_IP
        src_port, dst_port = SERVER_PORT, CLIENT_PORT

    tcp = struct.pack("!HHIIHHHH",
                      src_port, dst_port,
                      seq, ack,
                      (5 << 12) | flags,
                      65535, 0x0000, 0)
    pseudo = src_ip + dst_ip + struct.pack("!BBH", 0, 6, len(tcp) + len(payload))
    tcp_csum = checksum16(pseudo + tcp + payload)
    tcp = tcp[:16] + struct.pack("!H", tcp_csum) + tcp[18:]

    ip = ipv4_header(src_ip, dst_ip, len(tcp) + len(payload), ip_id)
    return ethernet_frame(src_mac, dst_mac, ip + tcp + payload)


def build_session():
    """Returns [(label, frame_bytes), ...] -- one complete TCP session."""
    # TCP flag bits: FIN=0x01 SYN=0x02 RST=0x04 PSH=0x08 ACK=0x10
    specs = [
        ("SYN      client -> server", "C2S", CLIENT_ISN, 0,
         0x02, b""),
        ("SYN-ACK  server -> client", "S2C", SERVER_ISN, CLIENT_ISN + 1,
         0x12, b""),
        ("ACK      client -> server", "C2S", CLIENT_ISN + 1, SERVER_ISN + 1,
         0x10, b""),
        ("HELLO    client -> server, 12 bytes", "C2S", CLIENT_ISN + 1, SERVER_ISN + 1,
         0x18, HELLO_FRAME),
        ("ATTACK   client -> server, 16 bytes", "C2S",
         CLIENT_ISN + 1 + len(HELLO_FRAME), SERVER_ISN + 1,
         0x18, ATTACK_FRAME),
        ("HP_SYNC  server -> client, 24 bytes", "S2C",
         SERVER_ISN + 1, CLIENT_ISN + 1 + len(HELLO_FRAME) + len(ATTACK_FRAME),
         0x18, HP_SYNC_FRAME),
        ("FIN      client -> server", "C2S",
         CLIENT_ISN + 1 + len(HELLO_FRAME) + len(ATTACK_FRAME),
         SERVER_ISN + 1 + len(HP_SYNC_FRAME),
         0x11, b""),
        ("FIN-ACK  server -> client", "S2C",
         SERVER_ISN + 1 + len(HP_SYNC_FRAME),
         CLIENT_ISN + 2 + len(HELLO_FRAME) + len(ATTACK_FRAME),
         0x11, b""),
        ("ACK      client -> server (teardown)", "C2S",
         CLIENT_ISN + 2 + len(HELLO_FRAME) + len(ATTACK_FRAME),
         SERVER_ISN + 2 + len(HP_SYNC_FRAME),
         0x10, b""),
    ]

    packets = []
    ip_id = 0x1000
    for label, direction, seq, ack, flags, payload in specs:
        frame = build_packet(direction, seq, ack, flags, payload, ip_id)
        ip_id += 1
        packets.append((label, frame))
    return packets


def hexdump_lines(data, bytes_per_line=16):
    lines = []
    for off in range(0, len(data), bytes_per_line):
        chunk = data[off:off + bytes_per_line]
        lines.append("%06x  %s" % (off, " ".join("%02x" % b for b in chunk)))
    return lines


def write_text2pcap_input(packets, path):
    """One hexdump block per packet, blank line in between.

    Deliberately NO per-packet text labels: text2pcap 4.0.17 mis-parses a
    hexdump when a "#TEXT" comment line ends in a single digit (we hit this
    with labels like "packet 1": only 152 of 16562 payload bytes survived).
    Packet descriptions are printed to stdout instead.
    """
    out = []
    out.append("#TEXT")
    out.append("ZOMBIE demo: synthesized Ethernet/IPv4/TCP headers.")
    out.append("ZOMBIE payload bytes follow the frozen format in README.md.")
    out.append("")
    for _label, frame in packets:
        out.extend(hexdump_lines(frame))
        out.append("")
    with open(path, "w", encoding="ascii", newline="\n") as fh:
        fh.write("\n".join(out))
        fh.write("\n")


def find_text2pcap(explicit):
    candidates = []
    if explicit:
        candidates.append(explicit)
    if os.environ.get("WIRESHARK_DIR"):
        candidates.append(os.path.join(os.environ["WIRESHARK_DIR"], "text2pcap.exe"))
    candidates.append(r"E:\Wireshark\text2pcap.exe")   # this box: Wireshark 4.0.17
    on_path = shutil.which("text2pcap")
    if on_path:
        candidates.append(on_path)
    for cand in candidates:
        if cand and os.path.isfile(cand):
            return cand
    return None


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Build demo.pcapng for the ZOMBIE protocol.")
    ap.add_argument("--text2pcap", default=None, help="path to text2pcap.exe")
    ap.add_argument("--out", default=os.path.join(here, "demo.pcapng"),
                    help="output pcapng path (default: next to this script)")
    ap.add_argument("--hexdump", default=os.path.join(here, "demo-frames.hexdump"),
                    help="text2pcap input file to write")
    ap.add_argument("--keep-hexdump", action="store_true",
                    help="keep demo-frames.hexdump after a successful run")
    args = ap.parse_args()

    text2pcap = find_text2pcap(args.text2pcap)
    if not text2pcap:
        print("ERROR: text2pcap.exe not found. Pass --text2pcap <path>.", file=sys.stderr)
        return 2

    packets = build_session()
    write_text2pcap_input(packets, args.hexdump)
    print("text2pcap input written: %s (%d packets)" % (args.hexdump, len(packets)))

    cmd = [text2pcap,
           "-l", "1",                      # link-layer type 1 = Ethernet
           "-F", "pcapng",                 # output format
           "-N", "synth-loopback",         # interface name inside the pcapng
           args.hexdump, args.out]
    print("running: %s" % " ".join('"%s"' % c if " " in c else c for c in cmd))
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = proc.stdout.decode("utf-8", "replace")
    print(output.strip())
    if proc.returncode != 0:
        print("ERROR: text2pcap failed with exit code %d" % proc.returncode, file=sys.stderr)
        return proc.returncode

    print("")
    print("demo pcapng written: %s (%d bytes)" % (args.out, os.path.getsize(args.out)))
    print("")
    print("frames in the session:")
    for i, (label, frame) in enumerate(packets, 1):
        print("  %2d  %-38s %3d bytes  %s" %
              (i, label, len(frame), frame.hex()))
    print("")
    print("validate with:")
    lua = os.path.join(here, "zombie.lua")
    print('  "%s" -r "%s" -X lua_script:"%s" -Y zombie' %
          (r"E:\Wireshark\tshark.exe", args.out, lua))
    print('  (in the GUI: open the pcapng and type "zombie" in the filter bar)')

    if not args.keep_hexdump:
        try:
            os.remove(args.hexdump)
        except OSError:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
