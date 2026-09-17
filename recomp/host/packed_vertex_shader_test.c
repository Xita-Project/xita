/* Production shader admission and patch creation with owned embedded GXP
 * bytes. SDK parameter lookup/patcher are stubs, not a shader execution test. */
#define XV_PACKED_VERTEX_LAYOUT 1
#include <assert.h>
#include "../../runtime/xv_shader.c"
#include "../../shaders/xv_layouts.h"
#include "../../runtime/xv_draw_state.h"
static const xv_vs_desc_t *current;
static unsigned calls,releases,mask,fail_packed,bound_calls;
static SceGxmVertexAttribute original_attrs[16];
static unsigned original_count;
static const SceGxmVertexProgram *last_bound;
void xv_logf(const char *fmt,...) {(void)fmt;}
SceUID sceIoOpen(const char *p,int f,SceMode m) {(void)p;(void)f;(void)m;return -1;}
int sceIoClose(SceUID f) {(void)f;return 0;}
SceOff sceIoLseek(SceUID f,SceOff p,int w) {(void)f;(void)p;(void)w;return -1;}
SceSSize sceIoRead(SceUID f,void *p,SceSize n) {(void)f;(void)p;(void)n;return -1;}
int sceGxmProgramCheck(const SceGxmProgram *p) {return memcmp(p,"GXP",3)?-1:0;}
unsigned sceGxmProgramGetSize(const SceGxmProgram *p) {uint32_t n;memcpy(&n,(const uint8_t *)p+8,4);return n;}
const SceGxmProgramParameter *sceGxmProgramFindParameterByName(const SceGxmProgram *p,const char *name)
{
    (void)p;if(!strncmp(name,"IN.",3))name+=3;
    if(!strcmp(name,"c"))return NULL;
    for(unsigned i=0;i<current->nattrs;i++)if(!strcmp(name,current->attrs[i].name))
        return (mask&(1u<<i))?(const SceGxmProgramParameter *)(uintptr_t)(i+1):NULL;
    return NULL;
}
unsigned sceGxmProgramParameterGetResourceIndex(const SceGxmProgramParameter *p) {return (uintptr_t)p-1;}
int sceGxmShaderPatcherRegisterProgram(SceGxmShaderPatcher *p,const SceGxmProgram *g,SceGxmShaderPatcherId *i)
{(void)p;(void)g;*i=(SceGxmShaderPatcherId)(uintptr_t)1;return 0;}
int sceGxmShaderPatcherUnregisterProgram(SceGxmShaderPatcher *p,SceGxmShaderPatcherId i) {(void)p;(void)i;return 0;}
int sceGxmShaderPatcherCreateVertexProgram(SceGxmShaderPatcher *p,SceGxmShaderPatcherId id,
 const SceGxmVertexAttribute *a,unsigned na,const SceGxmVertexStream *s,unsigned ns,SceGxmVertexProgram **out)
{
    (void)p;(void)id;assert(ns==current->nstreams);
    if(!calls) {assert(s[0].stride==current->stride[0]);memcpy(original_attrs,a,na*sizeof *a);original_count=na;}
    else {
        assert(calls==1 && s[0].stride==16 && na==original_count && !memcmp(a,original_attrs,na*sizeof *a));
        if(ns==2)assert(s[1].stride==8);
    }
    calls++;if(calls==2 && fail_packed)return -1;
    *out=(SceGxmVertexProgram *)(uintptr_t)(calls*16);return 0;
}
int sceGxmShaderPatcherReleaseVertexProgram(SceGxmShaderPatcher *p,SceGxmVertexProgram *v)
{(void)p;assert(v);releases++;return 0;}
void sceGxmSetFrontDepthFunc(SceGxmContext *c,SceGxmDepthFunc d) {(void)c;(void)d;}
void sceGxmSetFrontDepthWriteEnable(SceGxmContext *c,SceGxmDepthWriteMode d) {(void)c;(void)d;}
void sceGxmSetCullMode(SceGxmContext *c,SceGxmCullMode d) {(void)c;(void)d;}
void sceGxmSetFragmentProgram(SceGxmContext *c,const SceGxmFragmentProgram *p) {(void)c;(void)p;}
void sceGxmSetVertexProgram(SceGxmContext *c,const SceGxmVertexProgram *p) {(void)c;bound_calls++;last_bound=p;}
static void load_case(const xv_vs_desc_t *d,unsigned active,int eligible)
{
    current=d;mask=active;calls=releases=0;xv_vshader_t v;
    assert(!xv_vshader_load(&v,d) && v.vprog);
    assert(calls==1u+eligible && !!v.packed_vprog==(eligible&&!fail_packed));
    if(v.packed_vprog) {
        xv_draw_state state={0};SceGxmVertexProgram *sequence[]={v.packed_vprog,v.packed_vprog,v.vprog,v.packed_vprog};
        bound_calls=0;
        for(unsigned i=0;i<4;i++) {
            xv_draw_state_bind(&state,NULL,0,0,0,sequence[i],NULL);
            assert(last_bound==sequence[i]);
        }
        assert(bound_calls==3); /* program identity survives state-cache reuse */
        uint8_t *mutable=(uint8_t *)(uintptr_t)v.prog;mutable[100]^=1;
        assert(!packed_layout(&v,original_attrs,original_count));mutable[100]^=1;
    }
    unsigned programs=1+!!v.packed_vprog;xv_vshader_unload(&v);assert(releases==programs);
}
int main(void)
{
    const xv_vs_desc_t *d[]={&xv_vs_halo_vs_06,&xv_vs_halo_vs_29,&xv_vs_halo_vs_58};
    unsigned active[]={1,3,65};
    for(unsigned i=0;i<3;i++) {
        load_case(d[i],active[i],1);
        fail_packed=1;load_case(d[i],active[i],1);fail_packed=0;
        load_case(d[i],active[i]|4,0);load_case(d[i],0,0);
        xv_vs_desc_t changed=*d[i];changed.stride[0]=36;load_case(&changed,active[i],0);
        changed=*d[i];changed.func_hash^=1;load_case(&changed,active[i],0);
        xv_attr_desc_t attrs[7];memcpy(attrs,d[i]->attrs,d[i]->nattrs*sizeof *attrs);
        changed=*d[i];changed.attrs=attrs;attrs[0].components=4;load_case(&changed,active[i],0);
    }
    load_case(&xv_vs_halo_vs_40,3,0);
    puts("PASS actual shader loader: three exact layouts/GXPs; unknown/mutated declaration, binding and program declines; allocation failure keeps original; raw/packed binding transitions and release");
}
