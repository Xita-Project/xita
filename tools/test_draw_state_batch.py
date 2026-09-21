#!/usr/bin/env python3
"""Compare production batched import with retained scalar state publication."""
from pathlib import Path
import os,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
def function(source,name):
    m=re.search(r'^(?:static )?(?:inline )?(?:uint32_t|void|int) '+name+r'\([^;]*?\)\s*\{',source,re.M)
    assert m,name
    end=m.end();depth=1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[m.start():end]+'\n'
def main():
    runtime=(ROOT/'runtime/xv_d3d.c').read_text()
    kernel=(ROOT/'recomp/kernel/xd3d.c').read_text()
    header=(ROOT/'runtime/xv_d3d.h').read_text()
    state=runtime[runtime.rfind('typedef struct {',0,runtime.index('} d3d_state_t;')):runtime.index('} d3d_state_t;')+len('} d3d_state_t;')]
    enums='\n'.join(re.findall(r'enum\s*\{[^}]+\};',header,re.S))
    setters=['SetAllAttributes','SetStreamSource','SetTexture','SetTexturePalette','SetTextureStageState','SetStencil','SetRenderState_ZEnable','SetRenderState_ZWriteEnable','SetRenderState_ZFunc','SetRenderState_CullMode','SetRenderState_AlphaBlendEnable','SetRenderState_ColorWriteEnable','SetRenderState_SrcBlend','SetRenderState_DestBlend','SetRenderState_BlendOp','SetPixelShader']
    source='''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "recomp/kernel/xd3d.h"
#define XV_MAX_STREAMS 4
#undef X_M32
static uint32_t guest[128];
static uint32_t read_state(uint32_t a) {assert(a>=0x18F180u && a<0x18F380u && !(a&3));return guest[(a-0x18F180u)/4];}
#define X_M32(a) read_state(a)
#define D3D_G_TEXTURESTATE 0x18F180u
static float attrs[16][4];
const float (*xd3d_current_attributes(void))[4] {return attrs;}
static uint32_t ps_key_from_capture(uint32_t h) {return h^0x12345678u;}
static unsigned sync_calls;
void xd3d_ps_sync(void) {sync_calls++;xd3d_state.ps_hash^=0x31415926u;}
static uint64_t xv_draw_profile_begin(void) {return 0;}
#define XV_DRAW_STATE 0
static void xv_draw_profile_step(int i,uint64_t *p) {(void)i;(void)p;}
xd3d_state_t xd3d_state;
'''+enums+'\n'+state+'\nstatic d3d_state_t S;\n'
    source+=''.join(function(runtime,'xv_d3d_'+n) for n in setters)
    source+=function(kernel,'xd3d_texture_state')+function(kernel,'xd3d_texture_states')
    source+=function(runtime,'gl_blend_to_d3d')+function(runtime,'xv_d3d_SyncDrawState')
    source+=(ROOT/'tools/tests/draw_state_reference.h').read_text()
    source+=function((ROOT/'runtime/xv_ui_gxm.c').read_text(),'sync_draw_state')
    source+='''
static uint32_t rng=1;
static uint32_t next(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void fill(void *p,size_t n) {unsigned char *b=p;while(n--)*b++=(unsigned char)next();}
int main(void) {
 uint32_t blends[]={0,1,0x300,0x301,0x302,0x303,0x304,0x305,0x306,0x307,0x308,0xffffffff};
 uint32_t ops[]={0,0x800a,0x800b,0x8007,0x8008,0xffffffff};
 for(unsigned trial=0;trial<10000;trial++) {
  fill(&xd3d_state,sizeof xd3d_state);fill(guest,sizeof guest);fill(attrs,sizeof attrs);
  xd3d_state.ps_key=trial%2?next():0;
  xd3d_state.src_blend=blends[trial%12];xd3d_state.dst_blend=blends[(trial/12)%12];
  xd3d_state.blend_op=ops[(trial/144)%6];xd3d_state.z_func=0x1ff+trial%10;
  xd3d_state.cull=trial%3==0?0x900:trial%3==1?0x901:next();
  xd3d_state_t before=xd3d_state,after;d3d_state_t expected;
  uint32_t live[128];memcpy(live,guest,sizeof guest);
  memset(&S,0xa5,sizeof S);sync_calls=0;reference_sync_draw_state();
  assert(sync_calls==1);memcpy(&expected,&S,sizeof S);after=xd3d_state;
  xd3d_state=before;memset(&S,0xa5,sizeof S);sync_calls=0;sync_draw_state();
  assert(sync_calls==1 && !memcmp(&expected,&S,sizeof S));
  assert(!memcmp(&after,&xd3d_state,sizeof after) && !memcmp(live,guest,sizeof guest));
 }
 puts("PASS: 10000 full recorder-state/guest-state cases; live texture reads, byte truncation, border ARGB, blend/depth/cull conversions and PS-key fallback");
}
'''
    with tempfile.TemporaryDirectory() as t:
        p=Path(t);(p/'test.c').write_text(source)
        cmd=['cc','-std=gnu11','-O2','-fno-strict-aliasing','-Wall','-Wextra','-Werror','-I',str(ROOT)]
        if os.getenv('SANITIZE'):cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        subprocess.run(cmd+[str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':main()
