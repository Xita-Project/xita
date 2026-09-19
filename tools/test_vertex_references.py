#!/usr/bin/env python3
"""Production indexed upload/retention checks and benchmark restoration."""
import itertools
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
sdk = Path(os.environ.get('VITASDK', str(Path.home() / 'vitasdk')))
source = (root / 'runtime/xv_d3d.c').read_text()
retain = source[source.index('static unsigned index_bounds('):source.index('static uint32_t geometry_hash(')]
layout = source[source.index('static int vertex_reference_layout('):source.index('static void record_draw(')]
fixture = r'''
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "runtime/xv_shader.h"
#include "runtime/xv_index_copy.h"
#include "runtime/xv_index_cache.h"
#define XV_NUM_LISTS 3
#define XV_SEQ_INDICES 65536
#define XV_QUAD_INDICES 1024
#define XV_FRAME_INDICES 2048
#define XV_LOG(...) ((void)0)
static uint16_t seq[XV_SEQ_INDICES], quads[3*XV_QUAD_INDICES], indices[3*XV_FRAME_INDICES];
static uint16_t *g_seq_indices=seq, *g_quad_indices=quads, *g_frame_indices=indices;
static unsigned g_build_frame, g_index_used[3], scan_index_calls;
static unsigned scan_reference_calls, scan_reference_fast;
static uint64_t g_index_requested[3], scan_indices;
static xv_vertex_refs g_draw_vertex_refs;
static int g_draw_vertex_refs_valid, enabled, scan;
static int xv_vertex_references_enabled(void) { return enabled; }
static int draw_scan_neon(void) { return scan; }
'''
checks = r'''
int main(void)
{
    index_reuse_begin_frame();
    uint16_t source[5]={0,1023,7,500,1023};
    for (unsigned mode=0;mode<4;mode++) {
        enabled=mode&1;scan=mode>>1;source[1]=1023;const void *p=source;unsigned vertices=0;
        assert(retain_indices(&p,5,&vertices) && vertices==1024 && p!=source);
        assert(!memcmp(p,source,sizeof source));
        assert(g_draw_vertex_refs_valid==enabled);
        if(enabled) {
            assert(g_draw_vertex_refs.vertices==1024 && g_draw_vertex_refs.groups==3);
            source[1]=999; assert(((const uint16_t*)p)[1]==1023);
            assert(g_draw_vertex_refs.bits[3] & (1u<<31));
        }
    }
    assert(scan_reference_calls==2 && scan_reference_fast==0); /* host fallback */
    const void *p=g_seq_indices;unsigned n;
    assert(retain_indices(&p,30,&n) && n==30 && !g_draw_vertex_refs_valid);
    quads[0]=0;quads[1]=3;quads[2]=1;p=quads;
    assert(retain_indices(&p,3,&n) && n==4 && !g_draw_vertex_refs_valid);
    p=NULL;assert(!retain_indices(&p,5,&n) && !g_draw_vertex_refs_valid);
    p=source;g_index_used[0]=XV_FRAME_INDICES;
    assert(!retain_indices(&p,5,&n) && !g_draw_vertex_refs_valid);
    xv_attr_desc_t attr={.stream=0,.components=4};
    xv_vs_desc_t desc={.nstreams=1,.stride={32},.nattrs=1,.attrs=&attr};
    for(unsigned format=0;format<SCE_GXM_ATTRIBUTE_FORMAT_UNTYPED;format++) {
        attr.format=format;assert(vertex_reference_layout(&desc,0,32));
    }
    attr.format=SCE_GXM_ATTRIBUTE_FORMAT_UNTYPED;
    assert(!vertex_reference_layout(&desc,0,32));
    attr.format=SCE_GXM_ATTRIBUTE_FORMAT_F32;attr.offset=20;
    assert(!vertex_reference_layout(&desc,0,32));
    attr.offset=16;assert(vertex_reference_layout(&desc,0,32));
    assert(!vertex_reference_layout(&desc,0,31));
    assert(!vertex_reference_layout(&desc,1,32));
    attr.components=0;assert(!vertex_reference_layout(&desc,0,32));
    attr.components=5;assert(!vertex_reference_layout(&desc,0,32));
    index_reuse_report(60);index_reuse_shutdown();
    puts("PASS: production retention uses captured indices; borrowed/invalid paths clear coverage; stream stride/attribute guards");
}
'''
with tempfile.TemporaryDirectory(prefix='xita-vertex-references-') as temp:
    temp = Path(temp)
    (temp / 'retainer.c').write_text(fixture + retain + layout + checks)
    flags = ['cc', '-O2', '-g', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
             '-Wno-unused-parameter', '-fno-strict-aliasing', '-fsanitize=address,undefined',
             '-I' + str(root), '-I' + str(root / 'runtime'),
             '-idirafter', str(sdk / 'arm-vita-eabi/include')]
    for name, path in [('uploads', root / 'recomp/host/vertex_references_test.c'),
                       ('retainer', temp / 'retainer.c'),
                       ('benchmark', root / 'recomp/host/resolution_benchmark_test.c')]:
        subprocess.run(flags + [str(path), '-lm', '-o', str(temp / name)], check=True)
        if name != 'benchmark':
            subprocess.run([str(temp / name)], check=True,
                           env=dict(os.environ,XV_INDEX_REUSE='0',XV_INDEX_METADATA='0'))
    keys = ['XV_BENCHMARK_VERTEX_REFERENCES', 'XV_BENCHMARK_NATIVE_BOUNDS',
            'XV_BENCHMARK_VERTEX_COPY', 'XV_BENCHMARK_DRAW_SCAN']
    for values in itertools.product(['0', '1'], repeat=4):
        env = dict(os.environ, **dict(zip(keys, values)))
        subprocess.run([str(temp / 'benchmark')], env=env, check=True, stdout=subprocess.DEVNULL)
    print('PASS: all 16 benchmark selector combinations; complete/cancel/lost-view restoration')
