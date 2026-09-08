#!/usr/bin/env python3
"""Exercise the production idle-pump resize command and acknowledgement."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = (root / 'main.c').read_text()
start = source.index('static int xv_pump_resolution(void)')
end = source.index('\n/* Only called on the recording thread', start)
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t g_resolution_request, g_resolution_result;
static struct { void *ctx; unsigned render_height; } g_gfx;
static int g_frame_events, phase, signals, fail;
#define XV_FRAME_COMPLETED 2
static void sceGxmFinish(void *ctx) {
    assert(ctx == g_gfx.ctx && phase == 0 && !g_resolution_result);
    phase = 1;
}
static void xv_gfx_free_scale(void *g) {
    assert(g == &g_gfx && phase == 1 && !g_resolution_result);
    g_gfx.render_height = 544; phase = 2;
}
static void xv_gfx_configure_resolution_height(unsigned height) {
    assert(phase == 2 && height == g_resolution_request && !g_resolution_result);
    if (!fail) g_gfx.render_height = height;
    phase = 3;
}
static void xv_frame_events_signal(void *event, unsigned bit) {
    assert(event == &g_frame_events && bit == XV_FRAME_COMPLETED);
    assert(!g_resolution_request && g_resolution_result == g_gfx.render_height);
    assert(phase == 0 || phase == 3); signals++;
}
'''
suffix = r'''
static void request(unsigned height) {
    phase = 0; g_resolution_result = 0; g_resolution_request = height;
    assert(xv_pump_resolution() == 1);
    assert(g_resolution_result == g_gfx.render_height && !g_resolution_request);
}
int main(void) {
    g_gfx.render_height = 544;
    assert(!xv_pump_resolution() && !signals);
    request(544); assert(phase == 0 && signals == 1);
    request(360); assert(phase == 3 && g_resolution_result == 360);
    request(544); assert(phase == 3 && g_resolution_result == 544);
    fail = 1; request(360); assert(phase == 3 && g_resolution_result == 544);
    fail = 0; request(480); assert(g_resolution_result == 480 && signals == 5);
    assert(!xv_pump_resolution() && signals == 5);
    puts("PASS: production resize finishes GPU before freeing surfaces, acknowledges actual resolution, handles allocation fallback and idle/same-size requests");
}
'''
with tempfile.TemporaryDirectory(prefix='xita-resolution-handoff-') as directory:
    test = pathlib.Path(directory) / 'test.c'
    exe = test.with_suffix('')
    test.write_text(prefix + source[start:end] + suffix)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
