"""Count breakpoint hits on a running emulator over QEMU's gdbstub.

    python3 tools/probe.py GDBPORT SECONDS ADDR[:REG] ...

ADDR:REG also records register REG (0..15) at each hit. Uses the remote
protocol client from cdj2000-emulator (tools/cdj_main/gdbprobe.py).
"""
import collections
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "emu/cdj2000-emulator"))
from tools.cdj_main.gdbprobe import Rsp, REG_PC  # noqa: E402


def main() -> int:
    port, seconds = int(sys.argv[1]), float(sys.argv[2])
    points = []
    for spec in sys.argv[3:]:
        addr, _, reg = spec.partition(":")
        points.append((int(addr, 16), int(reg) if reg else None))
    rsp = Rsp(port)
    rsp.interrupt()
    rsp.packet()
    for addr, _ in points:
        rsp.point(0, addr, 2)
    hits = collections.Counter()
    first = {}
    end = time.time() + seconds
    while time.time() < end:
        rsp.resume()
        stop = rsp.packet(timeout=max(0.1, end - time.time()))
        if not stop:
            rsp.interrupt()
            rsp.packet()
            break
        regs = rsp.regs()
        pc = regs[REG_PC]
        reg = dict(points).get(pc)
        key = (pc, regs[reg] if reg is not None else None)
        hits[key] += 1
        first.setdefault(key, time.time())
        rsp.point(0, pc, 2, on=False)
        rsp.cmd("s")
        rsp.point(0, pc, 2)
    for addr, _ in points:
        rsp.point(0, addr, 2, on=False)
    rsp.resume()
    for (pc, val), n in sorted(hits.items()):
        print(f"{pc:#010x}" + (f" reg={val:#x}" if val is not None else "") + f"  hits {n}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
