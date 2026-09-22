#!/bin/sh
# Run the whole-game harness on the Raspberry Pi bench (ARM, weak memory ordering) from this machine over ssh.
#
#   tools/pi_run.sh <tag> [seconds] [K=V ...]      default 300 s; extra K=V are added to the Pi-side environment
#
# Pi layout (user birchwoodgod, ssh alias "pi" -> 192.168.0.9, key ~/.ssh/xita_pi): ~/xita/harness (static armhf
# build from tools/host_build.py --static --cc <arm-none-linux-gnueabihf-gcc>), ~/xita/halo_image.bin (the stage's
# recomp/halo_image.bin), ~/xita/haloce (game dir), ~/xita/runs/<tag>.log + save-<tag>/ (removed at exit).
# The same runtime knobs as tools/host_run.sh; pad script enters the campaign (a10 loads ~frame 500).
# The Pi is ~10x slower than the x86 host and has no GPU: structure, counters, races and crashes only, never ms.
# PI_HARNESS=<local file> copies a fresh harness first.  A crash leaves a core in ~/xita/runs (ulimit -c unlimited).
set -e
tag=${1:?tag}; secs=${2:-300}; [ $# -gt 1 ] && shift 2 || shift
pad=${PI_PAD:-150:a,300:a,450:a,1100:lu,1400:lu}
[ -n "${PI_HARNESS:-}" ] && scp -q "$PI_HARNESS" pi:~/xita/harness
ssh pi "cd ~/xita && mkdir -p runs && rm -rf runs/save-$tag && mkdir -p runs/save-$tag && ulimit -c unlimited && \
  env XV_LEVEL=a10 XV_EXPERIMENTAL_OBJECT_JOBS=1 XV_OBJECT_JOB_WORKERS=2 XV_OWNER_PHASE=1 XV_THREADS=1 \
      XV_VERTEX_WORKER=1 XV_VERTEX_REFERENCES=1 XV_NATIVE_OBJECT_BASIS=1 XV_VERTEX_CAPTURE_RETAIN=1 \
      XV_PAD=$pad $* timeout $secs ./harness halo_image.bin haloce runs/save-$tag > runs/$tag.log 2>&1; \
  rc=\$?; rm -rf runs/save-$tag; \
  echo \"rc=\$rc reports=\$(grep -c 'report at frame' runs/$tag.log) last=\$(grep 'report at frame' runs/$tag.log | tail -1 | sed 's/.*frame //')\"; \
  grep 'frame stats' runs/$tag.log | tail -1 | sed -E 's/.*frame stats: //' | cut -c1-160; \
  ls runs | grep -c core || true"
