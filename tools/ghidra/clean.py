"""Remove Ghidra's FPSCR bookkeeping from SH-4 decompiler output (stdin to stdout).

    clean.py           decomp.sh output
    clean.py --file    one file of DecompileAll.java output
"""
import re
import sys

text = sys.stdin.read()
# --file: the input is a DecompileAll.java file; keep its "// =====" headers.
# Without it, the input is decomp.sh output; drop the Ghidra log before the C.
if "--file" not in sys.argv[1:]:
    start = re.search(r"^(void|undefined|int|uint|char|byte|bool|code)\S* \S*FUN_", text, re.M)
    text = text[start.start():] if start else text
out, stmt = [], []
for line in text.splitlines():
    stmt.append(line)
    s = line.strip()
    if s.endswith(";") or s.endswith("{") or s.endswith("}") or s == "" or s.endswith(":"):
        block = "\n".join(stmt)
        stmt = []
        if "FPSCR" in block or re.match(r"\s*bVar\d+ = \(byte\)\(?\(?uVar\d+", block) or re.match(r"\s*byte bVar\d+;", block):
            continue
        if re.match(r"\s*uVar\d+ = \(\(uint\)bVar\d+ \|", block):
            continue
        out.append(block)
    if line.startswith(("INFO", "WARN", "openjdk", "OpenJDK")):
        stmt = []
sys.stdout.write("\n".join(out) + "\n")
