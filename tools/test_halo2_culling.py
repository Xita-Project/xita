#!/usr/bin/env python3
"""Exercise production GXM state setup with alternating face/enable state."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'games/halo2_5849/menu_gxm.c').read_text()
start = source.index('    SceGxmCullMode cull =')
end = source.index('    sceGxmSetVertexProgram', start)
clip = next(line for line in source[end:].splitlines()
            if 'sceGxmSetRegionClip(g_ctx, cull_all' in line)
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
enum { SCE_GXM_CULL_NONE, SCE_GXM_CULL_CW, SCE_GXM_CULL_CCW,
       SCE_GXM_REGION_CLIP_ALL, SCE_GXM_REGION_CLIP_OUTSIDE, W=640, H=480 };
typedef int SceGxmCullMode;
typedef struct { uint32_t setup[0x2000/4]; } state;
static void *g_ctx;
static int mode=-1,region=-1,calls;
static void sceGxmSetCullMode(void *ctx, int m) { (void)ctx;mode=m;calls++; }
static void sceGxmSetRegionClip(void *ctx,int m,int x,int y,int r,int b) {
    (void)ctx;assert(x==0 && y==0 && r==639 && b==479);region=m;
}
static void apply(const state *s) {
@SETUP@
@CLIP@
}
int main(void) {
    const unsigned faces[]={0x404,0x405,0x408};
    /* Explicit CW/CCW geometry in Xbox top-left window coordinates. */
    const float tri[2][6]={{0,0,8,0,0,8},{0,0,0,8,8,0}};
    state s={0}; int expected_calls=0;
    for(unsigned front=0;front<2;front++)
    for(unsigned f=0;f<3;f++)
    for(unsigned enable=0;enable<2;enable++) {
        s.setup[0x308/4]=enable;s.setup[0x39c/4]=faces[f];s.setup[0x3a0/4]=0x900+front;
        apply(&s);assert(calls==++expected_calls);
        for(unsigned reverse=0;reverse<2;reverse++) {
            float x[3],y[3];
            /* Production vertex conversion, then GXM viewport: winding preserved. */
            for(unsigned v=0;v<3;v++) {
                x[v]=320+320*(tri[reverse][2*v]/320-1);
                y[v]=240-240*(1-tri[reverse][2*v+1]/240);
            }
            float area=(x[1]-x[0])*(y[2]-y[0])-(y[1]-y[0])*(x[2]-x[0]);
            int discarded=region==SCE_GXM_REGION_CLIP_ALL ||
                (mode==SCE_GXM_CULL_CW && area>0) || (mode==SCE_GXM_CULL_CCW && area<0);
            int is_front=reverse==front;
            int expected=enable && (f==2 || (f==0 ? is_front : !is_front));
            assert(discarded==expected);
        }
        /* A following two-sided UI draw must clear both cull and clip-all state. */
        s.setup[0x308/4]=0;apply(&s);expected_calls++;
        assert(mode==SCE_GXM_CULL_NONE && region==SCE_GXM_REGION_CLIP_OUTSIDE);
    }
    puts("H2 GXM culling: face/winding combinations and per-draw reset passed");
}
'''.replace('@SETUP@', source[start:end]).replace('@CLIP@', clip)
with tempfile.TemporaryDirectory(prefix='xita-h2-cull-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(fixture)
    flags = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror']
    if os.getenv('SANITIZE'):
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(flags + [str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
