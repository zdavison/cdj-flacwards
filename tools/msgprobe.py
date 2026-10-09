"""Log task messages on a running emulator over QEMU's gdbstub.

    python3 tools/msgprobe.py GDBPORT SECONDS ADDR[:REG] ...

Breaks at each ADDR, reads the message pointer from register REG (default 4)
and prints the code (+8) and the arguments at +0x24..+0x30. Repeated lines are
counted. Use few, cold addresses: a hot breakpoint slows the firmware.
"""
import struct, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "emu/cdj2000-emulator"))
from tools.cdj_main.gdbprobe import Rsp, REG_PC
port, secs = int(sys.argv[1]), float(sys.argv[2])
spec = {}
for a in sys.argv[3:]:
    addr, _, reg = a.partition(":"); spec[int(addr, 16)] = int(reg or 4)
rsp = Rsp(port); rsp.interrupt(); rsp.packet()
for a in spec: rsp.point(0, a, 2)
t0 = time.time(); end = t0 + secs; last = None; rep = 0
while time.time() < end:
    rsp.resume()
    stop = rsp.packet(timeout=max(0.1, end - time.time()))
    if not stop:
        rsp.interrupt(); rsp.packet(); break
    r = rsp.regs(); pc = r[REG_PC]; m = r[spec.get(pc, 4)]
    raw = rsp.read(m, 0x34)
    if len(raw) == 0x34:
        w = struct.unpack("<13I", raw)
        line = f"{pc:#x} code={w[2]:#x} a24={w[9]:#x} a28={w[10]:#x} a2c={w[11]:#x} a30={w[12]:#x}"
    else:
        line = f"{pc:#x} r={m:#x}"
    if line == last: rep += 1
    else:
        if rep: print(f"   (x{rep + 1})")
        print(f"t={time.time()-t0:5.1f} {line}", flush=True); last = line; rep = 0
    rsp.point(0, pc, 2, on=False); rsp.cmd("s"); rsp.point(0, pc, 2)
if rep: print(f"   (x{rep + 1})")
for a in spec: rsp.point(0, a, 2, on=False)
rsp.resume()
