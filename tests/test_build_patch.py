#!/usr/bin/env python3
"""Check a patched image: each hook word points at its wrapper, the blob is in
the padding, and no other byte outside the padding changed.

    test_build_patch.py           out/flac    (debug blob, console commands)
    test_build_patch.py release   out/release (release blob: hooks only)
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_patch as bp  # noqa: E402
import upd  # noqa: E402

release = sys.argv[1:] == ["release"]
name, blob_name = ("release", "flac-release") if release else ("flac", "flac")
flash = (ROOT / f"out/{name}/main-firmware.bin").read_bytes()
app = upd.unpack_region(flash, upd.APP_REGION)
_, stock = bp.load_stock()
syms = bp.symbols(ROOT / f"build/{blob_name}.sym")
blob = (ROOT / f"build/{blob_name}.bin").read_bytes()
failures = 0
if release:
    extra = [n for n in syms if n.startswith(("_cmd_", "_vs_", "_vh_stats"))]
    if extra:
        print(f"FAIL the release blob holds debug symbols: {extra}")
        failures += 1


def word(data, addr):
    o = bp.off(addr)
    return int.from_bytes(data[o:o + 4], "little")


if len(bp.FLAC_HOOKS) != 20 or (0xA41B0A1C, 0x042F01E8, "_hook_send") not in bp.FLAC_HOOKS \
        or sum(1 for h in bp.FLAC_HOOKS if h[2] == "_hook_fill_policy") != 3:
    print("FAIL FLAC_HOOKS must hold the 20 words: the second send and the 3 fill-policy words")
    failures += 1
allowed = set()
for slot, old, name in bp.FLAC_HOOKS:
    new = word(app, slot)
    if new & 0x1FFFFFFF != syms[name] or new & 0xE0000000 != word(stock, slot) & 0xE0000000:
        print(f"FAIL {slot:#x}: {new:#x}, expected {syms[name]:#x} with the stock alias")
        failures += 1
    allowed.update(range(bp.off(slot), bp.off(slot) + 4))
if not release:
    allowed.update(range(bp.off(bp.CMD_TABLE_SLOT), bp.off(bp.CMD_TABLE_SLOT) + 4))
allowed.update(range(bp.off(bp.VERSION_ADDR), bp.off(bp.VERSION_ADDR) + 5))
allowed.update(range(bp.off(bp.DATE_ADDR), bp.off(bp.DATE_ADDR) + 9))
pad = range(bp.off(bp.PAD_START), bp.off(bp.PAD_END) + 4)   # includes the image sum
other = [i for i in range(len(stock)) if app[i] != stock[i] and i not in allowed and i not in pad]
if other:
    print(f"FAIL {len(other)} bytes changed outside the allowed words, first at {other[0]:#x}")
    failures += 1
if app[bp.off(bp.BLOB_ADDR):bp.off(bp.BLOB_ADDR) + len(blob)] != blob:
    print(f"FAIL the blob in the image is not build/{blob_name}.bin")
    failures += 1
print(f"{failures} failures")
sys.exit(1 if failures else 0)
