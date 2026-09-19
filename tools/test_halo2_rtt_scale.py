#!/usr/bin/env python3
"""Exercise production H2 texture selection and uniforms with CPU GXM doubles.

The sampler oracle uses texel centres: copied and GPU-resident views of the same
linear image must address the same texel. This does not validate GPU scheduling.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'games/halo2_5849/menu_gxm.c').read_text()
binding = source[source.index('    tex_entry *tex[4]'):source.index('    /* vertices:')]
scaling = source[source.index('    for (unsigned u = 0; u < 4; ++u) { scale[u]'):
                 source.index('    if (fs->psc)')]
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
enum { W=640, H=480 };
typedef struct { unsigned id; } SceGxmTexture;
typedef struct { unsigned w,h; int linear; SceGxmTexture tex; } tex_entry;
typedef struct { uint32_t setup[0x2000/4]; } h2_command_state;
typedef struct { int sampler[4]; } fragment;
typedef struct { unsigned color_offset,physical; int gpu_newer,guest_newer;
    void *mem,*guest; unsigned synced_epoch; SceGxmTexture as_tex; } target;
static target targets[4], *g_open;
static tex_entry copies[4];
static int direct[4],missing[4];
static unsigned g_perf_uploads,g_perf_rtt,g_perf_tex_us,xv_write_epoch;
static void *g_ctx;
static unsigned ends,finishes,downloads,fetches;
static unsigned perf_now(void) { return 0; }
static void end_scene(void) { ++ends;g_open=NULL; }
static void sceGxmFinish(void *ctx) { (void)ctx;++finishes; }
static void rings_recycle_after_finish(void) { }
static int guest_changed(target *t) { return t->guest_newer; }
static void (*h2_render_target_fault)(unsigned);
static void set_sampler_state(SceGxmTexture *tx,const h2_command_state *s,
                              unsigned u,unsigned levels,int clamp) {
    (void)tx;(void)s;(void)u;(void)levels;(void)clamp;
}
static target *sampled_target(unsigned off,unsigned fmt,unsigned ctrl,unsigned rect) {
    (void)fmt;(void)ctrl;(void)rect;
    for(unsigned u=0;u<4;u++) if(direct[u] && targets[u].color_offset==off) return &targets[u];
    return NULL;
}
static void sync_target(target *t) { (void)t;++downloads;g_open=NULL; }
static tex_entry *get_texture(const h2_command_state *s,const void *c,unsigned u) {
    (void)s;(void)c;++fetches;return missing[u]?NULL:&copies[u];
}
static void bind_and_scale(h2_command_state *s,fragment *fs,target *t,
                           unsigned used,float scale[4][4]) {
    struct { unsigned tex_used; } cb={used};
    const void *c=NULL;
@BINDING@
@SCALING@
    (void)bound;
}
static void check_linear(float scale[4],unsigned w,unsigned h) {
    /* Unnormalised Xbox pixel centres must land at the same GXM texels,
     * including interior points that a scale of 1 would clamp to the edge. */
    unsigned x[]={0,1,w/2,w-1},y[]={0,1,h/2,h-1};
    for(unsigned i=0;i<4;i++) {
        assert((unsigned)floorf((x[i]+0.5f)*scale[0]*w)==x[i]);
        assert((unsigned)floorf((y[i]+0.5f)*scale[1]*h)==y[i]);
    }
    assert(scale[2]==0 && scale[3]==0);
}
int main(void) {
    h2_command_state s={0};fragment fs={{0,1,2,3}};
    target destination={.color_offset=0x800000};float scale[4][4];
    for(unsigned u=0;u<4;u++) {
        targets[u].color_offset=0x100000+u*0x200000;
        s.setup[(0x1B00+u*64)/4]=targets[u].color_offset;
        copies[u]=(tex_entry){W,H,1,{u}};
    }
    bind_and_scale(&s,&fs,&destination,15,scale);
    for(unsigned u=0;u<4;u++) check_linear(scale[u],W,H);
    /* All units may take the resident path. No CPU readback or copy is needed. */
    for(unsigned u=0;u<4;u++) direct[u]=1;
    g_open=&targets[0];fetches=0;
    bind_and_scale(&s,&fs,&destination,15,scale);
    for(unsigned u=0;u<4;u++) check_linear(scale[u],W,H);
    assert(ends==1 && !finishes && !downloads && !fetches && g_perf_rtt==4);
    /* Mixed draw: direct RT, smaller linear copy, normalised image, disabled. */
    direct[1]=direct[2]=direct[3]=0;
    copies[1].w=128;copies[1].h=64;copies[2].linear=0;
    bind_and_scale(&s,&fs,&destination,7,scale);
    check_linear(scale[0],W,H);check_linear(scale[1],128,64);
    for(unsigned u=2;u<4;u++) assert(scale[u][0]==1 && scale[u][1]==1);
    /* State must reset for the next draw, including missing/unused samplers. */
    direct[0]=0;missing[0]=1;fs.sampler[1]=-1;
    bind_and_scale(&s,&fs,&destination,3,scale);
    for(unsigned u=0;u<4;u++) assert(scale[u][0]==1 && scale[u][1]==1);
    /* Sampling the current target uses the copied path, with the same scale. */
    direct[0]=1;missing[0]=0;g_open=&targets[0];
    bind_and_scale(&s,&fs,&targets[0],1,scale);
    check_linear(scale[0],W,H);assert(downloads==1);
    puts("H2 RTT coordinates: resident/copied texels, mixed units and state reset passed");
}
'''.replace('@BINDING@', binding).replace('@SCALING@', scaling)
with tempfile.TemporaryDirectory(prefix='xita-h2-rtt-scale-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(fixture)
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-address']
    if os.getenv('SANITIZE'):
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command + [str(path / 'test.c'), '-lm', '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
