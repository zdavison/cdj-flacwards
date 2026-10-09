"""Talk to the firmware's debug console (third SCIF) over TCP."""
import socket, sys, time

def session(port, commands, wait=1.5, raw_path="console.raw"):
    s = socket.create_connection(("127.0.0.1", port)); s.settimeout(0.3)
    raw = open(raw_path, "ab")
    def drain(t):
        end = time.time() + t; out = b""
        while time.time() < end:
            try:
                d = s.recv(65536)
                if d:
                    out += d
                    raw.write(d)
            except socket.timeout:
                pass
        # The firmware writes Japanese messages in Shift-JIS (cp932).
        return out.decode("cp932", errors="replace")
    print(drain(wait), end="")
    for c in commands:
        s.sendall(c.encode() + b"\r")
        print(f"\n>>> {c}\n" + drain(wait), end="")
    s.close()

if __name__ == "__main__":
    session(int(sys.argv[1]), sys.argv[2:])
