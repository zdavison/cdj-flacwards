#!/bin/sh
# Emulator update test: boot FROM_FLASH in update mode (RELOOP and USB held),
# install UPD from a USB stick image, save the flash, and compare the
# unpacked application with the stock 4.32 application.
#   tools/update-test.sh FROM_FLASH UPD
# Needs the emulator (tools/build-emulator.sh) and the firmware. It stops any
# running emulator (tools/emu.sh does). EMU_DIR is the cdj2000-emulator clone
# (default emu/cdj2000-emulator).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
EMU_DIR=${EMU_DIR:-$ROOT/emu/cdj2000-emulator}
from=$(realpath "$1"); file=$(realpath "$2")
run=$ROOT/runs/update-test
rm -rf "$run"; mkdir -p "$run/stick"
cp "$file" "$run/stick/C900MAIN.UPD"
(cd "$EMU_DIR" && "$ROOT/build/venv/bin/python" -m tools.cdj_main.make_sd_image \
    "$run/stick" "$run/stick.img" --size 128M >/dev/null)
cd "$ROOT/runs"
CDJ_PANEL_FRAME=00000000000000000000000000000000040000020000 "$ROOT/tools/emu.sh" update-test "$from" 900 \
    -drive if=none,id=usbstick,format=raw,file="$run/stick.img" \
    -device usb-storage,drive=usbstick,removable=on
python3 - "$run" <<'PY'
import socket, sys, time
run = sys.argv[1]
s = None
for _ in range(100):
    try:
        s = socket.create_connection(("127.0.0.1", 5557)); break
    except OSError:
        time.sleep(0.5)
s.settimeout(1); out = b""; end = time.time() + 600
while time.time() < end and b"Update END" not in out:
    try:
        out += s.recv(65536)
    except socket.timeout:
        pass
print("updater:", "Update END" if b"Update END" in out else "no marker")
m = socket.socket(socket.AF_UNIX); m.connect(run + "/mon.sock"); time.sleep(0.5); m.recv(4096)
m.sendall(b"pmemsave 0 0x400000 fa.bin\n"); time.sleep(3)
PY
pkill -f "[q]emu-system-sh4 -M cdj2000-main" || true
python3 - "$run/fa.bin" <<'PY'
import sys
sys.path.insert(0, "../tools")
import build_patch as bp, upd
flash = open(sys.argv[1], "rb").read()
_, stock = bp.load_stock()
app = upd.unpack_region(flash, upd.APP_REGION)
same = app == bytes(stock)
print("result equals the stock 4.32 application:", same)
sys.exit(0 if same else 1)
PY
