"""A stand-in for the CDJ-900 GUI board on the emulator's GUI bus (CDJ_GUI_BUS).

    python3 tools/fake_gui.py PORT [--log FILE] [--seconds N]
                              [--inject SECONDS:TYPE[:W2:W3...]] ...

Wire format (see cdj2000_main.c, CDJ_GUI_BUS): frames are "CDJL"/"CDJI" +
u32 little-endian length + bytes. MAIN sends its 64-byte status record for
every GUI exchange; we send 48-byte requests. Word 1 of a request is the type
(low 14 bits), words 2.. are arguments, and word 23 is the CRC of bytes 0..45.
Bit 15 of word 1 means "cancel the open request" (the dispatcher at
0x0421552e then also handles the type), and repeating the same list request
while one is open also cancels it. So a request goes out once, without bit
15, and the idle request (all zero) follows; it does nothing in MAIN.
"""
from __future__ import annotations

import argparse
import socket
import struct
import sys
import time

REQUEST_LEN = 48


def firmware_crc(data: bytes) -> int:
    """CRC-16, polynomial 0x1021, init 0, two zero bytes appended."""
    state = 0
    for byte in data:
        state |= byte
        for _ in range(8):
            state <<= 1
            if state & (1 << 24):
                state ^= 0x01102100
    for _ in range(16):
        state <<= 1
        if state & (1 << 24):
            state ^= 0x01102100
    return (state >> 8) & 0xFFFF


def request(words: dict[int, int]) -> bytes:
    """A 48-byte request with words set by index (0..22) and the CRC in word 23."""
    buf = bytearray(REQUEST_LEN)
    for index, value in words.items():
        if not 0 <= index < 23:
            raise ValueError("word index must be 0..22")
        struct.pack_into("<H", buf, index * 2, value & 0xFFFF)
    struct.pack_into("<H", buf, 46, firmware_crc(bytes(buf[:46])))
    return bytes(buf)


def frame(magic: bytes, payload: bytes) -> bytes:
    return magic + struct.pack("<I", len(payload)) + payload


def parse_inject(spec: str) -> tuple[float, bytes]:
    parts = spec.split(":")
    words = {1: int(parts[1], 0)}
    for i, v in enumerate(parts[2:], start=2):
        words[i] = int(v, 0)
    return float(parts[0]), request(words)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("port", type=int)
    ap.add_argument("--log", default="gui-bus.log")
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--hold", type=float, default=1.5,
                    help="seconds to repeat each injected request")
    ap.add_argument("--inject", action="append", default=[],
                    help="SECONDS:TYPE[:W2:W3...] -- a request at that time")
    args = ap.parse_args()
    injects = sorted(parse_inject(s) for s in args.inject)

    deadline = time.time() + 10
    while True:
        try:
            sock = socket.create_connection(("127.0.0.1", args.port))
            break
        except OSError:
            if time.time() > deadline:
                raise
            time.sleep(0.2)
    sock.settimeout(0.05)
    idle = request({})
    sock.sendall(frame(b"CDJI", idle))
    release_at = None
    start = time.time()
    buf = b""
    last = None
    count = 0
    changes = 0
    bad_crc = 0
    with open(args.log, "w") as log:
        while time.time() - start < args.seconds:
            now = time.time() - start
            while injects and injects[0][0] <= now:
                _, req = injects.pop(0)
                sock.sendall(frame(b"CDJL", req))
                log.write(f"{now:9.3f} -> {req.hex(' ')}\n")
            if release_at is not None and now >= release_at:
                sock.sendall(frame(b"CDJI", idle))
                release_at = None
            try:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                buf += chunk
            except socket.timeout:
                continue
            while len(buf) >= 8:
                if buf[:4] != b"CDJL":
                    print("desync", buf[:16].hex(), file=sys.stderr)
                    return 1
                n = struct.unpack_from("<I", buf, 4)[0]
                if len(buf) < 8 + n:
                    break
                rec, buf = buf[8:8 + n], buf[8 + n:]
                count += 1
                if n >= 64 and firmware_crc(rec[:62]) != struct.unpack_from("<H", rec, 62)[0]:
                    bad_crc += 1
                if rec != last or n != 66:
                    changes += 1
                    last = rec
                    log.write(f"{now:9.3f} <- {rec.hex(' ')}\n")
                    log.flush()
    print(f"records {count}, changes {changes}, bad crc {bad_crc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
