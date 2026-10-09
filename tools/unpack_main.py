#!/usr/bin/env python3
"""Unpack the CDJ-900 MAIN firmware 4.32 for local analysis.

    python3 tools/unpack_main.py CDJ-900v432.zip   (or C900MAIN.UPD)

Writes:
    firmware/CDJ-900v432/*.UPD          the update files (from the zip)
    firmware/unpacked/main-firmware.bin the flash image (emulator -bios)
    firmware/unpacked/main-unpacked.bin the application image at 0x04000000

All output is derived from Pioneer firmware. It stays local: firmware/ is in
.gitignore. Get the update package from Pioneer DJ's support site.
"""
from __future__ import annotations

import hashlib
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_patch  # noqa: E402
import upd  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
UPD_DIR = ROOT / "firmware/CDJ-900v432"
OUT = ROOT / "firmware/unpacked"


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    src = Path(sys.argv[1])
    UPD_DIR.mkdir(parents=True, exist_ok=True)
    if zipfile.is_zipfile(src):
        with zipfile.ZipFile(src) as z:
            for info in z.infolist():
                name = Path(info.filename).name
                if name.upper().endswith(".UPD"):
                    (UPD_DIR / name).write_bytes(z.read(info))
        main_upd = (UPD_DIR / "C900MAIN.UPD").read_bytes()
    else:
        main_upd = src.read_bytes()
        (UPD_DIR / "C900MAIN.UPD").write_bytes(main_upd)
    if not hashlib.sha256(main_upd).hexdigest().startswith(build_patch.STOCK_SHA256):
        print("C900MAIN.UPD is not the 4.32 file that this project was made for", file=sys.stderr)
        return 1
    flash = upd.decode_upd(main_upd)
    app = upd.unpack_region(flash, upd.APP_REGION)
    if not upd.check_image_sum(app):
        print("image sum mismatch", file=sys.stderr)
        return 1
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "main-firmware.bin").write_bytes(flash)
    (OUT / "main-unpacked.bin").write_bytes(app)
    print(f"{OUT}/main-firmware.bin {len(flash)} bytes, main-unpacked.bin {len(app)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
