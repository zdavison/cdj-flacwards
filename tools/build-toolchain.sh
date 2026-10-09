#!/bin/sh
# Build a C-only sh-elf GCC with libgcc into ./toolchain.
#
# The FLAC code runs freestanding inside the CDJ-900 firmware, so it needs no
# C library. That avoids the AUR sh-elf-gcc <-> sh-elf-newlib build cycle.
# Needs: gmp, mpfr, libmpc (and a C/C++ compiler). If sh-elf-as is not
# installed (for example sh-elf-binutils from the AUR), the script also builds
# binutils into ./toolchain first. CI uses that path.
set -eu

GCC_VER=15.2.0
GCC_SHA512=89047a2e07bd9da265b507b516ed3635adb17491c7f4f67cf090f0bd5b3fc7f2ee6e4cc4008beef7ca884b6b71dffe2bb652b21f01a702e17b468cca2d10b2de
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PREFIX=$ROOT/toolchain
WORK=${WORK:-$ROOT/build/toolchain}
TARBALL=$WORK/gcc-$GCC_VER.tar.xz

BINUTILS_VER=2.45.1
BINUTILS_SHA512=ea030419eba387579ab717be7e3223fc99e93b586860b06003c12489f93441640d4082736f76aa5e98233db4f46e232f536a45e471486de1f5b64e1b827c167e
mkdir -p "$WORK"

if [ ! -f "$TARBALL" ]; then
    if [ -f "$HOME/.cache/yay/sh-elf-gcc/gcc-$GCC_VER.tar.xz" ]; then
        cp "$HOME/.cache/yay/sh-elf-gcc/gcc-$GCC_VER.tar.xz" "$TARBALL"
    else
        curl -fL -o "$TARBALL" "https://gcc.gnu.org/pub/gcc/releases/gcc-$GCC_VER/gcc-$GCC_VER.tar.xz"
    fi
fi
echo "$GCC_SHA512  $TARBALL" | sha512sum -c -

[ -d "$WORK/gcc-$GCC_VER" ] || tar -C "$WORK" -xf "$TARBALL"

export PATH="$PREFIX/bin:$PATH"
if ! command -v sh-elf-as >/dev/null; then
    BU_TAR=$WORK/binutils-$BINUTILS_VER.tar.xz
    [ -f "$BU_TAR" ] || curl -fL -o "$BU_TAR" "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VER.tar.xz"
    echo "$BINUTILS_SHA512  $BU_TAR" | sha512sum -c -
    [ -d "$WORK/binutils-$BINUTILS_VER" ] || tar -C "$WORK" -xf "$BU_TAR"
    rm -rf "$WORK/binutils-build"
    mkdir -p "$WORK/binutils-build"
    (cd "$WORK/binutils-build" && "$WORK/binutils-$BINUTILS_VER/configure" --target=sh-elf \
        --prefix="$PREFIX" --disable-nls --disable-werror --disable-gdb --disable-sim \
        && make -j"$(nproc)" && make install)
fi
SH_AS=$(command -v sh-elf-as)
SH_LD=$(command -v sh-elf-ld)
rm -rf "$WORK/build"
mkdir -p "$WORK/build"
cd "$WORK/build"

# GCC 16 hosts default to C++20, where u8"" is char8_t. GCC 15's libcody
# needs u8"" to be char. Do not force a -std: libcody checks for exactly C++11.
export CXXFLAGS="${CXXFLAGS:--O2} -fno-char8_t"

# SH-4 without FPU instructions, little-endian, one multilib. The firmware
# runs with the FPU in single-precision mode. Plain -m4 code sets FPSCR.PR
# (double precision) at each call, and it would give that mode to the
# firmware. -m4-nofpu code does not touch FPSCR. The FLAC decoder uses
# integer maths only.
"$WORK/gcc-$GCC_VER/configure" \
    --target=sh-elf \
    --prefix="$PREFIX" \
    --with-as="$SH_AS" \
    --with-ld="$SH_LD" \
    --with-cpu=m4-nofpu \
    --with-endian=little \
    --with-multilib-list=m4-nofpu \
    --enable-languages=c \
    --without-headers \
    --with-newlib \
    --disable-shared \
    --disable-threads \
    --disable-tls \
    --disable-nls \
    --disable-libssp \
    --disable-libquadmath \
    --disable-libgomp \
    --disable-decimal-float

make -j"$(nproc)" all-gcc all-target-libgcc
make install-gcc install-target-libgcc
"$PREFIX/bin/sh-elf-gcc" --version | head -1
