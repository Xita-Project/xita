#!/bin/sh
# Run the Halo 2 harness on the Raspberry Pi bench (ssh alias "pi"), pinned to cores 2-3 so the Halo CE bench
# (~/xita, cores 0-1) can run at the same time.
#
#   tools/h2_pi_run.sh <tag> [seconds] [K=V ...]
#
# Pi layout (~/xita-h2, same shape as tools/h2_host_run.sh's base): harness (static armhf from
# tools/h2_host_build.py --static --cc <arm-none-linux-gnueabihf-gcc>), app0-mp328/, game/, save-base/,
# xita-base/{env.txt,shaders/}, runs/<tag>/ (working dir: boot.log in ux0:data/xita-halo2/, stderr.log).
# PI_HARNESS=<local file> copies a fresh harness first. PI_APP0=<dir under ~/xita-h2> picks the stage files (default app0-mp328). H2_DRIVE=1 drives the menus into a match (h2_menu_driver.py). PI_CPUS (default 2,3) is the taskset list.
# The run's save copy is removed at the end unless PI_KEEP_SAVE=1 (it is ~700 MB on the SD card).
# A crash leaves a core in the run dir (ulimit -c unlimited). Hangs: ssh pi, gdb -p <pid>.
# The Pi has no GPU in this path (null GXM) and is not a Vita: structure, counters, hangs, CPU split only.
set -e
here=$(cd "$(dirname "$0")" && pwd)
tag=${1:?tag}; secs=${2:-600}; [ $# -gt 1 ] && shift 2 || shift
cpus=${PI_CPUS:-2,3}
[ -n "${PI_HARNESS:-}" ] && scp -q "$PI_HARNESS" pi:xita-h2/harness
scp -q "$here/h2_host_run.sh" "$here/h2_menu_driver.py" pi:xita-h2/
ssh pi "cd ~/xita-h2 && ulimit -c unlimited && \
  H2_HOST_BASE=\$HOME/xita-h2 H2_HOST_BIN=\$HOME/xita-h2/harness H2_HOST_GAME=\$HOME/xita-h2/game \
  H2_HOST_APP0=\$HOME/xita-h2/${PI_APP0:-app0-mp328} H2_HOST_TASKSET=$cpus H2_DRIVE=${H2_DRIVE:-0} \
  H2_DRIVER=\$HOME/xita-h2/h2_menu_driver.py sh ./h2_host_run.sh $tag $secs $*; \
  [ \"${PI_KEEP_SAVE:-0}\" = 1 ] || rm -rf 'runs/$tag/ux0:data/xita-halo2/save'"
