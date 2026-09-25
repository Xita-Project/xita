#!/usr/bin/env python3
"""Exercise the production stream-corruption diagnostic's opt-in boundary.

Default scheduler calls must not inspect guest memory. Opt-in calls must still
detect a corrupted vtable, and report it once. Each mode runs in a fresh process
because the production setting is cached for the application's lifetime.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "recomp/kernel/xd3d.c").read_text()
start = source.index("void xd3d_ds_check(const char *where, uint32_t eip)\n{")
end = source.index("static uint32_t ds_stream_obj", start)
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#define DS_MAX_STREAMS 128
#define DS_STREAM_VTBL 0x12345678u
static struct { uint32_t obj; } g_ds_streams[DS_MAX_STREAMS];
static struct { unsigned id; struct { uint32_t r[8]; } ctx; } *xk_cur;
static uint32_t g_xpt[1 << 20];
static unsigned reads, reports, corrupt;
static uint32_t read_guest(uint32_t addr) {
    reads++;
    return corrupt && addr == 0x1000 ? 0 : DS_STREAM_VTBL;
}
#define X_M32(a) read_guest(a)
#define X_G(a) ((void *)(uintptr_t)(a))
static void log_line(const char *fmt, ...) { (void)fmt; reports++; }
#define D3DLOG(...) log_line(__VA_ARGS__)
'''
fixture += source[start:end]
fixture += r'''
int main(int argc, char **argv) {
    assert(argc == 2);
    const int on = atoi(argv[1]);
    g_ds_streams[0].obj = 0x1000;
    for (unsigned i = 0; i < 10000; i++) xd3d_ds_check("yield", 0);
    assert(reads == (on ? 10000u : 0u));
    assert(reports == 0);
    corrupt = 1;
    xd3d_ds_check("yield", 0);
    assert(reports == (on ? 1u : 0u));
    unsigned before = reads;
    xd3d_ds_check("yield", 0);
    assert(reads == before); /* off never scans; detected corruption reports once */
    puts(on ? "opt-in detects corruption once" : "default: zero guest-memory reads");
}
'''
with tempfile.TemporaryDirectory(prefix="xita-ds-debug-") as tmp:
    c = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    c.write_text(fixture)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall",
                    "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    str(c), "-o", str(exe)], check=True)
    env = os.environ.copy()
    env.pop("XV_DS_CHECK", None)
    subprocess.run([str(exe), "0"], env=env, check=True)
    for value in ("1", "0", ""):
        env["XV_DS_CHECK"] = value
        subprocess.run([str(exe), "1"], env=env, check=True)
