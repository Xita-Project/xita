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
    actual = '\n'.join(function(source, sig) for sig in (
        'void xd3d_remote_trace_begin(unsigned frame)',
        'void xd3d_remote_trace_note_draw(void)',
        'void xd3d_remote_trace_end(unsigned frame, unsigned commands)',
        'int xd3d_hist_active(void)\n', 'int xd3d_vertex_trace_active(void)\n'))
    assert 'hist_remote_track' not in source
    renderer = (ROOT / 'runtime/xv_d3d.c').read_text()
    assert 'xd3d_remote_trace_begin(g_build_frame)' in function(renderer, 'void xv_d3d_BeginFrame(void)')
    assert 'xd3d_remote_trace_end(g_build_frame, l->ncmds)' in function(renderer, 'uint32_t xv_d3d_EndFrame(void)')
    swap = function(renderer, 'void xv_d3d_Swap(void)')
    assert 'xd3d_remote_trace_begin(g_build_frame)' in swap and 'xd3d_remote_trace_end(g_build_frame, l->ncmds)' in swap
    assert 'xd3d_remote_trace_note_draw();' in function(renderer, 'static void trace_draw_state(')
    assert 'if (vertex_trace_frame()) {\n' in renderer
    assert 'if (vertex_trace_frame()) XV_LOG("[hist] stream' in renderer
    assert '!trace_frame() && stride==32' in renderer
    fixture = r'''
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
static struct { unsigned frame; } g_dev;
static int g_hist_frame=-2, g_remote_hist_on;
static unsigned g_remote_hist_frame, g_remote_hist_records, logs, request;
int xv_remote_take_draw_trace(void) __attribute__((weak));
static void log_stub(const char *format, ...) { (void)format; ++logs; }
#define D3DLOG(...) log_stub(__VA_ARGS__)
@ACTUAL@
#ifndef ABSENT
int xv_remote_take_draw_trace(void) { unsigned r=request; request=0; return r; }
#endif
int main(void) {
    /* No networking hook, or no request: preserve the configured three-frame
     * manual trace at frames 12..14. Frame numbers describe the next draw. */
    setenv("XV_D3D_HIST", "12", 1);setenv("XV_D3D_HIST_COUNT", "3", 1);
    for(unsigned f=1;f<17;f++) {
        g_dev.frame=f-1;xd3d_remote_trace_begin(f);
        assert(xd3d_hist_active()==(f>=12 && f<=14));
        assert(xd3d_vertex_trace_active()==xd3d_hist_active());
    }
    assert(!logs);
#ifndef ABSENT
    /* Recorder and guest-frame counters may be arbitrarily far apart.
     * Owner progress must not truncate a queued/deferred scene capture. */
    g_dev.frame=100;request=1;xd3d_remote_trace_begin(40);
    assert(!request && g_remote_hist_on && g_remote_hist_frame==40);
    for(unsigned draw=0;draw<1000;draw++) {
        g_dev.frame=101+draw;
        assert(xd3d_vertex_trace_active());
        assert(!xd3d_hist_active());
        xd3d_remote_trace_note_draw();
    }
    assert(g_remote_hist_records==1000);
    request=1;xd3d_remote_trace_begin(41); /* no drain/end yet: cannot rearm */
    assert(request && g_remote_hist_frame==40);
    xd3d_remote_trace_end(40,1002);
    assert(!xd3d_vertex_trace_active() && !g_remote_hist_on && logs==2);
    xd3d_remote_trace_note_draw(); assert(g_remote_hist_records==1000);
    xd3d_remote_trace_begin(41);
    assert(!request && g_remote_hist_frame==41 && g_remote_hist_records==0);
    xd3d_remote_trace_end(41,0); /* empty capture is explicit, closes normally */
    assert(!xd3d_vertex_trace_active());
    request=1;xd3d_remote_trace_begin(UINT_MAX);
    assert(xd3d_vertex_trace_active());xd3d_remote_trace_end(UINT_MAX,1);
    request=1;xd3d_remote_trace_begin(0);
    assert(g_remote_hist_frame==0 && xd3d_vertex_trace_active());
    xd3d_remote_trace_end(0,1);assert(!xd3d_vertex_trace_active());
    /* Remote completion leaves a separately configured manual trace intact. */
    g_dev.frame=11;request=1;xd3d_remote_trace_begin(1234);
    assert(xd3d_hist_active());xd3d_remote_trace_end(1234,1);
    assert(xd3d_vertex_trace_active());
    g_dev.frame=14;assert(!xd3d_vertex_trace_active());
#else
    (void)request;
#endif
    return 0;
}
'''.replace('@ACTUAL@', actual)
    if os.getenv('XITA_TEST_EMIT'):
        Path(os.environ['XITA_TEST_EMIT']).write_text(fixture)
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
