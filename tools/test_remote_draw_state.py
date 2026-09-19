#!/usr/bin/env python3
"""Check the production one-frame state logger without guest memory or GXM."""
from pathlib import Path
import os
import subprocess
import tempfile
from test_remote_draw_trace import function

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'runtime/xv_d3d.c').read_text()
actual = function(source, 'static void trace_draw_state(')
recorder = function(source, 'static void record_draw(')
# Hardware uses the asynchronous capture return. Descriptors must be resolved
# first, but the logger cannot sit below that return or force a worker join.
call = recorder.index('trace_draw_state(c, d, texok);')
assert recorder.index('unsigned texok = record_material(') < call
assert call < recorder.index('if(captured) {') < recorder.index('xv_vertex_prepare_finish(')
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
typedef struct { void *data; unsigned w,h,type,u,v; } SceGxmTexture;
typedef struct { unsigned pass,previous_frame,atest; SceGxmTexture tex[4]; float texscale[4][4]; } cmd_t;
typedef struct { const char *gxp; } xv_vs_desc_t;
static struct {
    unsigned ps_hash,ps_key,blend_enable,src_blend,dst_blend,z_enable,z_write,z_func,color_mask;
    unsigned tex_guest[4],tex_addr_u[4],tex_addr_v[4],tex_min[4],tex_mag[4];
    float vsc[192][4];
} S;
#define XV_RT_SLOTS 2
static struct { void *mem; } g_rt[XV_RT_SLOTS];
static unsigned g_build_frame=55,trace,reads;
static struct list { unsigned ncmds; } list={4};
static struct list *cur_list(void) { return &list; }
static int vertex_trace_frame(void) {return trace;}
static const void *sceGxmTextureGetData(const SceGxmTexture *t) { reads++; return t->data; }
#define GET(name,field) static unsigned name(const SceGxmTexture *t) {reads++;return t->field;}
GET(sceGxmTextureGetWidth,w)
GET(sceGxmTextureGetHeight,h)
GET(sceGxmTextureGetType,type)
GET(sceGxmTextureGetUAddrMode,u)
GET(sceGxmTextureGetVAddrMode,v)
static char logbuf[16384]; static unsigned used;
static void logline(const char *fmt,...) {
    va_list args;va_start(args,fmt);
    int n=vsnprintf(logbuf+used,sizeof logbuf-used,fmt,args);va_end(args);
    assert(n>=0 && (unsigned)n<sizeof logbuf-used);used+=(unsigned)n;
}
#define XV_LOG logline
@ACTUAL@
static void reset(void) { used=reads=0;logbuf[0]=0; }
int main(void) {
    cmd_t c={0}; xv_vs_desc_t d={"shadow.gxp"};
    /* A disabled capture must not touch even its descriptor or command list. */
    trace_draw_state(NULL,NULL,15); assert(!reads && !used);
    trace=1; S.tex_guest[0]=0x1234; S.tex_addr_u[0]=4; S.tex_addr_v[0]=3;
    c.texscale[0][0]=c.texscale[0][1]=1;
    /* Bound-but-unused texture slots are intentionally not GXM descriptors. */
    memset(c.tex,0xa5,sizeof c.tex);trace_draw_state(&c,&d,0);
    assert(!reads && strstr(logbuf,"guest 00001234 captured 0 address 4/3"));
    assert(!strstr(logbuf,"[draw-texture]") && !strstr(logbuf,"[draw-projection]"));
    reset(); c.tex[0]=(SceGxmTexture){(void *)(uintptr_t)0x4000,128,64,0x60000000,2,3};
    trace_draw_state(&c,&d,1);
    assert(reads==6 && strstr(logbuf,"frame 55 cmd 3 pass 0 vs shadow.gxp"));
    assert(strstr(logbuf,"data 00004000 size 128/64 type 60000000 address 2/3"));
    assert(!strstr(logbuf,"[draw-projection]"));
    /* A sampled render target selects only the bounded projection rows. */
    reset(); g_rt[1].mem=c.tex[0].data; S.vsc[96+15][0]=1.25f;
    trace_draw_state(&c,&d,1);
    assert(strstr(logbuf,"read-rt 2 c[15] 1.25 0 0 0"));
    unsigned count=0;for(char *p=logbuf;(p=strstr(p,"[draw-projection]"));p++)count++;
    assert(count==13 && strstr(logbuf,"c[31]") && !strstr(logbuf,"c[32]"));
    /* A producer can write a target without sampling any texture. */
    reset();c.pass=1;trace_draw_state(&c,&d,0);
    assert(!reads && strstr(logbuf,"read-rt 0 c[28]"));
    /* Unbound NULL storage must never count as a render-target alias. */
    reset();c.pass=0;g_rt[1].mem=NULL;c.tex[0].data=NULL;
    trace_draw_state(&c,&d,1);assert(!strstr(logbuf,"[draw-projection]"));
    return 0;
}
'''.replace('@ACTUAL@', actual)
with tempfile.TemporaryDirectory(prefix='xita-draw-state-') as tmp:
    p = Path(tmp)
    (p / 'test.c').write_text(fixture)
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror']
    if os.getenv('SANITIZE'):
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command + [str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('Draw state: disabled path, unused bindings, effective samplers and RT projections passed')
