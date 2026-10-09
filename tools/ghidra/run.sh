#!/bin/sh
# Run a Ghidra script on the analyzed CDJ-900 project. Usage: run.sh Script.java args...
cd "$(dirname "$0")/../.." || exit 1
script=$1; shift
"${GHIDRA_HOME:-/opt/ghidra}/support/analyzeHeadless" "${GHIDRA_PROJ:-ghidra-proj}" cdj900 -process main-unpacked.bin -noanalysis \
  -scriptPath tools/ghidra -postScript "$script" "$@" 2>&1 \
  | sed -n 's/^INFO  [A-Za-z]*\.java> //p; /^\/\/\|^ \|^[a-z{}]/p' | grep -v '(GhidraScript)$' 
