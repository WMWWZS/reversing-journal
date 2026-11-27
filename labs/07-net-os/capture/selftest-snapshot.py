#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""selftest-snapshot.py -- extra self-test for the ZOMBIE TCP desegmentation.

Builds `selftest-snapshot.pcapng`: one TCP connection (52341 <-> 4001) with
handshake, HELLO, then a 16006-byte ZOMBIE SNAPSHOT frame (count = 1000)
delivered in TWO TCP segments of 8000 and 8006 bytes, then BYE and FIN.

This exercises the `pinfo.desegment_len` code path in zombie.lua, which is
the path the real lab hits with `--snapshot-count 1000` (BRIEF.md: "count=1000
-> payload 16002 bytes").  demo.pcapng itself stays small on purpose.

Expected result with:
  tshark -r selftest-snapshot.pcapng -X lua_script:zombie.lua
    frame 5 -> TCP ... [TCP segment of a reassembled PDU]
    frame 6 -> ZOMBIE ... SNAPSHOT count=1000
and
  tshark -r selftest-snapshot.pcapng -X lua_script:zombie.lua \
         -Y 'zombie.count' -T fields -e zombie.count -e zombie.snapshot.id
returns one row with 1000,1000 (count; the last entity id) and
`-Y zombie.snapshot.id` counts 1000 fields.

Run:  python selftest-snapshot.py
"""

import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mkpcap  # noqa: E402  (same folder)

TEXT2PCAP = r"E:\Wireshark\text2pcap.exe"

COUNT = 1000
SPLIT = 8000          # bytes of the SNAPSHOT frame in the first TCP segment


def build():
    payload = struct.pack("<H", COUNT)
    for i in range(COUNT):
        payload += struct.pack("<Ifff", i, float(i), -float(i), 100.0 - i * 0.1)
    snap_frame = mkpcap.zombie_frame(0x04, payload)          # 4 + 16002 = 16006
    assert len(snap_frame) == 16006, len(snap_frame)

    hello = mkpcap.HELLO_FRAME
    bye = mkpcap.BYE_FRAME
    c, s = mkpcap.CLIENT_ISN, mkpcap.SERVER_ISN

    pkts = []
    # handshake
    pkts.append(mkpcap.build_packet("C2S", c, 0, 0x02, b"", 0x2000))
    pkts.append(mkpcap.build_packet("S2C", s, c + 1, 0x12, b"", 0x2001))
    pkts.append(mkpcap.build_packet("C2S", c + 1, s + 1, 0x10, b"", 0x2002))
    # client HELLO
    pkts.append(mkpcap.build_packet("C2S", c + 1, s + 1, 0x18, hello, 0x2003))
    # server SNAPSHOT, split over two TCP segments (two IP packets)
    pkts.append(mkpcap.build_packet("S2C", s + 1, c + 1 + len(hello),
                                    0x18, snap_frame[:SPLIT], 0x2004))
    pkts.append(mkpcap.build_packet("S2C", s + 1 + SPLIT, c + 1 + len(hello),
                                    0x18, snap_frame[SPLIT:], 0x2005))
    # client BYE, then teardown
    pkts.append(mkpcap.build_packet("C2S", c + 1 + len(hello), s + 1 + len(snap_frame),
                                    0x18, bye, 0x2006))
    pkts.append(mkpcap.build_packet("C2S", c + 1 + len(hello) + len(bye),
                                   s + 1 + len(snap_frame), 0x11, b"", 0x2007))
    pkts.append(mkpcap.build_packet("S2C", s + 1 + len(snap_frame),
                                   c + 2 + len(hello) + len(bye), 0x11, b"", 0x2008))
    pkts.append(mkpcap.build_packet("C2S", c + 2 + len(hello) + len(bye),
                                   s + 2 + len(snap_frame), 0x10, b"", 0x2009))
    return pkts


def main():
    packets = build()
    hexdump = os.path.join(HERE, "selftest-snapshot-frames.hexdump")
    out = os.path.join(HERE, "selftest-snapshot.pcapng")
    mkpcap.write_text2pcap_input([("frame", f) for f in packets], hexdump)
    print("wrote text2pcap input %s (%d packets)" % (hexdump, len(packets)))
    print("frame sizes:", [len(f) for f in packets])

    proc = subprocess.run([TEXT2PCAP, "-l", "1", "-F", "pcapng",
                           "-N", "synth-loopback", hexdump, out],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(proc.stdout.decode("utf-8", "replace").strip())
    if proc.returncode != 0:
        return proc.returncode
    os.remove(hexdump)
    print("wrote %s (%d bytes)" % (out, os.path.getsize(out)))
    print("")
    print("verify (expect frame 5 reassembled segment, frame 6 SNAPSHOT count=1000):")
    print('  "E:\\Wireshark\\tshark.exe" -r "%s" -X lua_script:zombie.lua -Y zombie' % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
