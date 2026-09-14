#!/usr/bin/env python3
"""Exercise the production world/upscale fence placement and error paths."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'runtime/main.c').read_text()
start = source.index('static int xv_gfx_end_scenes(')
end = source.index('static int xv_gfx_render_frame(', start)
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <psp2/gxm.h>
typedef struct { SceGxmContext *ctx; void *scaled_target; } xv_gfx_t;
#define XV_LOG(...) ((void)0)
#define XV_RENDER_END(pass,call) (call)
static xv_gfx_t gfx;
static SceGxmNotification final,world;
static unsigned ends,upscales;
static int fail_end,fail_upscale,early;
int sceGxmEndScene(SceGxmContext *ctx,const SceGxmNotification *vertex,
                  const SceGxmNotification *fragment)
{
    assert(ctx==gfx.ctx && !vertex && !ends && !upscales);
    assert(fragment==(gfx.scaled_target ? (early?&world:NULL) : &final));
    ends++;
    return fail_end?-1:0;
}
static int xv_gfx_upscale(xv_gfx_t *g,unsigned ui,const SceGxmNotification *fence)
{
    assert(g==&gfx && g->scaled_target && ends==1 && !upscales && !fail_end);
    assert(ui==2 && fence==&final);upscales++;
    return fail_upscale?-1:0;
}
'''
suffix = r'''
int main(void)
{
    for(unsigned scaled=0;scaled<2;scaled++)for(early=0;early<2;early++)
    for(fail_end=0;fail_end<2;fail_end++)for(fail_upscale=0;fail_upscale<2;fail_upscale++) {
        gfx.ctx=(SceGxmContext*)(uintptr_t)1;gfx.scaled_target=scaled?(void*)1:NULL;
        ends=upscales=0;
        int result=xv_gfx_end_scenes(&gfx,2,&final,early?&world:NULL);
        assert(result==(fail_end || (scaled && fail_upscale)?-1:0));
        assert(ends==1 && upscales==(unsigned)(scaled && !fail_end));
    }
    puts("PASS: world fence only precedes an upscale; final fence stays on last scene; failed scene submission stops continuation");
}
'''
with tempfile.TemporaryDirectory(prefix='xita-scene-fences-') as directory:
    p = Path(directory)
    (p / 'test.c').write_text(prefix + source[start:end] + suffix)
    sdk = Path(os.environ.get('VITASDK', str(Path.home() / 'vitasdk')))
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-idirafter',
                    str(sdk / 'arm-vita-eabi/include'), str(p / 'test.c'),
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
