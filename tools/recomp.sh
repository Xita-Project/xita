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

# HLE overrides by address for functions the symbol database does not name: Bink (movies stay off),
# the DirectSound voice queries, and the Winsock/XNet entry points (served by the in-process network
# layer in recomp/kernel/xk_net.c).  Format ADDR:Name:StackArgc.
HLE=(
  1B83C0:BinkOpen:2
  19C5E7:DSoundVoiceIsPlaying:1 19C5FF:DSoundVoiceStop:1
  1AF688:ws_WSAGetLastError:0 1B0D29:ws_accept:3 1B0D13:ws_bind:3 1B1572:ws_closesocket:1 1B0D1E:ws_connect:3
  1B03F1:ws_getpeername:3 1B03FC:ws_getsockname:3 1B03E2:ws_getsockopt:5 1B01B9:ws_ioctlsocket:3 1B0572:ws_listen:2
  1B149E:ws_recv:4 1B158C:ws_recvfrom:6 1B0D34:ws_select:5 1B14EA:ws_send:4 1B157D:ws_sendto:6 1B0D04:ws_setsockopt:5
  1B102C:ws_socket:3
  1AF3E0:xn_XNetCreateKey:2 1AF488:xn_XNetGetTitleXnAddr:1 1AF44E:xn_XNetRandom:2 1AF420:xn_XNetRegisterKey:2
  1AF437:xn_XNetUnregisterKey:1 1AF471:xn_XNetXnAddrToInAddr:3
)

"$PY" xita_recomp.py "$XBE" --manifest game_manifest.json --symbols halo_symbols.json -o recomp/ \
    --hle-addr "${HLE[@]}" "$@"
echo "recompiled -> recomp/code_*.c; now: make RECOMP=1"
