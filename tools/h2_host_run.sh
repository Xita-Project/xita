#!/bin/sh
# Run a Halo 2 host harness (tools/h2_host_build.py) headless on this machine.
#
#   tools/h2_host_run.sh <tag> [seconds] [K=V ...]      default 600 s; K=V are added to the environment
#
# Layout (all private, never committed), under H2_HOST_BASE (default <worktree>/../private/hostrun):
#   app0-<stage>/          the stage VPK unpacked without eboot/sce_sys (halo2_image.bin, pass shaders, contracts,
#                          h2menu_*.gxp, halo2-dsp.bin)            -> H2_HOST_APP0 (default app0-mp328)
#   save-base/             a save tree (profiles, preferences, cache3-5 partitions) copied per run (reflink)
#   xita-base/env.txt      runtime knobs (the lab's XV_MENU_* set), copied per run
#   xita-base/shaders/     device/offline-compiled h2menu_*.gxp (ux0:data/xita/shaders), linked
#   runs/<tag>/            the run's working directory: app0:<file> links and ux0:data/... resolve relative to it
#                          (psp2_host.c uses Vita paths verbatim), boot.log in ux0:data/xita-halo2/, stderr.log
# Other knobs: H2_HOST_BIN (harness, required), H2_HOST_GAME (game dir; default the lab's copy),
# H2_HOST_SAVE_FROM (save tree to copy instead of save-base), H2_HOST_TASKSET (cpu list for taskset),
# H2_HOST_KEEP_CACHE4=1 (keep the copied cache4 partition instead of starting it empty).
# H2_HOST_BARE=1: no xita-base env.txt and no shaders link, like a device with only the package installed
# (a MENU_RUNTIME_DEFAULTS=1 build then runs on its compiled-in defaults and loads h2menu_*.gxp from app0).
# H2_DRIVE=1 runs tools/h2_menu_driver.py next to the harness (title -> split-screen match, driver.log).
# Pad: XV_PAD="<flip>:<button>[*<hold>],..." / "t<sec>:..." / XV_PAD_FILE=<file> (see host/psp2_host.c).
# Timing on a host is ~structure only: no GPU, x86 is many times a Vita core. Never quote it as Vita ms.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
tag=${1:?tag}; secs=${2:-600}; [ $# -gt 1 ] && shift 2 || shift
base=${H2_HOST_BASE:-$here/../private/hostrun}
bin=${H2_HOST_BIN:?set H2_HOST_BIN to the harness built by tools/h2_host_build.py}
app0=${H2_HOST_APP0:-$base/app0-mp328}
game=${H2_HOST_GAME:-/home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/vita3k/ux0/data/xita-halo2/game}
save_from=${H2_HOST_SAVE_FROM:-$base/save-base}
run=$base/runs/$tag
pin=${H2_HOST_TASKSET:+taskset -c $H2_HOST_TASKSET}     # the save copy, the driver and the harness all stay on these cores
[ -e "$run" ] && { echo "run $run exists (use a new tag)" >&2; exit 2; }
mkdir -p "$run/ux0:data/xita-halo2" "$run/ux0:data/xita"
for f in "$app0"/*; do ln -s "$f" "$run/app0:${f##*/}"; done    # Vita paths are app0:<name>, no slash
ln -s "$game" "$run/ux0:data/xita-halo2/game"
$pin cp -a --reflink=auto "$save_from" "$run/ux0:data/xita-halo2/save"
# The game re-formats the n: cache partition (cache4) at every boot and the runtime refuses raw access to a populated
# one ("[h2/blocked] raw access to mounted/populated cache4 requires a general FATX driver"): start it empty, as the
# lab's preserve_fresh_cache.py does (the intro then re-copies mainmenu.map there as cache002.map).
[ "${H2_HOST_KEEP_CACHE4:-0}" = 1 ] || { rm -rf "$run/ux0:data/xita-halo2/save/cache4" "$run/ux0:data/xita-halo2/save/cache4.raw"; mkdir "$run/ux0:data/xita-halo2/save/cache4"; }
if [ "${H2_HOST_BARE:-0}" != 1 ]; then
  cp "$base/xita-base/env.txt" "$run/ux0:data/xita/env.txt"
  ln -s "$base/xita-base/shaders" "$run/ux0:data/xita/shaders"
fi
cd "$run"
echo "run $run: $secs s, bin $bin"
driver=
if [ "${H2_DRIVE:-0}" = 1 ]; then       # tools/h2_menu_driver.py presses through the menus into a match (driver.log)
    set -- XV_PAD_FILE="$run/pad.txt" XV_HOST_STATUS="$run/status.txt" "$@"
    $pin python3 "${H2_DRIVER:-$here/tools/h2_menu_driver.py}" "$run" --timeout "$secs" > /dev/null 2>&1 &
    driver=$!
fi
env "$@" timeout "$secs" $pin "$bin" > stderr.log 2>&1 || echo "exit status $?"
[ -n "$driver" ] && { kill $driver 2>/dev/null || true; cat driver.log 2>/dev/null || true; }
log="ux0:data/xita-halo2/boot.log"
echo "boot.log $(wc -l < "$log") lines; last progress: $(grep '^\[host\] flip' "$log" | tail -1)"
grep -m3 '\[h2/blocked\]' "$log" || true
grep '\[h2/perf\]' "$log" | tail -1 | cut -c1-300
grep '^\[host\] exit' "$log" || true
