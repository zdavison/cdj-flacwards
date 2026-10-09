#!/usr/bin/env python3
"""A synthetic update file with the CDJ-900 layout, for tests without firmware.

    python3 tests/fake_firmware.py OUTDIR

The application image has random but compressible content, 0xff padding, the
stock values at the hook words, and the stock version and date strings. A
fake blob and fake symbols give a manifest for it. tools/build_patch.py then
makes the reference outputs.
"""
from __future__ import annotations

import hashlib
import io
import json
import random
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_patch as bp  # noqa: E402
import upd  # noqa: E402


def fake_upd(seed: int = 1) -> bytes:
    rnd = random.Random(seed)
    words = [rnd.getrandbits(32).to_bytes(4, "little") for _ in range(64)]
    app = bytearray(0x3C0000)
    pad = bp.off(bp.PAD_START)
    for o in range(0, pad, 4):
        app[o:o + 4] = words[rnd.randrange(64)]
    app[pad:bp.off(bp.PAD_END)] = b"\xff" * (bp.off(bp.PAD_END) - pad)
    for slot, old, _name in bp.FLAC_HOOKS:
        app[bp.off(slot):bp.off(slot) + 4] = old.to_bytes(4, "little")
    app[bp.off(bp.VERSION_ADDR):bp.off(bp.VERSION_ADDR) + 5] = b"4.32\0"
    app[bp.off(bp.DATE_ADDR):bp.off(bp.DATE_ADDR) + 9] = b"20140325\0"
    upd.fix_image_sum(app)
    boot = bytes(rnd.getrandbits(8) for _ in range(upd.APP_REGION))
    flash = upd.build_flash(boot, bytes(app))
    return upd.encode_upd(flash, "4.32", len(flash))


def fake_blob_and_syms() -> tuple[bytes, dict[str, int]]:
    blob = bytes((i * 37 + 11) & 0xFF for i in range(6000))
    names = sorted({name for _slot, _old, name in bp.FLAC_HOOKS})
    syms = {name: bp.BLOB_ADDR + 64 * i for i, name in enumerate(names)}
    return blob, syms


def main() -> int:
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    data = fake_upd()
    (out / "fake.upd").write_bytes(data)
    for name, method in (("fake.zip", zipfile.ZIP_DEFLATED), ("fake-stored.zip", zipfile.ZIP_STORED)):
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w", method) as z:
            z.writestr("CDJ-900v432/C900MAIN.UPD", data)
        (out / name).write_bytes(buf.getvalue())
    blob, syms = fake_blob_and_syms()
    m = bp.manifest_data(blob, syms, sha256=hashlib.sha256(data).hexdigest())
    (out / "fake-manifest.json").write_text(json.dumps(m))
    patched, rollback = bp.apply_manifest(data, m)
    (out / "fake-patched.upd").write_bytes(patched)
    (out / "fake-rollback.upd").write_bytes(rollback)
    bad = dict(m, free=[[bp.PAD_START - 16, bp.PAD_START + 16]])
    (out / "bad-padding-manifest.json").write_text(json.dumps(bad))
    words = [list(w) for w in m["words"]]
    words[0][1] ^= 1
    (out / "bad-word-manifest.json").write_text(json.dumps(dict(m, words=words)))
    print(f"{out}: fake firmware and references written")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
