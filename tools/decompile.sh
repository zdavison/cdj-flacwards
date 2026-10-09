#!/bin/sh
# Reproduce the local analysis of the CDJ-900 MAIN firmware 4.32.
#   tools/decompile.sh CDJ-900v432.zip    (or C900MAIN.UPD)
# Needs Python 3 and Ghidra 12.1 (GHIDRA_HOME, default /opt/ghidra).
# Output, all local and in .gitignore (it is derived from Pioneer firmware;
# never commit or publish it):
#   firmware/            the update files and the unpacked images
#   $GHIDRA_PROJ/cdj900  the Ghidra project (default ghidra-proj)
#   $DECOMP_OUT          C for every function, one file per 64 KiB
#                        (default build/decomp)
# After this, tools/ghidra/decomp.sh and tools/ghidra/run.sh work.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
[ $# -eq 1 ] || { sed -n '2,13p' "$0"; exit 2; }
GH=${GHIDRA_HOME:-/opt/ghidra}
PROJ=${GHIDRA_PROJ:-ghidra-proj}
OUT=${DECOMP_OUT:-build/decomp}
case $OUT in /*) ;; *) OUT=$ROOT/$OUT ;; esac
[ -x "$GH/support/analyzeHeadless" ] || { echo "Ghidra not found at $GH (set GHIDRA_HOME)" >&2; exit 1; }

python3 tools/unpack_main.py "$1"
mkdir -p "$PROJ" "$OUT"
# The application is a raw image at 0x04000000. SeedFunctions runs before
# the auto-analysis: it creates a function at each literal-pool code pointer.
"$GH/support/analyzeHeadless" "$PROJ" cdj900 -overwrite \
    -import firmware/unpacked/main-unpacked.bin \
    -processor SuperH4:LE:32:default -loader BinaryLoader -loader-baseAddr 0x04000000 \
    -scriptPath tools/ghidra -preScript SeedFunctions.java \
    -postScript DecompileAll.java "$OUT" 2>&1 \
    | grep -E 'SeedFunctions|DecompileAll|ERROR' || true
# Remove Ghidra's FPSCR bookkeeping. The firmware runs the FPU in one mode,
# so these lines only hide the real code.
for f in "$OUT"/*.c; do
    python3 tools/ghidra/clean.py --file < "$f" > "$f.tmp" && mv "$f.tmp" "$f"
done
echo "functions written: $(grep -h -c '^// ===== ' "$OUT"/*.c | awk '{s+=$1} END {print s}')"
