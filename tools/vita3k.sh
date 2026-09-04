#!/usr/bin/env bash
# tools/vita3k.sh - drive the Vita3K emulator from the command line for the
# Xita pipeline (no console needed).
#
#   vita3k.sh install  <file.vpk> <TITLEID>      unzip a homebrew VPK into ux0:app/<TITLEID>
#   vita3k.sh run      <TITLEID> [seconds]       launch, auto-dismiss dialogs, run, kill
#   vita3k.sh shaders  [shader-dir]              compile shaders/*.cg with xv_shadercomp inside
#                                                the emulator and copy the .gxp files back
#   vita3k.sh shot     <out.png>                 screenshot the X display
#
# Environment: VITA3K_BIN (default ~/vita3k/ubuntu/Vita3K), VITA3K_PREF, VITA3K_ARGS (extra Vita3K options, e.g. -c <config.yml> for a second pref path)
# (default ~/.local/share/Vita3K/Vita3K), DISPLAY (default :0).
#
# Vita3K rewrites config.yml from defaults whenever CLI options are given unless
# -f/-w are passed, so every launch here uses `-f -w`.  The Qt front end still
# shows modal dialogs (welcome / missing font package); they are dismissed with a
# synthetic click via tools/xtest_click.py (XTest, no extra packages needed).
set -euo pipefail

VITA3K_BIN="${VITA3K_BIN:-$HOME/vita3k/ubuntu/Vita3K}"
VITA3K_PREF="${VITA3K_PREF:-$HOME/.local/share/Vita3K/Vita3K}"
export DISPLAY="${DISPLAY:-:0}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLICK="python3 $HERE/xtest_click.py"
LOG="${VITA3K_LOG:-/tmp/vita3k_run.log}"

die() { echo "vita3k.sh: $*" >&2; exit 1; }
need() { [ -x "$VITA3K_BIN" ] || die "Vita3K binary not found at $VITA3K_BIN"; }

kill_emu() { pkill -9 -x Vita3K 2>/dev/null || true; sleep 0.5; }

# Dismiss whatever modal Vita3K shows on launch.  Button coordinates assume the
# default window placement at the top-left of a 1080p display (what Vita3K does
# on a fresh config); pass VITA3K_NOCLICK=1 to skip.
dismiss_dialogs() {
    [ -n "${VITA3K_NOCLICK:-}" ] && return 0
    $CLICK click 626 688 >/dev/null 2>&1 || true      # welcome: Ok
    sleep 1
    $CLICK click 558 372 >/dev/null 2>&1 || true      # missing font pkg: "don't show again"
    sleep 0.5
    $CLICK click 585 409 >/dev/null 2>&1 || true      # missing font pkg: Launch Anyway
}

cmd_install() {
    local vpk="$1" tid="$2"
    [ -f "$vpk" ] || die "no such vpk: $vpk"
    rm -rf "$VITA3K_PREF/ux0/app/$tid"
    mkdir -p "$VITA3K_PREF/ux0/app/$tid"
    unzip -qo "$vpk" -d "$VITA3K_PREF/ux0/app/$tid"
    echo "installed $vpk -> ux0:app/$tid"
}

# launch <TITLEID>; returns immediately, emulator keeps running in background
launch() {
    need
    kill_emu
    (cd "$(dirname "$VITA3K_BIN")" && "$VITA3K_BIN" ${VITA3K_ARGS:-} -f -w -l 1 -r "$1" > "$LOG" 2>&1 &)
    sleep 14
    dismiss_dialogs
}

# X11 id of Vita3K's render window (class "vita3k", title not the main window's).
render_window() {
    for id in $(xprop -root _NET_CLIENT_LIST 2>/dev/null | grep -oE "0x[0-9a-f]+"); do
        timeout 2 xprop -id "$id" WM_CLASS 2>/dev/null | grep -qi '"vita3k"' || continue
        timeout 2 xprop -id "$id" WM_NAME 2>/dev/null | grep -q '"Vita3K v' && continue
        echo "$id"; return 0
    done
    return 1
}

# run <TITLEID> [seconds] [frame-dir]: launch, capture frames of the render window
# every 1.5 s into frame-dir (default /tmp/vita3k_frames), stop, print app log.
cmd_run() {
    local tid="$1" secs="${2:-60}" fdir="${3:-/tmp/vita3k_frames}"
    mkdir -p "$fdir"; rm -f "$fdir"/frame_*.png
    need; kill_emu
    (cd "$(dirname "$VITA3K_BIN")" && "$VITA3K_BIN" ${VITA3K_ARGS:-} -f -w -l 1 -r "$tid" > "$LOG" 2>&1 &)
    local t=0 n=0 win=""
    while [ "$t" -lt "$secs" ] && pgrep -x Vita3K >/dev/null; do
        sleep 1.5; t=$((t + 2))
        [ -z "$win" ] && win="$(render_window || true)"
        [ -z "$win" ] && [ "$t" -eq 14 ] && dismiss_dialogs
        [ -n "$win" ] && timeout 5 import -window "$win" "$fdir/frame_$(printf %03d $n).png" 2>/dev/null && n=$((n + 1))
    done
    kill_emu
    echo "--- emulator log: app output + errors ---"
    grep -E "\[xv|\[xv/|sceClibPrintf|\|E\||\|W\|" "$LOG" | grep -vE "telephony|VDPAU" | tail -60
    echo "(full log: $LOG; $n frame(s) in $fdir)"
}

cmd_shaders() {
    local dir="${1:-shaders}" out="$VITA3K_PREF/ux0/data/xita/shaders"
    [ -d "$VITA3K_PREF/ux0/app/XVSC00001" ] || die "xv_shadercomp not installed: $0 install tools/shadercomp/xv_shadercomp.vpk XVSC00001"
    [ -f "$VITA3K_PREF/ur0/data/libshacccg.suprx" ] || die "put libshacccg.suprx at $VITA3K_PREF/ur0/data/"
    mkdir -p "$out"
    rm -f "$out"/*.gxp "$out"/compile.log
    cp "$dir"/*.cg "$out"/
    echo "compiling $(ls "$dir"/*.cg | wc -l) shader(s) inside Vita3K ..."
    launch XVSC00001
    for _ in $(seq 1 40); do
        sleep 5
        [ -f "$out/compile.log" ] && grep -q "^done:" "$out/compile.log" && break
    done
    kill_emu
    [ -f "$out/compile.log" ] || die "no compile.log produced (see $LOG)"
    cp "$out"/*.gxp "$dir"/ 2>/dev/null || true
    cp "$out/compile.log" "$dir/compile.log"
    grep -E "^done:|FAILED|\[error" "$dir/compile.log" | head -20
    echo "pulled $(ls "$out"/*.gxp 2>/dev/null | wc -l) .gxp into $dir/"
}

cmd_shot() { import -window root "$1" && echo "wrote $1"; }

case "${1:-}" in
    install)  shift; cmd_install "$@" ;;
    run)      shift; cmd_run "$@" ;;
    shaders)  shift; cmd_shaders "$@" ;;
    shot)     shift; cmd_shot "$@" ;;
    *)        sed -n '2,15p' "$0"; exit 2 ;;
esac
