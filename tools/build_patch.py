"""Build CDJ-900 MAIN update files from the stock C900MAIN.UPD.

    python3 tools/build_patch.py hello    -> out/hello/C900MAIN.UPD    (version 4.33)
    python3 tools/build_patch.py rollback -> out/rollback/C900MAIN.UPD (stock image, header 4.99)
    python3 tools/build_patch.py flac     -> out/flac/C900MAIN.UPD     (version 4.35)

The updater takes a file only if its header version is greater than the
running version. The running version is the "4.32" string at 0x04000740.

hello:    a data-only change. The deck shows version 4.33 and date 20261008,
          and the padding gets an inert marker. No new code runs.
rollback: the stock application in a file with header version 4.99. After the
          update the deck reports 4.32 again.
flac:     the FLAC hooks (build/flac.bin) and the console command "N,VW".
          20 literal-pool words point at the wrappers. No instruction changes.
"""
from __future__ import annotations

import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import upd  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
STOCK_UPD = ROOT / "firmware/CDJ-900v432/C900MAIN.UPD"
STOCK_SHA256 = "75f428732586d282"  # prefix of sha256(C900MAIN.UPD) for v4.32

VERSION_ADDR = 0x04000740
DATE_ADDR = 0x04000760
PAD_START = 0x043471C8
PAD_END = 0x043BFFFC
MARKER = b"cdj900-flac hello marker v1\0"

# Console command table: 8-byte entries {name halfword, argc, radix, handler};
# the lookup loop stops at "ZZ". One literal-pool slot holds its address.
CMD_TABLE = 0xA405C310
CMD_TABLE_SLOT = 0xA4101DC4
NEW_CMD_TABLE = 0x04347200
BLOB_ADDR = 0x04347400

# FLAC hooks: (literal-pool word, stock call target, our symbol).
# See NOTES.md, "FLAC hooks". All are call targets in playback code.
FLAC_HOOKS = [
    (0xA41B0460, 0x042E8EA8, "_hook_open"),      # FUN_041b0376, playback open
    (0xA41B0468, 0x042E59DE, "_hook_fclose"),    # FUN_041b0402, close
    (0xA41E3428, 0x042E5E1E, "_hook_fseek"),     # FUN_041e324a, header seek
    (0xA419EFCC, 0x042E5E1E, "_hook_fseek"),     # FUN_0419ebb6, stream seek
    (0xA41E3438, 0x042E5C1A, "_hook_fread"),     # FUN_041e328c, header read
    (0xA419EFFC, 0x042E5C1A, "_hook_fread"),     # FUN_0419ed9c, stream read
    (0xA41E343C, 0x042E5EA8, "_hook_ftell"),     # FUN_041e3300
    (0xA41E51B8, 0x042E5EA8, "_hook_ftell"),     # FUN_041e49b2, WAV parser
    (0xA419EC88, 0x042E5EA8, "_hook_ftell"),     # stream API
    (0xA419EFF4, 0x042E5EA8, "_hook_ftell"),     # FUN_0419ed9c
    (0xA41E3440, 0x042E65A4, "_hook_filelen"),   # FUN_041e3340, FUN_041e33c8
    (0xA41E3444, 0x042E5F3A, "_hook_feof"),      # FUN_041e3388
    (0xA41E4F30, 0x042E5F3A, "_hook_feof"),      # FUN_041e49b2, WAV parser
    (0xA41E51C8, 0x042E5F3A, "_hook_feof"),      # FUN_041e49b2, WAV parser
    (0xA419EC94, 0x042E5F3A, "_hook_feof"),      # FUN_0419eb06
    (0xA41B0738, 0x042F01E8, "_hook_send"),      # FUN_041b046c, message to the parser task
    (0xA41B0A1C, 0x042F01E8, "_hook_send"),      # FUN_041b0648, the same message on a second path
    (0xA41ADF68, 0x041AC360, "_hook_fill_policy"),  # read-ahead policy, after each file-task reply
    (0xA41AE2C0, 0x041AC360, "_hook_fill_policy"),  # read-ahead policy, idle poll
    (0xA41AF38C, 0x041AC360, "_hook_fill_policy"),  # read-ahead policy, after a backward fill
]


def off(addr: int) -> int:
    """Image offset of an address in any alias (0x04..., 0x84..., 0xa4...)."""
    o = (addr & 0x1FFFFFFF) - upd.APP_BASE
    if not 0 <= o < 0x3C0000:
        raise ValueError(f"{addr:#x} is outside the application image")
    return o


def put(app: bytearray, addr: int, old: bytes, new: bytes) -> None:
    """Replace old with new at addr. Refuse if the stock bytes are not old."""
    o = off(addr)
    if app[o:o + len(old)] != old:
        raise ValueError(f"{addr:#x}: expected {old!r}, found {bytes(app[o:o + len(old)])!r}")
    if len(new) != len(old):
        raise ValueError("replacement must keep the length")
    app[o:o + len(new)] = new


def redirect(app: bytearray, slot: int, old_target: int, new_target: int) -> None:
    """Point one literal-pool word at new_target. Keep the address alias of
    the stock word. Refuse if the word does not hold old_target."""
    o = off(slot)
    word = int.from_bytes(app[o:o + 4], "little")
    if word & 0x1FFFFFFF != old_target & 0x1FFFFFFF:
        raise ValueError(f"{slot:#x}: expected {old_target:#x}, found {word:#x}")
    new = (word & 0xE0000000) | (new_target & 0x1FFFFFFF)
    app[o:o + 4] = new.to_bytes(4, "little")


def load_stock() -> tuple[bytes, bytearray]:
    data = STOCK_UPD.read_bytes()
    if not hashlib.sha256(data).hexdigest().startswith(STOCK_SHA256):
        raise ValueError("C900MAIN.UPD is not the v4.32 file this tool was written for")
    flash = upd.decode_upd(data)
    app = bytearray(upd.unpack_region(flash, upd.APP_REGION))
    if not upd.check_image_sum(app):
        raise ValueError("stock image sum mismatch")
    return flash, app


def symbols(path: Path) -> dict[str, int]:
    out = {}
    for line in path.read_text().splitlines():
        addr, _, name = line.split()
        out[name] = int(addr, 16)
    return out


def check_padding_free(app: bytearray, start: int, end: int) -> None:
    if any(b != 0xFF for b in app[off(start):off(end)]):
        raise ValueError(f"padding {start:#x}..{end:#x} is not free")


def add_console_command(app: bytearray, name: str, argc: int, radix: int, handler: int) -> None:
    """Copy the command table into the padding with one entry added before ZZ."""
    add_console_commands(app, [(name, argc, radix, handler)])


def add_console_commands(app: bytearray, commands: list[tuple[str, int, int, int]]) -> None:
    """Copy the command table into the padding with the entries
    (name, argc, radix, handler) added before ZZ."""
    o = off(CMD_TABLE)
    entries = []
    while True:
        entry = bytes(app[o:o + 8])
        entries.append(entry)
        o += 8
        if entry[:2] == b"ZZ":
            break
        if len(entries) > 64:
            raise ValueError("no ZZ entry in the command table")
    added = b""
    for name, argc, radix, handler in commands:
        new = name[1].encode() + name[0].encode() + bytes([argc, radix]) + handler.to_bytes(4, "little")
        if any(e[:2] == new[:2] for e in entries) or new[:2] in added[::8]:
            raise ValueError(f"command {name} already exists")
        added += new
    table = b"".join(entries[:-1]) + added + entries[-1]
    check_padding_free(app, NEW_CMD_TABLE, NEW_CMD_TABLE + len(table))
    app[off(NEW_CMD_TABLE):off(NEW_CMD_TABLE) + len(table)] = table
    put(app, CMD_TABLE_SLOT, CMD_TABLE.to_bytes(4, "little"), (0xA0000000 | NEW_CMD_TABLE).to_bytes(4, "little"))


def write(name: str, flash: bytes, app: bytes, version: str) -> Path:
    new_flash = upd.build_flash(flash, app)
    if upd.unpack_region(new_flash, upd.APP_REGION) != app:
        raise AssertionError("packed region does not unpack to the patched image")
    file = upd.encode_upd(new_flash, version, len(new_flash))
    # Check the file the way the updater will see it.
    check = upd.decode_upd(file)
    if not upd.check_image_sum(upd.unpack_region(check, upd.APP_REGION)):
        raise AssertionError("image sum wrong in the output")
    if check[:upd.APP_REGION] != flash[:upd.APP_REGION]:
        raise AssertionError("boot ROM or loader changed")
    out = ROOT / "out" / name / "C900MAIN.UPD"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(file)
    # The same flash image, for the emulator's -bios.
    (out.parent / "main-firmware.bin").write_bytes(check)
    print(f"{out}  {len(file)} bytes  header {version}  "
          f"flash image ends {len(new_flash):#x}  "
          f"sha256 {hashlib.sha256(file).hexdigest()[:16]}")
    return out


def hello() -> None:
    flash, app = load_stock()
    put(app, VERSION_ADDR, b"4.32\0", b"4.33\0")
    put(app, DATE_ADDR, b"20140325\0", b"20261008\0")
    put(app, PAD_START + 8, b"\xff" * len(MARKER), MARKER)
    upd.fix_image_sum(app)
    write("hello", flash, bytes(app), "4.33")


def fltest() -> None:
    """Version 4.34 with the console command "ADDR,LEN,FL" (FLAC decode self-test)."""
    flash, app = load_stock()
    blob = (ROOT / "build/blob.bin").read_bytes()
    syms = symbols(ROOT / "build/blob.sym")
    if syms["_cmd_fl"] != BLOB_ADDR:
        raise ValueError("cmd_fl is not at the start of the blob; check src/link.ld")
    check_padding_free(app, BLOB_ADDR, BLOB_ADDR + len(blob))
    if BLOB_ADDR + len(blob) > PAD_END:
        raise ValueError("blob does not fit in the padding")
    app[off(BLOB_ADDR):off(BLOB_ADDR) + len(blob)] = blob
    add_console_command(app, "FL", 2, 16, syms["_cmd_fl"])
    put(app, VERSION_ADDR, b"4.32\0", b"4.34\0")
    put(app, DATE_ADDR, b"20140325\0", b"20261008\0")
    upd.fix_image_sum(app)
    write("fltest", flash, bytes(app), "4.34")


def flac(hooks: bool = True, name: str = "flac", version: str = "4.35", enabled: bool = True,
         stats: bool = False, release: bool = False) -> None:
    """The FLAC hooks and the console commands "N,VW" and "N,VP".
    hooks=False: the blob and the commands are in the image, but no pool word
    changes (a reference for the VP regression check).
    enabled=False: the hooks are in place, but FLAC registration is off
    (stage-1 deck build).
    stats=True: debug build; each FLAC track close writes C:/FLSTATn.TXT.
    release=True: the release blob (build/flac-release.*): hooks only, no
    console commands, no statistics."""
    flash, app = load_stock()
    blob_name = "flac-release" if release else "flac"
    blob = (ROOT / f"build/{blob_name}.bin").read_bytes()
    syms = symbols(ROOT / f"build/{blob_name}.sym")
    if not release and syms["_cmd_vw"] != BLOB_ADDR:
        raise ValueError("cmd_vw is not at the start of the blob; check src/flac.ld")
    if release and min(syms.values()) != BLOB_ADDR:
        raise ValueError("the release blob does not start at BLOB_ADDR; check src/flac-release.ld")
    if BLOB_ADDR + len(blob) > PAD_END:
        raise ValueError("blob does not fit in the padding")
    check_padding_free(app, BLOB_ADDR, BLOB_ADDR + len(blob))
    app[off(BLOB_ADDR):off(BLOB_ADDR) + len(blob)] = blob
    for slot, old, sym in FLAC_HOOKS if hooks else []:
        redirect(app, slot, old, syms[sym])
    if stats:
        put(app, syms["_vh_stats_enabled"], (0).to_bytes(4, "little"), (1).to_bytes(4, "little"))
    if not enabled:
        put(app, syms["_vh_flac_enabled"], (1).to_bytes(4, "little"), (0).to_bytes(4, "little"))
    if not release:
        add_console_commands(app, [("VW", 1, 16, syms["_cmd_vw"]), ("VP", 1, 16, syms["_cmd_vp"]),
                                   ("TK", 1, 16, syms["_cmd_tk"])])
    put(app, VERSION_ADDR, b"4.32\0", version.encode() + b"\0")
    put(app, DATE_ADDR, b"20140325\0", b"20261008\0")
    upd.fix_image_sum(app)
    write(name, flash, bytes(app), version)


def rollback() -> None:
    flash, app = load_stock()
    write("rollback", flash, bytes(app), "4.99")


if __name__ == "__main__":
    {"hello": hello, "fltest": fltest, "flac": flac,
     "flacref": lambda: flac(hooks=False, name="flacref"),
     "deck1": lambda: flac(name="deck1", version="4.36", enabled=False),
     # deck2 (4.37) was the first FLAC build on the deck. deck3 adds dr_flac's
     # CRC code, which turns on its binary search seek (reverse play fix).
     "deck3": lambda: flac(name="deck3", version="4.38"),
     # deck4: deck3 plus the debug statistics file (slow-UI investigation).
     "deck4": lambda: flac(name="deck4", version="4.39", stats=True),
     # deck5: deck4 plus the dr_flac seek fix (false frame header in the binary search).
     "deck5": lambda: flac(name="deck5", version="4.40", stats=True),
     # deck6: deck5 plus the read-ahead throttle (VH_THROTTLE_AFTER_S, VH_MIN_FILL_RATE).
     "deck6": lambda: flac(name="deck6", version="4.41", stats=True),
     # deck7: deck6 plus the FLAC read-ahead cap (hook_fill_policy, VH_AHEAD_CAP).
     "deck7": lambda: flac(name="deck7", version="4.42", stats=True),
     # release: the hooks only (no console commands, no statistics). 4.43 added
     # the damaged-frame resume, no brute force seek, and faster memcpy/convert.
     "release": lambda: flac(name="release", version="4.44", release=True),
     "rollback": rollback}[sys.argv[1]]()
