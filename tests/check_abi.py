#!/usr/bin/env python3
"""Check two calling-convention rules in the FLAC blob sources.

1. The firmware uses the Renesas calling convention, so MACH and MACL are
   callee-saved. Each function that changes MACH or MACL must save MACL.
   (mul.l and the 16-bit multiplies change only MACL.)
2. The firmware runs the FPU in single-precision mode. Our code must not
   use the FPU or change FPSCR.

The check reads the compiler's assembler output, not a disassembly, so
literal-pool data is never read as an instruction. libgcc is not checked:
it comes from the m4-nofpu multilib, and the decoder calls it only through
call_with_stack, which saves MACH and MACL.
Usage: check_abi.py CC FLAGS... -- SOURCES...
"""
import re
import subprocess
import sys

MAC_WRITE = re.compile(r"^\t(mul\.l|muls\.w|mulu\.w|dmuls\.l|dmulu\.l|mac\.[lw]|clrmac"
                       r"|lds(\.l)?\s+\S+,mac[hl])\b")
MAC_SAVE = re.compile(r"^\tsts\.l\s+macl,@-r15")
FPU = re.compile(r"^\t(f[a-z]+(\.[a-z]+)?\s|(sts|lds)(\.l)?\s+\S*fp(scr|ul))")

sep = sys.argv.index("--")
cc, sources = sys.argv[1:sep], sys.argv[sep + 1:]
bad_mac, bad_fpu = [], []
for src in sources:
    asm = subprocess.run(cc + ["-S", "-o", "-", src], capture_output=True, text=True,
                         check=True).stdout
    name, writes, saves = None, False, False
    for line in asm.splitlines():
        m = re.match(r"^(_[A-Za-z0-9_.$]+):", line)
        if m and not m.group(1).startswith("_.") and not line.startswith("_L"):
            if name and writes and not saves:
                bad_mac.append(name)
            name, writes, saves = m.group(1), False, False
            continue
        if MAC_WRITE.match(line):
            writes = True
        if MAC_SAVE.match(line):
            saves = True
        if FPU.match(line):
            bad_fpu.append(f"{name}: {line.strip()}")
    if name and writes and not saves:
        bad_mac.append(name)
print("functions that change MACL without a save:", ", ".join(bad_mac) or "none")
print("FPU or FPSCR instructions:", "; ".join(bad_fpu[:10]) or "none")
sys.exit(1 if bad_mac or bad_fpu else 0)
