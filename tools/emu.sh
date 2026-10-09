#!/bin/sh
# Start the CDJ-900 MAIN firmware in the emulator, in the background.
#   tools/emu.sh RUNDIR FLASH.bin SECONDS [extra qemu args...]
# Console (SCIF0): tcp 127.0.0.1:5557. Monitor: RUNDIR/mon.sock.
# Any earlier instance is stopped first, so the console port is free.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
run=$1; flash=$(realpath "$2"); secs=$3; shift 3
pkill -f "[q]emu-system-sh4 -M cdj2000-main" 2>/dev/null || true
while pgrep -f "[q]emu-system-sh4 -M cdj2000-main" >/dev/null; do sleep 0.2; done
mkdir -p "$run"
rm -f "$run/mon.sock" "$run/console.raw"
cd "$run"
CDJ_TMU_FREQ=54000000 setsid timeout "$secs" "$ROOT/build/qemu/build/qemu-system-sh4" \
    -M cdj2000-main -bios "$flash" -display none -no-reboot -D qemu.log \
    -serial file:scif-a.log -serial file:scif-b.log \
    -serial tcp:127.0.0.1:5557,server,nowait \
    -monitor unix:mon.sock,server,nowait "$@" > stdout.log 2> stderr.log &
until [ -S mon.sock ]; do sleep 0.2; done
echo "emulator started in $run"
