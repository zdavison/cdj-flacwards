#!/usr/bin/env python3
"""Tests of the manifest path in tools/build_patch.py, on the fake firmware.

    python3 tests/test_manifest.py build/webtest
"""
import base64
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_patch as bp  # noqa: E402
import upd  # noqa: E402

D = Path(sys.argv[1])
failures = 0


def check(name, cond, detail=""):
    global failures
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  " + detail))
    if not cond:
        failures += 1


def raises(fn):
    try:
        fn()
    except ValueError:
        return True
    return False


data = (D / "fake.upd").read_bytes()
m = json.loads((D / "fake-manifest.json").read_text())
patched, rollback = bp.apply_manifest(data, m)

# The patched file: valid, version 4.44, the edits in place, boot area unchanged.
flash = upd.decode_upd(patched)
app = upd.unpack_region(flash, upd.APP_REGION)
check("patched header", patched[:upd.HEADER_SIZE] == upd.header("4.44"))
check("patched image sum", upd.check_image_sum(app))
o = bp.off(m["blob_addr"])
blob = base64.b64decode(m["blob"])
check("blob in place", app[o:o + len(blob)] == blob)
check("words in place", all(int.from_bytes(app[bp.off(a):bp.off(a) + 4], "little") == new
                            for a, _old, new in m["words"]))
check("version string", app[bp.off(bp.VERSION_ADDR):bp.off(bp.VERSION_ADDR) + 5] == b"4.44\0")
check("boot area unchanged", flash[:upd.APP_REGION] == upd.decode_upd(data)[:upd.APP_REGION])

# The rollback file: only the header version and the CRC differ.
check("rollback header", rollback[:upd.HEADER_SIZE] == upd.header("4.99"))
check("rollback body unchanged", rollback[upd.HEADER_SIZE:-2] == data[upd.HEADER_SIZE:-2])
check("rollback CRC", upd.crc16_xmodem(rollback[:-2]) == int.from_bytes(rollback[-2:], "little"))
check("rollback decodes to the stock flash", upd.decode_upd(rollback) == upd.decode_upd(data))

# Guards.
check("wrong file refused", raises(lambda: bp.apply_manifest(patched, m)))
check("wrong stock word refused",
      raises(lambda: bp.apply_manifest(data, json.loads((D / "bad-word-manifest.json").read_text()))))
check("padding not free refused",
      raises(lambda: bp.apply_manifest(data, json.loads((D / "bad-padding-manifest.json").read_text()))))
check("manifest hash is the fake file's hash", m["upd_sha256"] == hashlib.sha256(data).hexdigest())

print(f"{failures} failures")
sys.exit(1 if failures else 0)
