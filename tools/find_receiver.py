"""Break where GUI list requests are posted, then watch who reads the message code.

    python3 tools/find_receiver.py GDBPORT SECONDS
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "emu/cdj2000-emulator"))
from tools.cdj_main.gdbprobe import Rsp, REG_PC, REG_PR  # noqa: E402

POST = 0x04216B28  # jsr send(mbx=r4, msg=r5) in the list-request poster


def main() -> int:
    port, seconds = int(sys.argv[1]), float(sys.argv[2])
    rsp = Rsp(port)
    rsp.interrupt(); rsp.packet()
    rsp.point(0, POST, 2)
    end = time.time() + seconds
    rsp.resume()
    stop = rsp.packet(timeout=seconds)
    if not stop:
        print("no list request posted"); return 1
    regs = rsp.regs()
    msg = regs[5]
    print(f"post at {regs[REG_PC]:#x}: mailbox id {regs[4]:#x}, message {msg:#x}, code word {rsp.word(msg + 8)}")
    rsp.point(0, POST, 2, on=False)
    rsp.point(3, msg + 8, 4)          # read watchpoint on the code
    for _ in range(6):
        rsp.resume()
        stop = rsp.packet(timeout=max(0.1, end - time.time()))
        if not stop:
            break
        r = rsp.regs()
        print(f"read: pc={r[REG_PC]:#x} pr={r[REG_PR]:#x}  {stop}")
    rsp.point(3, msg + 8, 4, on=False)
    rsp.resume()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
