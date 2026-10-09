"""SH-4 helpers for the unpacked CDJ-900 MAIN application.

The application loads at 0xa4000000 and is little-endian SH-4A.
SH-4 code reaches constants through PC-relative literal pools:
mov.l @(disp,PC),Rn reads the word at (PC & ~3) + 4 + disp * 4.
"""
import struct
import sys
from pathlib import Path

import capstone

BASE = 0xA4000000
IMAGE = Path(__file__).resolve().parent.parent / "firmware/unpacked/main-unpacked.bin"

data = IMAGE.read_bytes()
cs = capstone.Cs(capstone.CS_ARCH_SH, capstone.CS_MODE_SH4A | capstone.CS_MODE_SHFPU | capstone.CS_MODE_LITTLE_ENDIAN)


def off(addr):
    # Code pointers use the P0 alias 0x04......, data pointers use P2 0xa4.......
    return (addr & 0x1FFFFFFF) - (BASE & 0x1FFFFFFF)


def u32(addr):
    return struct.unpack_from("<I", data, off(addr))[0]


def cstr(addr, limit=80):
    o = off(addr)
    end = data.find(b"\0", o, o + limit)
    return data[o:end if end != -1 else o + limit].decode("latin-1")


def pool_slots(value):
    """Yield every 4-aligned address whose word equals value."""
    needle = struct.pack("<I", value)
    o = data.find(needle)
    while o != -1:
        if o % 4 == 0:
            yield BASE + o
        o = data.find(needle, o + 1)


def loads_of(slot):
    """Yield code addresses that load the literal at slot (mov.l and mova)."""
    # mov.l @(disp,PC),Rn = 0xDndd, reach 1020 bytes forward.
    for disp in range(256):
        start = slot - 4 - disp * 4
        for pc in (start, start + 2):
            if pc < BASE:
                continue
            w = struct.unpack_from("<H", data, off(pc))[0]
            if w >> 12 == 0xD and (w & 0xFF) == disp and ((pc & ~3) + 4 + disp * 4) == slot:
                yield pc


def xrefs(value):
    """Return (code address, pool slot) pairs that load value, in any alias."""
    phys = value & 0x1FFFFFFF
    out = []
    for alias in sorted({value, phys, phys | 0x80000000, phys | 0xA0000000}):
        out += [(pc, s) for s in pool_slots(alias) for pc in loads_of(s)]
    return out


def function_start(pc, limit=0x4000):
    """Walk back to the nearest 'sts.l pr,@-r15' (0x4f22), a common prologue."""
    a = pc & ~1
    while a > pc - limit:
        if struct.unpack_from("<H", data, off(a))[0] == 0x4F22:
            # Prologues often push r8-r14 before pr; step back over them.
            while struct.unpack_from("<H", data, off(a - 2))[0] & 0xF0FF == 0x2F06:
                a -= 2
            return a
        a -= 2
    return None


def disasm(addr, count=40):
    out = []
    a = addr
    while len(out) < count:
        ins = next(cs.disasm(data[off(a):off(a) + 2], a), None)
        w = struct.unpack_from("<H", data, off(a))[0]
        if ins is None:
            out.append(f"{a:08x}: .word      {w:#06x}")
            a += 2
            continue
        a += 2
        line = f"{ins.address:08x}: {ins.mnemonic:10s} {ins.op_str}"
        if w >> 12 == 0xD:
            slot = (ins.address & ~3) + 4 + (w & 0xFF) * 4
            v = u32(slot)
            note = f"  ; ={v:#010x}"
            if 0 <= off(v) < len(data):
                s = cstr(v, 40)
                if s.isprintable() and len(s) >= 3:
                    note += f' "{s}"'
            line += note
        out.append(line)
    return "\n".join(out)


if __name__ == "__main__":
    cmd, arg = sys.argv[1], int(sys.argv[2], 0)
    if cmd == "xref":
        for pc, s in xrefs(arg):
            print(f"{pc:#010x} loads slot {s:#010x}  func~{function_start(pc) or 0:#010x}")
    elif cmd == "dis":
        print(disasm(arg, int(sys.argv[3]) if len(sys.argv) > 3 else 40))
