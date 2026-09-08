#!/usr/bin/env bash
# Regenerate the recompiled engine (recomp/code_*.c + recomp/xv_recomp_protos.h) from YOUR copy of
# the Xbox executable.  The generated code is game code and is never committed; run this once after
# cloning and again whenever xita_recomp.py changes.
#
#   tools/recomp.sh [path/to/default.xbe]        (default: haloce/default.xbe)
#
# Needs python3 with iced-x86 (pip install iced-x86).  Then: make RECOMP=1
set -euo pipefail
cd "$(dirname "$0")/.."
XBE="${1:-haloce/default.xbe}"; [ $# -gt 0 ] && shift
[ -f "$XBE" ] || { echo "no XBE at $XBE - extract default.xbe from your own disc image first"; exit 1; }
PY="${PYTHON:-python3}"

# The profile owns revision identity, custom HLE addresses and postprocessing.
# Other ports can invoke xita_recomp.py --profile ... directly.
"$PY" xita_recomp.py "$XBE" --profile halo_ce_3925 --symbols halo_symbols.json -o recomp/ "$@"
echo "recompiled -> recomp/code_*.c; now: make RECOMP=1"
