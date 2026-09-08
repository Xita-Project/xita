#include <assert.h>
#include <stdio.h>
#include "../../xv_d3d.c"

static unsigned calls[5];
void xv_logf(const char *fmt,...) {(void)fmt;}
void sceGxmSetFrontDepthFunc(SceGxmContext *c,SceGxmDepthFunc v){(void)c;(void)v;calls[0]++;}
void sceGxmSetFrontDepthWriteEnable(SceGxmContext *c,SceGxmDepthWriteMode v){(void)c;(void)v;calls[1]++;}
void sceGxmSetCullMode(SceGxmContext *c,SceGxmCullMode v){(void)c;(void)v;calls[2]++;}
void sceGxmSetVertexProgram(SceGxmContext *c,const SceGxmVertexProgram *v){(void)c;(void)v;calls[3]++;}
void sceGxmSetFragmentProgram(SceGxmContext *c,const SceGxmFragmentProgram *v){(void)c;(void)v;calls[4]++;}

int main(void)
{
    xv_draw_state s={0};
    SceGxmVertexProgram *vp=(void *)1;SceGxmFragmentProgram *fp=(void *)2;
    for(unsigned i=0;i<100;++i)xv_draw_state_bind(&s,NULL,SCE_GXM_DEPTH_FUNC_LESS,SCE_GXM_DEPTH_WRITE_ENABLED,SCE_GXM_CULL_CW,vp,fp);
    for(unsigned i=0;i<5;++i)assert(calls[i]==1);
    xv_draw_state_bind(&s,NULL,SCE_GXM_DEPTH_FUNC_ALWAYS,SCE_GXM_DEPTH_WRITE_ENABLED,SCE_GXM_CULL_CW,vp,fp);
    assert(calls[0]==2&&calls[1]==1&&calls[4]==1);
    s.valid=0; /* UI, clear or new scene: bind every field again. */
    xv_draw_state_bind(&s,NULL,SCE_GXM_DEPTH_FUNC_ALWAYS,SCE_GXM_DEPTH_WRITE_ENABLED,SCE_GXM_CULL_CW,vp,fp);
    assert(calls[0]==3);for(unsigned i=1;i<5;++i)assert(calls[i]==2);

    cmdlist_t *l=calloc(1,sizeof *l);assert(l);g_lists[0]=l;g_build_frame=0;
    xv_d3d_Clear(X_D3DCLEAR_TARGET,0x1234,.1f,0);
    xv_d3d_Clear(X_D3DCLEAR_ZBUFFER,0x5678,.5f,0);
    assert(l->ncmds==1&&l->cmds[0].clear_flags==3&&l->cmds[0].clear_color==0x1234&&l->cmds[0].clear_z==.5f);
    xv_d3d_Clear(X_D3DCLEAR_TARGET,0xabcd,1,0);
    assert(l->ncmds==1&&l->cmds[0].clear_color==0xabcd&&l->cmds[0].clear_z==.5f);
    xv_d3d_Clear(X_D3DCLEAR_STENCIL,0,0,0x181);
    assert(l->ncmds==1&&l->cmds[0].clear_flags==7&&l->cmds[0].clear_stencil==0x81);
    xv_d3d_Clear(X_D3DCLEAR_TARGET,0xabcd,0,0);
    assert(l->cmds[0].clear_stencil==0x81&&l->cmds[0].clear_z==.5f);
    assert(xv_d3d_record_ui(0,0));
    xv_d3d_Clear(X_D3DCLEAR_TARGET,1,1,0);assert(l->ncmds==2);
    l->cur_pass=1;xv_d3d_Clear(X_D3DCLEAR_TARGET,2,1,0);assert(l->ncmds==3);
    l->active_visibility=1;xv_d3d_Clear(X_D3DCLEAR_TARGET,3,1,0);assert(l->ncmds==4);
    l->active_visibility=0;xv_d3d_Clear(X_D3DCLEAR_TARGET,4,1,0);assert(l->ncmds==5);
    assert(new_cmd());xv_d3d_Clear(X_D3DCLEAR_TARGET,5,1,0);assert(l->ncmds==7);
    free(l);
    /* Byte-level constant identity is necessary: floats with different NaN
     * payloads or signed zero are not interchangeable shader inputs. Exercise
     * the actual setter and its generation, including UI's partial overrides. */
    float constants[192][4] = {{0}};
    uint8_t *bytes = (uint8_t *)constants;
    for (unsigned mode = 0; mode < 2; mode++) {
        xv_d3d_draw_scan_override(mode);
        for (unsigned i = 0; i < sizeof constants; i++) bytes[i] = (uint8_t)(i * 13);
        xv_d3d_SetAllConstants(constants);
        for (unsigned i = 0; i < sizeof constants; i++) {
            unsigned before = S.vsc_gen;
            xv_d3d_SetAllConstants(constants);
            assert(S.vsc_gen == before);
            bytes[i] ^= 0x80;
            xv_d3d_SetAllConstants(constants);
            assert(S.vsc_gen == before + 1);
            assert(!memcmp(S.vsc, constants, sizeof constants));
        }
        const uint32_t bit_patterns[] = {0x7fc00001, 0x7fc00002, 0x80000000, 0};
        float row[4]; memcpy(row, bit_patterns, sizeof row);
        xv_d3d_SetVertexShaderConstant(-81, row, 1);
        unsigned before = S.vsc_gen;
        xv_d3d_SetAllConstants(constants);
        assert(S.vsc_gen == before + 1 && !memcmp(S.vsc, constants, sizeof constants));
        S.vsc_gen = UINT32_MAX; bytes[sizeof constants - 1] ^= 1;
        xv_d3d_SetAllConstants(constants);
        assert(S.vsc_gen == 0 && !memcmp(S.vsc, constants, sizeof constants));
    }
    xv_d3d_draw_scan_override(-1);
    puts("PASS: repeated state binds removed; clear values and UI/target/query/draw boundaries preserved");
    puts("PASS: exact constants and generations at every byte, partial UI writes and wrap, both scan modes");
}
