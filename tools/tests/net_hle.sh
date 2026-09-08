#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
test_bin=$(mktemp /tmp/xita-net-test.XXXXXX)
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
# Use system libc headers, then the SDK declarations; dead-section removal
# excludes startup APIs from this transport-only host test.
cc -std=gnu11 -O1 -g -fno-strict-aliasing -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined --param asan-globals=0 \
    -idirafter "${VITASDK:-$HOME/vitasdk}/arm-vita-eabi/include" \
    -Irecomp tools/tests/net_hle.c recomp/xv_x86rt.c -Wl,--gc-sections -lm -o "$test_bin"
"$test_bin"
