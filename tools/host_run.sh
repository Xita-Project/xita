#!/bin/sh
# Run the whole-game host harness (recomp/host/harness.c) from the main menu into a10 gameplay, no hardware.
#
#   tools/host_run.sh [seconds] [K=V ...]        default 300 s; extra K=V are added to the environment
#
# Prerequisites: a harness built by tools/host_build.py (HOST_OBJ/harness), the stage's recomp/halo_image.bin,
# and the game directory (maps/, ui.map ...).  Environment knobs:
#   HOST_STAGE   stage build dir (default ../external-clock-candidate/build, relative to this checkout)
#   HOST_OBJ     host_build.py --out dir holding 'harness'
#   HOST_GAME    game dir (default ~/.local/share/Vita3K/Vita3K/ux0/data/xita/haloce)
#   HOST_LOG     log file (default host-run.log in the current directory)
#   HOST_SAVE    save dir (default a fresh temp dir: the harness writes cache*.map and saves there)
#   HOST_PAD     scripted pad (default "150:a,300:a,450:a": Campaign -> New game -> difficulty -> a10 loads
#                at ~frame 500, the cinematic starts at ~frame 780, gameplay camera control follows it)
#
# Findings (2026-09-21): the game paces itself on the 60 Hz vblank thread (vblank_thread in xd3d.c, started by
# the game's SetVerticalBlankCallback) and runs 30 frames/s on the host; with no input it sits in the main
# menu forever ("68 Begin/End, 809 SetVertexData", the ring camera loop).  XV_LEVEL=a10 is the default and a
# no-op - it only patches the mission-1 table for other levels.  Reports print every 60 frames
# ([host] report at frame N, then the kernel's 60-frame [object-jobs]/[object-pass]/... lines);
# XV_HOST_CLOCK_TRACE=1 prints the vblank clock against the frame-end target once a second.
# Host timing is ~60x Vita (x86 desktop): use it for structure, correctness, deadlocks and counters, not ms.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
secs=${1:-300}; [ $# -gt 0 ] && shift
stage=${HOST_STAGE:-$here/../external-clock-candidate/build}
obj=${HOST_OBJ:?set HOST_OBJ to the tools/host_build.py --out directory}
game=${HOST_GAME:-$HOME/.local/share/Vita3K/Vita3K/ux0/data/xita/haloce}
log=${HOST_LOG:-$PWD/host-run.log}
save=${HOST_SAVE:-$(mktemp -d "${TMPDIR:-/tmp}/xita-host-save.XXXXXX")}
pad=${HOST_PAD:-150:a,300:a,450:a}
[ -x "$obj/harness" ] || { echo "no harness in $obj (run tools/host_build.py)" >&2; exit 2; }
cd "$stage/recomp"
# same runtime knobs the Vita perf builds use (worker lanes, owner phase, vertex worker/capture)
env XV_LEVEL=a10 XV_EXPERIMENTAL_OBJECT_JOBS=1 XV_OBJECT_JOB_WORKERS=2 XV_OWNER_PHASE=1 XV_THREADS=1 \
    XV_VERTEX_WORKER=1 XV_VERTEX_REFERENCES=1 XV_NATIVE_OBJECT_BASIS=1 XV_VERTEX_CAPTURE_RETAIN=1 \
    XV_PAD="$pad" "$@" \
    timeout "$secs" "$obj/harness" halo_image.bin "$game" "$save" > "$log" 2>&1 || true
echo "log $log ($(grep -c 'report at frame' "$log") reports, last: $(grep 'report at frame' "$log" | tail -1 | sed 's/.*frame //'))"
grep 'frame stats' "$log" | tail -1 | sed -E 's/.*frame stats: //' | cut -c1-160
