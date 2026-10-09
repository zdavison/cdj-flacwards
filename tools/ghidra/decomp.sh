#!/bin/sh
# Decompile CDJ-900 MAIN functions to clean C. Usage: decomp.sh 0xADDR ...
cd "$(dirname "$0")/../.." || exit 1
"${GHIDRA_HOME:-/opt/ghidra}/support/analyzeHeadless" "${GHIDRA_PROJ:-ghidra-proj}" cdj900 -process main-unpacked.bin -noanalysis \
  -scriptPath tools/ghidra -postScript Decompile.java "$@" 2>/dev/null \
  | grep -v -E '^(INFO|WARN|WARNING|openjdk|OpenJDK)|^    /' | python3 tools/ghidra/clean.py
