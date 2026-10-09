#!/usr/bin/env python3
"""Local test (needs firmware/CDJ-900v432/C900MAIN.UPD and build/flac-release.*):
the manifest path gives the same release file as the old direct path."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
if not (ROOT / "firmware/CDJ-900v432/C900MAIN.UPD").exists():
    print("skip: no firmware")
    sys.exit(0)
for target in ("release", "release-old", "rollback"):
    subprocess.run([sys.executable, str(ROOT / "tools/build_patch.py"), target], check=True)
new = (ROOT / "out/release/C900MAIN.UPD").read_bytes()
old = (ROOT / "out/release-old/C900MAIN.UPD").read_bytes()
stock = (ROOT / "firmware/CDJ-900v432/C900MAIN.UPD").read_bytes()
rb = (ROOT / "out/rollback/C900MAIN.UPD").read_bytes()
ok = new == old and rb[32:-2] == stock[32:-2] and rb[19:23] == b"4.99"
print("release paths identical:", new == old, "| rollback body is the official body:", rb[32:-2] == stock[32:-2])
sys.exit(0 if ok else 1)
