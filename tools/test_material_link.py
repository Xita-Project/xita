#!/usr/bin/env python3
"""Exercise production material-link cache/fallback with a mock GXM loader."""
from pathlib import Path
import os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
src=(ROOT/'runtime/xv_d3d.c').read_text()
a=src.index('static xv_fshader_t *fragment_for_ps_policy(');b=src.index('\nstatic xv_fshader_t *fragment_for_ps_mode',a)
body=src[a:b]
fixture=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {const char *gxp;} desc;
typedef struct {desc *desc;} vs_t;
typedef struct {vs_t vs;} vs_slot_t;
typedef struct {unsigned colorFunc,alphaFunc;} SceGxmBlendInfo;
typedef struct {unsigned alpha_test_mode;} xv_fshader_t;
typedef struct {uint16_t vs,next;int16_t entry;uint8_t blend,failed,alpha_mode,replace_blend;xv_fshader_t fs;} ps_link_t;
#define XV_PS_TABLE_COUNT 1
#define BLEND_MODES 2
#define XV_PS_BUCKETS 16
#define XV_PS_LINKS 8
#define SCE_GXM_BLEND_FUNC_NONE 0
#define XV_LOG(...) ((void)0)
#define XV_ONCE(flag,...) ((void)0)
static struct {unsigned ps_key;const char *gxp;} xv_ps_table[]={{0x154066FD,"app0:shaders/ps_154066FD_7F_t8.frag.gxp"}};
static vs_slot_t g_vs[2];static ps_link_t g_ps_links[XV_PS_LINKS];
static unsigned g_ps_buckets[XV_PS_BUCKETS],g_ps_count;
static struct {unsigned src,dst,mask;} g_blend_combo[2];
static unsigned black_loads,normal_loads,fail_black;
static const SceGxmBlendInfo *blend_info_for(unsigned blend,SceGxmBlendInfo *b) {(void)blend;return b;}
static int xv_fshader_load(xv_fshader_t *f,const char *p,vs_t *v,const SceGxmBlendInfo *b) {
 (void)f;(void)v;(void)b;
 if(strstr(p,"_axisblack_na.frag.gxp")) {++black_loads;return fail_black?-1:0;}
 assert(strstr(p,"_na.frag.gxp"));++normal_loads;return 0;
}
@BODY@
int main(void) {
 xv_fshader_t *normal=fragment_for_ps_policy(g_vs,0,0,1,0);
 assert(normal && normal->alpha_test_mode==1 && normal_loads==1);
 xv_fshader_t *special=fragment_for_ps_policy(g_vs,0,0,4,0);
 assert(special && special!=normal && special->alpha_test_mode==1 && black_loads==1);
 assert(fragment_for_ps_policy(g_vs,0,0,4,0)==special && black_loads==1);
 fail_black=1;
 xv_fshader_t *fallback=fragment_for_ps_policy(g_vs+1,0,0,4,0);
 assert(fallback && fallback->alpha_test_mode==1 && black_loads==2 && normal_loads==2);
 assert(fragment_for_ps_policy(g_vs+1,0,0,4,0)==fallback && black_loads==2);
 assert(!fragment_for_ps_policy(g_vs,-1,0,4,0));
 puts("actual material linker: distinct cache identity, failed-load fallback and alpha mode preservation pass");
}
'''.replace('@BODY@',body)
if os.getenv('XITA_TEST_EMIT'):
 Path(os.environ['XITA_TEST_EMIT']).write_text(fixture)
with tempfile.TemporaryDirectory(prefix='xita-material-link-') as tmp:
 c=Path(tmp)/'test.c'; exe=Path(tmp)/'test';c.write_text(fixture)
 subprocess.run(['cc','-O2','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
