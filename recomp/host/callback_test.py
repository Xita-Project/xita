#!/usr/bin/env python3
"""Check callback stack ownership using the locally recompiled Halo vblank code."""
from pathlib import Path
import os
import re
import shlex
import subprocess

root = Path(__file__).resolve().parents[2]
callback = None
for path in (root / 'recomp').glob('code_*.c'):
    match = re.search(r'^void f_000BB4E0\(.*?^\}', path.read_text(), re.M | re.S)
    if match:
        callback = match[0]
        break
assert callback, 'Generate the user-owned Halo 3925 functions first'
source = Path(os.environ.get('CALLBACK_TEST_SOURCE', root / 'recomp/kernel/xd3d.c'))
head = r'''
#define xv_call dispatch_callback
#include "D3D_SOURCE"
#undef xv_call
#include <assert.h>
uint8_t *g_xram;
xk_thread *xk_cur;
static uint64_t clock_us;
static unsigned events;
void xk_os_log(const char *fmt, ...) { (void)fmt; }
void xk_yield(void) { assert(!"unexpected guest trap/yield"); }
uint64_t xk_os_monotonic_us(void) { return ++clock_us; }
void f_00012CC3(xctx *c) { events++; c->r[0] = 0; X_RET(1); }
'''.replace('D3D_SOURCE', str(source))
body = r'''
void dispatch_callback(xctx *c, uint32_t fn)
{
    assert(X_M32(c->r[4]) == 0xDEAD0010u);
    if (fn == 0xBB4E0) { f_000BB4E0(c); return; }
    uint32_t arg = X_ARG(0);
    if (fn == 2) { /* nested host-to-guest callback, with caller cleanup */
        uint32_t sp = c->r[4];
        call_guest(c, 1, arg + 1);
        assert(c->r[4] == sp && X_ARG(0) == arg);
        c->r[4] += 4;
    } else { assert(fn == 1); c->r[0] = arg; c->r[4] += 8; } /* RET 4 */
}
int main(void)
{
    xk_mem_setup(0x10000, 0x400000);
    g_xram = calloc(1, xk_mem_arena_size()); assert(g_xram);
    xk_mem_bind_arena();
    uint32_t stack = xk_mem_alloc(0x10000, 0, 0, 0, 0); assert(stack);
    xctx c = {0}; c.r[4] = stack + 0xFFF0;
    const uint32_t sp = c.r[4];
    memset(X_G(stack), 0xA5, 0x10000);
    X_IMG8(0x276B3C) = 1; /* Exercise the event-setting branch as well. */
    X_IMG32(0x276B38) = 4;
    g_dev.vblank_cb = 0xBB4E0;
    for (unsigned i = 0; i < 60 * 60 * 60; i++) { /* one hour at 60 Hz */
        vblank_fire(&c);
        assert(c.r[4] == sp);
        assert(X_IMG32(0x1F8C80) == i + 1);
    }
    assert(events == 60 * 60 * 30);
    assert(X_M32(g_vb_data) == 60 * 60 * 60);
    assert(xv_n_fires == 60 * 60 * 60);
    for (unsigned i = 0; i < 10000; i++) {
        call_guest(&c, 1, i); assert(c.r[4] == sp && c.r[0] == i);
        call_guest(&c, 2, i); assert(c.r[4] == sp && c.r[0] == i + 1);
    }
    /* Only the handful of top-of-stack callback slots may change. */
    for (unsigned i = 0; i < 0xFFC0; i++) assert(X_M8(stack + i) == 0xA5);
    for (unsigned i = 0; i < 16; i++) assert(X_M8(sp + i) == 0xA5);
    free(g_xram); free(g_xpt);
    puts("PASS: real Halo vblank for one simulated hour, callback data/events, RET/RET 4, nested calls, stack guards");
}
'''
path = root / 'recomp/host/build/callback_test.c'
path.parent.mkdir(exist_ok=True)
path.write_text(head + '\n' + callback + '\n' + body)
exe = path.with_suffix('')
subprocess.run([
    os.environ.get('CC', 'cc'), '-std=gnu11', '-O2', '-g',
    '-I' + str(root / 'recomp/kernel'), '-ffunction-sections', '-fdata-sections',
    *shlex.split(os.environ.get('CALLBACK_TEST_CFLAGS', '')),
    str(path), str(root / 'recomp/xv_x86rt.c'), str(root / 'recomp/kernel/xk_mem.c'),
    '-Wl,--gc-sections', '-lm', '-o', str(exe)
], check=True)
subprocess.run([str(exe)], check=True)
