#!/usr/bin/env python3
"""Exercise actual draw-trace frame selection, separately from HTTP admission."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    source = (ROOT / 'recomp/kernel/xd3d.c').read_text()
    actual = function(source, 'static void hist_remote_track(void)') + '\n'
    actual += function(source, 'int xd3d_hist_active(void)\n')
    # Cover both supported HLE frame boundaries, not a harness-only caller.
    assert 'hist_remote_track();' in function(source, 'static void hist_level_track(void)')
    assert 'hist_level_track();' in function(source, 'void xv_hle_D3DDevice_Present(xctx *c)')
    assert 'hist_remote_track();' in function(source, 'void xv_hle_D3DDevice_Swap(xctx *c)')
    fixture = r'''
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
static struct { unsigned frame; } g_dev;
static int g_hist_frame=-2, g_remote_hist_on;
static unsigned g_remote_hist_frame, logs, request;
int xv_remote_take_draw_trace(void) __attribute__((weak));
#define D3DLOG(...) (++logs)
@ACTUAL@
#ifndef ABSENT
int xv_remote_take_draw_trace(void) { unsigned r=request; request=0; return r; }
#endif
int main(void) {
    /* No networking hook, or no request: preserve the configured three-frame
     * manual trace at frames 12..14. Frame numbers describe the next draw. */
    setenv("XV_D3D_HIST", "12", 1);setenv("XV_D3D_HIST_COUNT", "3", 1);
    for(unsigned f=1;f<17;f++) {
        g_dev.frame=f-1;hist_remote_track();
        assert(xd3d_hist_active()==(f>=12 && f<=14));
    }
    assert(!logs);
#ifndef ABSENT
    /* Remote capture is exactly one frame despite the configured count=3. */
    g_dev.frame=100;request=1;hist_remote_track();
    assert(!request && g_remote_hist_on && g_remote_hist_frame==101);
    for(unsigned draw=0;draw<1000;draw++)assert(xd3d_hist_active());
    g_dev.frame=101;hist_remote_track();
    assert(!xd3d_hist_active() && !g_remote_hist_on && logs==2);
    g_dev.frame=102;hist_remote_track();assert(!xd3d_hist_active());
    /* A request at completion arms the following frame without losing either. */
    request=1;hist_remote_track();assert(xd3d_hist_active());
    g_dev.frame=103;request=1;hist_remote_track();assert(xd3d_hist_active());
    g_dev.frame=104;hist_remote_track();assert(!xd3d_hist_active());
    /* Wrap at UINT_MAX still selects and retires exactly frame zero. */
    g_dev.frame=UINT_MAX;request=1;hist_remote_track();
    assert(g_remote_hist_frame==0 && xd3d_hist_active());
    g_dev.frame=0;hist_remote_track();assert(!xd3d_hist_active());
    /* Overlap does not truncate or extend a separate manual trace. */
    g_dev.frame=11;request=1;hist_remote_track();assert(xd3d_hist_active());
    for(unsigned f=12;f<16;f++) {
        g_dev.frame=f;hist_remote_track();
        assert(xd3d_hist_active()==(f<14));
    }
#else
    (void)request;
#endif
    return 0;
}
'''.replace('@ACTUAL@', actual)
    with tempfile.TemporaryDirectory(prefix='xita-draw-trace-') as directory:
        path = Path(directory)
        (path / 'test.c').write_text(fixture)
        for absent in (False, True):
            command = ['cc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror']
            if os.getenv('SANITIZE'):
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            if absent:
                command += ['-DABSENT=1']
            subprocess.run(command + [str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)
    print('Actual trace selectors passed: one frame, overlap, repeated requests, wrap and absent hook')


if __name__ == '__main__':
    main()
