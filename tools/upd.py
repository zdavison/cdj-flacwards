"""Read, change and rebuild the CDJ-900 MAIN update file (C900MAIN.UPD).

File format (same as the CDJ-2000; see cdj2000-emulator tools/cdj_main/make_upd.py):
  * 32-byte header: "CDJ-900 MAIN    Ver4.32\\0", spaces, flag byte at 0x1f.
    The flag must be '0': '1' also rewrites the boot ROM and the loader.
    The updater takes a file only if its version is greater than the running one.
  * Motorola S-records: S0 "romobj  mot", S2 records of 32 bytes (all-zero
    records left out), S7 entry 0xa0000000, CRLF line ends.
  * CRC-16/XMODEM of everything before it, little-endian.

Flash image:
  0x00000  boot code
  0x10000  loader:      u32 packed size, LZSS data, u16 byte sum (LE)
  0x40000  application: u32 packed size, LZSS data, u16 byte sum (LE)
The byte sum covers the size word and the LZSS data.

Unpacked application (loads at 0x04000000):
  last 4 bytes = 32-bit sum of all earlier big-endian words, stored big-endian.
"""
from __future__ import annotations

import struct

HEADER_SIZE = 0x20
RECORD_BYTES = 32
S0_RECORD = b"S00E0000726F6D6F626A20206D6F74D8"
S7_RECORD = b"S705A00000005A"
APP_REGION = 0x40000
APP_REGION_END = 0x3E0000  # the updater erases 0x40000..0x3dffff
APP_BASE = 0x04000000

LZ_N = 4096
LZ_F = 18
LZ_MIN = 3


# --- CRC and header ---------------------------------------------------------

def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def header(version: str) -> bytes:
    if len(version) != 4 or version[1] != "." or not (version[0] + version[2:]).isdigit():
        raise ValueError("version must look like 4.33")
    text = b"CDJ-900 MAIN    Ver" + version.encode("ascii") + b"\0"
    return text.ljust(HEADER_SIZE - 1, b" ") + b"0"


# --- S-records --------------------------------------------------------------

def decode_upd(upd: bytes) -> bytes:
    """Return the flash image from address 0 that the S-records carry."""
    if crc16_xmodem(upd[:-2]) != int.from_bytes(upd[-2:], "little"):
        raise ValueError("UPD CRC mismatch")
    if upd[0x1F:0x20] != b"0":
        raise ValueError("UPD flag is not '0'")
    records = []
    for line in upd[HEADER_SIZE:-2].split(b"\r\n"):
        if not line.startswith(b"S2"):
            continue
        raw = bytes.fromhex(line[2:].decode("ascii"))
        if (sum(raw[:-1]) + raw[-1]) & 0xFF != 0xFF or raw[0] != len(raw) - 1:
            raise ValueError("bad S-record")
        records.append((int.from_bytes(raw[1:4], "big"), raw[4:-1]))
    end = max(a + len(p) for a, p in records)
    image = bytearray(end)
    for address, payload in records:
        image[address:address + len(payload)] = payload
    return bytes(image)


def srecord(address: int, payload: bytes) -> bytes:
    body = (len(payload) + 4).to_bytes(1, "big") + address.to_bytes(3, "big") + payload
    return b"S2" + (body + bytes([0xFF - (sum(body) & 0xFF)])).hex().upper().encode("ascii")


def encode_upd(image: bytes, version: str, end: int) -> bytes:
    if end > APP_REGION_END:
        raise ValueError("image runs past the updater's erase area")
    image = image.ljust(end, b"\xff")
    lines = [S0_RECORD]
    lines += [srecord(a, image[a:a + RECORD_BYTES]) for a in range(0, end, RECORD_BYTES)
              if any(image[a:a + RECORD_BYTES])]
    lines.append(S7_RECORD)
    body = header(version) + b"\r\n".join(lines) + b"\r\n"
    return body + crc16_xmodem(body).to_bytes(2, "little")


# --- LZSS (4 KiB window, 18-byte lookahead, flag bit 1 = literal) -----------

def lzss_decompress(src: bytes) -> bytes:
    window = bytearray(b" " * LZ_N)
    r = LZ_N - LZ_F
    flags = 0
    i = 0
    out = bytearray()
    while i < len(src):
        flags >>= 1
        if not flags & 0x100:
            flags = src[i] | 0xFF00
            i += 1
            if i >= len(src):
                break
        if flags & 1:
            c = src[i]
            i += 1
            out.append(c)
            window[r] = c
            r = (r + 1) & (LZ_N - 1)
        else:
            if i + 1 >= len(src):
                break
            pos = src[i] | ((src[i + 1] & 0xF0) << 4)
            length = (src[i + 1] & 0x0F) + LZ_MIN
            i += 2
            for k in range(length):
                c = window[(pos + k) & (LZ_N - 1)]
                out.append(c)
                window[r] = c
                r = (r + 1) & (LZ_N - 1)
    return bytes(out)


def lzss_compress(data: bytes, chain: int = 32) -> bytes:
    """Greedy LZSS with hash chains. Matches never use the initial spaces."""
    out = bytearray()
    heads: dict[bytes, list[int]] = {}
    n = len(data)
    i = 0
    flag_pos = -1
    flag_bit = 8
    while i < n:
        if flag_bit == 8:
            flag_pos = len(out)
            out.append(0)
            flag_bit = 0
        best_len, best_pos = 0, 0
        if i + LZ_MIN <= n:
            key = data[i:i + LZ_MIN]
            cands = heads.get(key)
            if cands:
                limit = min(LZ_F, n - i)
                for p in reversed(cands[-chain:]):
                    if i - p > LZ_N - 1:
                        break
                    length = LZ_MIN
                    while length < limit and data[p + length] == data[i + length]:
                        length += 1
                    if length > best_len:
                        best_len, best_pos = length, p
                        if length == limit:
                            break
        if best_len >= LZ_MIN:
            ring = (LZ_N - LZ_F + best_pos) & (LZ_N - 1)
            out.append(ring & 0xFF)
            out.append(((ring >> 4) & 0xF0) | (best_len - LZ_MIN))
            step = best_len
        else:
            out[flag_pos] |= 1 << flag_bit
            out.append(data[i])
            step = 1
        for k in range(i, min(i + step, n - LZ_MIN + 1)):
            lst = heads.setdefault(data[k:k + LZ_MIN], [])
            lst.append(k)
            if len(lst) > 4 * chain:
                del lst[:-chain]
        i += step
        flag_bit += 1
    return bytes(out)


# --- packed regions and image sum -------------------------------------------

def unpack_region(flash: bytes, address: int) -> bytes:
    size = int.from_bytes(flash[address:address + 4], "little")
    end = address + 4 + size
    if sum(flash[address:end]) & 0xFFFF != int.from_bytes(flash[end:end + 2], "little"):
        raise ValueError(f"region {address:#x} byte sum mismatch")
    return lzss_decompress(flash[address + 4:end])


def pack_region(unpacked: bytes) -> bytes:
    packed = lzss_compress(unpacked)
    if lzss_decompress(packed) != unpacked:
        raise AssertionError("LZSS round trip failed")
    body = len(packed).to_bytes(4, "little") + packed
    return body + (sum(body) & 0xFFFF).to_bytes(2, "little")


def image_sum(app: bytes) -> int:
    words = struct.unpack(">%dI" % ((len(app) - 4) // 4), app[:-4])
    return sum(words) & 0xFFFFFFFF


def fix_image_sum(app: bytearray) -> None:
    app[-4:] = image_sum(app).to_bytes(4, "big")


def check_image_sum(app: bytes) -> bool:
    return image_sum(app) == int.from_bytes(app[-4:], "big")


def build_flash(stock_flash: bytes, app: bytes) -> bytes:
    """Return a flash image with the application region replaced by app."""
    region = pack_region(app)
    end = APP_REGION + len(region)
    if end > APP_REGION_END:
        raise ValueError(f"packed application ends at {end:#x}, past {APP_REGION_END:#x}")
    flash = bytearray(stock_flash[:APP_REGION])
    flash += region
    return bytes(flash)
