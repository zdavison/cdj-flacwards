#!/usr/bin/env python3
"""PC tests for vwav and vfs_hook. Run them with `make test`."""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_testdata as td  # noqa: E402

ROOT = td.ROOT
DATA = td.OUT
HOST = ROOT / "build/host_vwav"
failures: list[str] = []


def check(name: str, cond: bool, detail: str = "") -> None:
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  " + detail.strip()))
    if not cond:
        failures.append(name)


def dump(name: str):
    out = DATA / f"{name}.out.wav"
    out.unlink(missing_ok=True)
    r = subprocess.run([str(HOST), "dump", str(DATA / f"{name}.flac"), str(out)],
                       capture_output=True, text=True, timeout=300)
    got = out.read_bytes() if out.exists() else b""
    return r, got


def test_valid() -> None:
    for name in td.VALID:
        r, got = dump(name)
        ref = (DATA / f"{name}.ref.wav").read_bytes()
        check(f"dump {name}", r.returncode == 0 and got == ref, r.stdout + r.stderr)


def test_reject() -> None:
    for name, code in td.REJECT.items():
        r, _ = dump(name)
        check(f"reject {name}", f"open={code}" in r.stdout, r.stdout + r.stderr)


def test_damaged() -> None:
    ref = (DATA / "t16_44.ref.wav").read_bytes()
    pcm = len(ref) - 44
    r, got = dump("truncated")
    check("truncated length", len(got) == len(ref), r.stdout + r.stderr)
    check("truncated head", got[:44 + pcm // 2] == ref[:44 + pcm // 2])
    tail = pcm * 3 // 10
    check("truncated tail is silence", got[-tail:] == bytes(tail))
    r, got = dump("corrupt_mid")
    check("corrupt_mid length", len(got) == len(ref), r.stdout + r.stderr)
    head = 44 + pcm * 45 // 100
    check("corrupt_mid head", got[:head] == ref[:head])
    # Silence for the damaged frames only, then the audio continues: the last
    # 45 % equals the reference, and at most 1 s (176400 bytes) differs.
    check("corrupt_mid tail", got[-(pcm * 45 // 100):] == ref[-(pcm * 45 // 100):])
    differ = sum(1 for i in range(44, len(ref), 4) if got[i:i + 4] != ref[i:i + 4]) * 4
    check(f"corrupt_mid gap ({differ} bytes differ)", 0 < differ <= 176400)


def test_jumps() -> None:
    for name in td.VALID:
        r = subprocess.run([str(HOST), "check", str(DATA / f"{name}.flac"),
                            str(DATA / f"{name}.ref.wav")],
                           capture_output=True, text=True, timeout=600)
        line = r.stdout.strip()
        check(f"jumps {name}", r.returncode == 0 and line.startswith("VW open=0 ")
              and "/0 jumps=" in line and line.endswith("/0"), r.stdout + r.stderr)


def test_vfs_hook() -> None:
    r = subprocess.run([str(ROOT / "build/test_vfs_hook"), str(DATA / "t16_44.flac"),
                        str(DATA / "t16_44.ref.wav"), str(DATA / "notflac.flac")],
                       capture_output=True, text=True, timeout=300)
    check("vfs_hook", r.returncode == 0, r.stdout + r.stderr)


def test_open_abi() -> None:
    """hook_open called the firmware's way, on SH-4 code (qemu-sh4-static)."""
    if os.environ.get("CDJ_HOST_ONLY"):
        print("skip hook_open ABI (CDJ_HOST_ONLY)")
        return
    r = subprocess.run(["qemu-sh4-static", str(ROOT / "build/abi_open")],
                       capture_output=True, text=True, timeout=60)
    check("hook_open ABI", r.returncode == 0, f"exit {r.returncode} {r.stderr}")


def test_reverse() -> None:
    """Reverse play reads 0xf000-byte blocks from the end to the start. Each
    block needs a backward seek. The bytes must be right, and the file data
    read must stay below 4 times the file size (the deck stuttered at about
    23 times)."""
    for name in ("t24_48", "t16_48_noseek"):
        r = subprocess.run([str(HOST), "reverse", str(DATA / f"{name}.flac"),
                            str(DATA / f"{name}.ref.wav"), "0xf000"],
                           capture_output=True, text=True, timeout=300)
        f = dict(kv.split("=") for kv in r.stdout.split()[1:]) if r.returncode == 0 else {}
        ratio = int(f["read"]) / int(f["file"]) if f else 99
        check(f"reverse {name} ({ratio:.1f}x file)", r.returncode == 0 and ratio < 4,
              r.stdout + r.stderr)


def test_seek_cost() -> None:
    """Seek to 1999 targets, each from a fresh open. No seek may read more than
    512 KB of the file. A seek that hits a false frame header made dr_flac fall
    back to a decode from the start of the file (1.7 MB here, 10.5 MB and
    3.7 s on the deck)."""
    total = 120 * 44100
    args = []
    for i in range(1, 2000):
        args += [str(44 + (i * total // 2000) * 4), "4"]
    r = subprocess.run([str(HOST), "probe", str(DATA / "long_noseek.flac")] + args,
                       capture_output=True, text=True, timeout=600)
    worst = max((int(l.split()[2]), int(l.split()[0])) for l in r.stdout.splitlines() if l)
    check(f"seek cost (worst {worst[0] // 1024} KB at {worst[1]})",
          r.returncode == 0 and worst[0] < 512 * 1024, r.stderr)


def test_rt() -> None:
    r = subprocess.run([str(ROOT / "build/test_rt")], capture_output=True, text=True, timeout=60)
    check("rt memcpy/memset", r.returncode == 0, r.stdout)


TESTS = [test_valid, test_reject, test_damaged, test_jumps, test_vfs_hook, test_open_abi,
         test_reverse, test_seek_cost, test_rt]

if __name__ == "__main__":
    td.make_all()
    for test in TESTS:
        test()
    print(f"{len(failures)} failures")
    sys.exit(1 if failures else 0)
