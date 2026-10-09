"""Sample PC/PR of a running emulator through its HMP monitor socket."""
import collections, re, socket, sys, time

def connect(path):
    s = socket.socket(socket.AF_UNIX); s.connect(path); s.settimeout(0.2)
    return s

def cmd(s, c):
    s.sendall((c + "\n").encode()); time.sleep(0.1); out = b""
    try:
        while True:
            d = s.recv(65536)
            if not d: break
            out += d
    except socket.timeout:
        pass
    return re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", out.decode(errors="replace"))

if __name__ == "__main__":
    s = connect(sys.argv[1]); n = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    cmd(s, "")
    pcs = collections.Counter()
    for i in range(n):
        r = cmd(s, "info registers")
        pc = re.search(r"\bpc=0x([0-9a-f]+)", r); pr = re.search(r"\bpr=0x([0-9a-f]+)", r)
        if pc: pcs[(pc.group(1), pr.group(1) if pr else "?")] += 1
        time.sleep(0.5)
    for (pc, pr), k in pcs.most_common(20): print(f"pc={pc} pr={pr} x{k}")
