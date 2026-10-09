#!/usr/bin/env python3
"""Reference outputs from tools/upd.py for the JavaScript tests.
    pyref.py compress IN OUT | decompress IN OUT | encode IN OUT VERSION
    pyref.py imagesum IN     | header VERSION OUT | decode IN OUT
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import upd  # noqa: E402

cmd, args = sys.argv[1], sys.argv[2:]
rd = lambda p: Path(p).read_bytes()  # noqa: E731
if cmd == "compress":
    Path(args[1]).write_bytes(upd.lzss_compress(rd(args[0])))
elif cmd == "decompress":
    Path(args[1]).write_bytes(upd.lzss_decompress(rd(args[0])))
elif cmd == "encode":
    image = rd(args[0])
    Path(args[1]).write_bytes(upd.encode_upd(image, args[2], len(image)))
elif cmd == "decode":
    Path(args[1]).write_bytes(upd.decode_upd(rd(args[0])))
elif cmd == "imagesum":
    print(upd.image_sum(rd(args[0])))
elif cmd == "header":
    Path(args[1]).write_bytes(upd.header(args[0]))
else:
    sys.exit(f"unknown command {cmd}")
