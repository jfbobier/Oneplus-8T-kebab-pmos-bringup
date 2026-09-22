#!/usr/bin/env python3
"""
Minimal offline parser for a raw DIAG capture (.qmdl) from diag_reader.

Pulls out uncompressed F3 / EXT_MSG (DIAG cmd 0x79) messages, which is what
diag_reader's live parser stops emitting once its HDLC framing desyncs on the
8 KB 0x99 LOG_F packets. Does NOT decode QSR4-compressed messages -- for those
you need the qdsp6m.qdb hash database and scat.

Layout of an EXT_MSG payload, established from diag_reader's own output:
  [0:4]   cmd 0x79 + pad
  [4:12]  u64 LE modem timestamp
  [12:14] u16 LE line number
  [14:16] u16 LE SSID
  [16:20] u32 mask/level
  [20:]   NUL-separated strings (format string, then source file)

Usage: qmdl-f3.py FILE [tail_n]
"""
import sys, re, struct

def unescape(frame: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(frame):
        b = frame[i]
        if b == 0x7d and i + 1 < len(frame):
            out.append(frame[i + 1] ^ 0x20)
            i += 2
        else:
            out.append(b)
            i += 1
    return bytes(out)

def main():
    path = sys.argv[1]
    tail = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    data = open(path, "rb").read()

    rows = []
    for frame in data.split(b"\x7e"):
        if len(frame) < 24:
            continue
        p = unescape(frame)
        if not p or p[0] != 0x79 or len(p) < 21:
            continue
        ts = struct.unpack_from("<Q", p, 4)[0]
        line = struct.unpack_from("<H", p, 12)[0]
        ssid = struct.unpack_from("<H", p, 14)[0]
        # be tolerant about arg layout: just take printable runs from 20 on
        strs = [s.decode("ascii", "replace")
                for s in re.findall(rb"[\x20-\x7e]{4,}", p[20:])]
        if not strs:
            continue
        msg = strs[0]
        src = strs[-1] if len(strs) > 1 else ""
        rows.append((ts, ssid, line, src, msg))

    print("total EXT_MSG packets parsed: %d" % len(rows))
    if not rows:
        return
    print("modem ts range: 0x%x .. 0x%x" % (rows[0][0], rows[-1][0]))
    sel = rows[-tail:] if tail else rows
    for ts, ssid, line, src, msg in sel:
        print("ts=0x%016x SSID=%-6d %-24s %s" % (ts, ssid, src[:24], msg))

if __name__ == "__main__":
    main()
