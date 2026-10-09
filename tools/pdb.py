"""Read track rows from a rekordbox device export (PIONEER/rekordbox/export.pdb).

Layout from Deep Symmetry crate-digger's rekordbox_pdb.ksy; the file type field
(row offset 0x5a) from rekordcrate's Track struct. Read-only.

    python3 tools/pdb.py export.pdb [TYPE]      TYPE: 1 mp3, 4 m4a, 5 flac, 11 wav, 12 aiff
"""
from __future__ import annotations

import struct
import sys
from dataclasses import dataclass

FILE_TYPES = {0: "?", 1: "mp3", 4: "m4a", 5: "flac", 11: "wav", 12: "aiff"}
PAGE_HEAP = 0x28
TRACKS_TABLE = 0


@dataclass
class Track:
    id: int
    file_type: int
    sample_rate: int
    sample_depth: int
    bitrate: int
    duration: int
    file_size: int
    title: str
    file_path: str
    analyze_path: str


def dsql_string(buf: bytes, pos: int) -> str:
    kind = buf[pos]
    if kind == 0x40:
        n = struct.unpack_from("<H", buf, pos + 1)[0]
        return buf[pos + 4:pos + n].decode("ascii", "replace")
    if kind == 0x90:
        n = struct.unpack_from("<H", buf, pos + 1)[0]
        return buf[pos + 4:pos + n].decode("utf-16-le", "replace")
    n = (kind >> 1) - 1
    return buf[pos + 1:pos + 1 + n].decode("ascii", "replace")


def tracks(path: str) -> list[Track]:
    buf = open(path, "rb").read()
    len_page, num_tables = struct.unpack_from("<II", buf, 4)
    first = last = None
    for t in range(num_tables):
        ttype, _, tfirst, tlast = struct.unpack_from("<IIII", buf, 0x1C + 16 * t)
        if ttype == TRACKS_TABLE:
            first, last = tfirst, tlast
    out = []
    page = first
    seen = set()
    while page not in seen:
        seen.add(page)
        base = page * len_page
        ptype, next_page = struct.unpack_from("<II", buf, base + 8)
        rows_word = int.from_bytes(buf[base + 0x18:base + 0x1B], "little")
        num_row_offsets = rows_word & 0x1FFF
        flags = buf[base + 0x1B]
        if ptype == TRACKS_TABLE and flags & 0x40 == 0 and num_row_offsets:
            for g in range((num_row_offsets - 1) // 16 + 1):
                gbase = base + len_page - g * 0x24
                present = struct.unpack_from("<H", buf, gbase - 4)[0]
                for r in range(16):
                    if not present >> r & 1:
                        continue
                    ofs = struct.unpack_from("<H", buf, gbase - (6 + 2 * r))[0]
                    row = base + PAGE_HEAP + ofs
                    f = struct.unpack_from("<HHIIIIIHHIIIIIIIIIIIIHHHHHHBBHH", buf, row)
                    strs = struct.unpack_from("<21H", buf, row + 0x5E)
                    out.append(Track(
                        id=f[20], file_type=f[29], sample_rate=f[3], sample_depth=f[24],
                        bitrate=f[14], duration=f[25], file_size=f[5],
                        title=dsql_string(buf, row + strs[17]),
                        file_path=dsql_string(buf, row + strs[20]),
                        analyze_path=dsql_string(buf, row + strs[14])))
        if page == last:
            break
        page = next_page
    return out


if __name__ == "__main__":
    want = int(sys.argv[2]) if len(sys.argv) > 2 else None
    ts = tracks(sys.argv[1])
    from collections import Counter
    print("tracks:", len(ts), dict(Counter(FILE_TYPES.get(t.file_type, t.file_type) for t in ts)))
    for t in ts:
        if want is None or t.file_type == want:
            print(f"{t.id:6d} {FILE_TYPES.get(t.file_type, t.file_type):4s} {t.sample_rate:6d} "
                  f"{t.sample_depth:2d}b {t.duration:4d}s {t.file_path}  [{t.analyze_path}]")
