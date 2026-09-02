#!/bin/sh
# Build the recompiled game + harness for the host (Linux x86-64).  ~63 MB of C, so -O0 and parallel.
set -e
R=$(cd "$(dirname "$0")/.." && pwd); O=${OBJDIR:-$R/host/obj}; mkdir -p "$O"
ls "$R"/code_*.c "$R"/xv_fn_table.c "$R"/xv_stubs_default.c "$R"/xv_x86rt.c "$R"/xv_funchist.c "$R"/kernel/*.c "$R"/host/harness.c "$R"/host/trace.c "$R"/host/softgfx.c | \
  xargs -P "${JOBS:-16}" -I{} sh -c 'f={}; o='"$O"'/$(basename $f .c).o; [ "$o" -nt "$f" ] && [ "$o" -nt '"$R"'/xv_x86rt.h ] && [ "$o" -nt '"$R"'/kernel/xk.h ] || gcc -O1 -g0 -w -std=gnu11 -I'"$R"' -c "$f" -o "$o"'
gcc "$O"/*.o -o "$R/host/harness" -lm -lpthread
echo "built $R/host/harness"
