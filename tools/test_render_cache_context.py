#!/usr/bin/env python3
"""Check helper-exclusive cache admission, generation retirement and mappings."""
from pathlib import Path
import os, subprocess, tempfile
from test_draw_state_batch import function
ROOT = Path(__file__).resolve().parents[1]
s = (ROOT / 'recomp/kernel/xk_scene_thread.c').read_text()
source = r'''
#define XV_OWNER_PHASE 1
#define XV_THREAD_PAGE_TABLE 1
#include "recomp/xv_x86rt.h"
#include <assert.h>
#include <stdlib.h>
static xctx ctx, foreign;
static unsigned depth, cache_generation;
static int helper;
static int xv_scene_thread_on_helper(void) { return helper; }
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
__thread uint32_t *xv_host_page_table;
static uint8_t *active_image;
#undef X_IMG_BASE
#define X_IMG_BASE active_image
#include "recomp/kernel/xk_render_cache_context.h"
'''
source += function(s, 'xv_scene_thread_owns_context') + '\n'
source += function(s, 'xv_scene_thread_context_generation') + '\n'
source += r'''
static unsigned owner_calls;
int xv_owner_phase_active(void *c, unsigned phase, uint32_t *g) {
    (void)c; (void)phase; ++owner_calls; *g=99; return 0;
}
int main(int argc, char **argv) {
    (void)argv;
    uint32_t live[4]={0}, snapshot[4]={0};
    uint8_t a[4], b[4];
    g_xpt=live; xv_host_page_table=snapshot; g_img_base=a; active_image=b;
    uint32_t token=0;
    if (argc==1) {
        assert(xv_render_cache_admit(&ctx,&token)==0 && token==99);
        assert(owner_calls==1 && xv_render_cache_pt()==live && xv_render_cache_image()==a);
        return 0;
    }
    assert(!setenv("XV_MODEL_CACHE_HELPER","1",1));
    for (helper=0; helper<2; ++helper) for (depth=0; depth<2; ++depth) {
        cache_generation=7; token=0;
        assert(xv_render_cache_admit(&foreign,&token)==-1 && token==0);
        assert(xv_render_cache_admit(0,&token)==-1 && token==0);
        assert(xv_render_cache_admit(&ctx,0)==-1);
        int result=xv_render_cache_admit(&ctx,&token);
        assert(result==(helper && depth ? 1 : -1));
        assert(token==(helper && depth ? 7u : 0u));
    }
    helper=depth=1; token=7; cache_generation=8;
    assert(xv_render_cache_admit(&ctx,&token)==-1 && token==7);
    token=0; assert(xv_render_cache_admit(&ctx,&token)==1 && token==8);
    cache_generation=0; token=0; assert(xv_render_cache_admit(&ctx,&token)==-1);
    cache_generation=UINT32_MAX; assert(xv_render_cache_admit(&ctx,&token)==-1);
    assert(owner_calls==0 && xv_render_cache_pt()==snapshot && xv_render_cache_image()==b);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='xita-cache-context-') as d:
    p=Path(d); (p/'test.c').write_text(source)
    subprocess.run([os.environ.get('CC','cc'),'-Wall','-Werror','-fsanitize=address,undefined',
                    '-I',str(ROOT),str(p/'test.c'),'-o',str(p/'test')],check=True)
    for args in [[],['helper']]: subprocess.run([str(p/'test'),*args],check=True)
print('cache context admission, generations and active mappings passed')
