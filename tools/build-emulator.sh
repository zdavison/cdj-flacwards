#!/bin/sh
# Build qemu-system-sh4 with the cdj2000-emulator board into build/qemu.
# QEMU is pinned to v11.1.0: the emulator's QEMU patches were made against it
# (target/sh4/translate.c blob 373950f), and later QEMU removed use_exit_tb().
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
QEMU_TAG=v11.1.0
[ -d "$ROOT/emu/cdj2000-emulator" ] || git clone https://github.com/cdj2k-revival/cdj2000-emulator "$ROOT/emu/cdj2000-emulator"
if [ ! -d "$ROOT/build/qemu" ] || [ "$(git -C "$ROOT/build/qemu" describe --tags 2>/dev/null)" != "$QEMU_TAG" ]; then
    rm -rf "$ROOT/build/qemu"
    git clone --depth 1 --branch "$QEMU_TAG" https://gitlab.com/qemu-project/qemu.git "$ROOT/build/qemu"
fi
[ -x "$ROOT/build/venv/bin/ninja" ] || { python3 -m venv "$ROOT/build/venv"; "$ROOT/build/venv/bin/pip" install -q ninja meson; }
# The first emulator patch fails only on its trace-events context in v11.1.0
# (one added line), so apply the whole stack here, the first with fuzz.
# build-qemu-sh4.sh treats every patch before the last applied one as applied.
P=$ROOT/emu/cdj2000-emulator/patches
if ! grep -q sh_intc_prio "$ROOT/build/qemu/hw/intc/trace-events"; then
    patch -d "$ROOT/build/qemu" -p1 -F3 --forward < "$P/qemu-sh-intc-priority-imask.patch"
    patch -d "$ROOT/build/qemu" -p1 --forward < "$P/qemu-sh-intc-priority-order.patch"
    patch -d "$ROOT/build/qemu" -p1 --forward < "$P/qemu-sh-tmu-stop-reset.patch"
fi
PATH=$ROOT/build/venv/bin:$PATH sh "$ROOT/emu/cdj2000-emulator/scripts/build-qemu-sh4.sh" "$ROOT/build/qemu"
