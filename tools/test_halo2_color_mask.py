#!/usr/bin/env python3
"""Test production Halo 2 blend setup/cache against channel-preservation cases."""
import os
from pathlib import Path
import subprocess
import tempfile
from test_remote_draw_trace import function

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'games/halo2_5849/menu_gxm.c').read_text()
actual = function(source, 'static fs_entry *get_fs(')
setup = source[source.index('    SceGxmBlendInfo blend;'):source.index('    uint32_t blend_key =')]
entry = source[source.index('typedef struct { uint64_t hash, key;'):source.index('} fs_entry;') + len('} fs_entry;')]
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum { MAX_FS=128, SCE_GXM_COLOR_MASK_A=1, SCE_GXM_COLOR_MASK_R=2,
       SCE_GXM_COLOR_MASK_G=4, SCE_GXM_COLOR_MASK_B=8, SCE_GXM_COLOR_MASK_ALL=15,
       SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4=0, SCE_GXM_MULTISAMPLE_NONE=0 };
typedef int SceGxmProgram;
typedef int SceGxmProgramParameter;
typedef int SceGxmShaderPatcherId;
typedef int SceGxmFragmentProgram;
typedef struct { unsigned colorMask; } SceGxmBlendInfo;
typedef struct { uint64_t hash; const SceGxmProgram *gxp; } vs_entry;
@ENTRY@
static fs_entry g_fs[MAX_FS];static unsigned g_nfs;
static void *g_patcher;
static unsigned loads,patches,registers;static int program=1;
static unsigned patched_masks[MAX_FS];
typedef struct { uint32_t setup[0x2000/4]; uint64_t hash; } h2_command_state;
typedef struct { int unused; } menu_combiner;
static uint64_t hash_ps(const h2_command_state *s,const menu_combiner *cb) { (void)cb;return s->hash; }
static const SceGxmProgram *load_gxp(const char *path) { (void)path;loads++;return &program; }
static void note_missing(uint64_t h,const char *s) { (void)h;(void)s;assert(0); }
static int sceGxmShaderPatcherRegisterProgram(void *p,const SceGxmProgram *g,SceGxmShaderPatcherId *id) {
    (void)p;assert(g==&program);*id=++registers;return 0;
}
static int sceGxmShaderPatcherCreateFragmentProgram(void *p,int id,int fmt,int msaa,
    const SceGxmBlendInfo *blend,const SceGxmProgram *vs,SceGxmFragmentProgram **out) {
    (void)p;(void)id;(void)fmt;(void)msaa;assert(vs);
    assert(patches<MAX_FS);patched_masks[patches++]=blend->colorMask;*out=&program;return 0;
}
static const SceGxmProgramParameter *sceGxmProgramFindParameterByName(const SceGxmProgram *p,const char *name) {
    (void)p;(void)name;return NULL;
}
static unsigned sceGxmProgramParameterGetResourceIndex(const SceGxmProgramParameter *p) { (void)p;return 0; }
@ACTUAL@
static unsigned mask_for(unsigned nv) {
    h2_command_state state={0},*s=&state;s->setup[0x358/4]=nv;
@SETUP@
    (void)s;return blend.colorMask;
}
int main(void) {
    const unsigned nvbits[]={1,0x100,0x10000,0x1000000}; /* B, G, R, A */
    const unsigned gxm[]={SCE_GXM_COLOR_MASK_B,SCE_GXM_COLOR_MASK_G,SCE_GXM_COLOR_MASK_R,SCE_GXM_COLOR_MASK_A};
    h2_command_state s={.hash=0x1234567890abcdefULL};menu_combiner cb={0};
    vs_entry vs={0x98765,&program};fs_entry *variants[16];
    for(unsigned bits=0;bits<16;bits++) {
        unsigned nv=0,expected=0;
        for(unsigned k=0;k<4;k++) if(bits&(1u<<k)) { nv|=nvbits[k];expected|=gxm[k]; }
        SceGxmBlendInfo blend={mask_for(nv)};assert(blend.colorMask==expected);
        variants[bits]=get_fs(&s,&cb,&vs,&blend,1);
        assert(variants[bits] && patched_masks[bits]==expected && patches==bits+1);
        /* The same input/shader state must select its original immutable variant. */
        assert(get_fs(&s,&cb,&vs,&blend,1)==variants[bits] && patches==bits+1);
    }
    assert(loads==1 && registers==1 && patches==16);
    /* Going back to a previous mask must not return the last-created variant. */
    for(unsigned bits=16;bits-->0;) {
        unsigned nv=0;for(unsigned k=0;k<4;k++) if(bits&(1u<<k)) nv|=nvbits[k];
        SceGxmBlendInfo blend={mask_for(nv)};
        assert(get_fs(&s,&cb,&vs,&blend,1)==variants[bits]);
    }
    assert(patches==16);
    puts("H2 color masks: all 16 masks, immutable variants, shared shader and return-to-prior-state passed");
}
'''.replace('@ENTRY@', entry).replace('@ACTUAL@', actual).replace('@SETUP@', setup)
with tempfile.TemporaryDirectory(prefix='xita-h2-color-mask-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(fixture)
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror']
    if os.getenv('SANITIZE'):
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command + [str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
