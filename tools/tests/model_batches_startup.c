/* Actual startup body and original override APIs; each case is a fresh process. */
#define _POSIX_C_SOURCE 200809L
#include "kernel/xk.h"
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define XV_LOG(...) printf(__VA_ARGS__)
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
static unsigned phase,override_calls,guards;
extern char **environ;
int xv_math_model_palette(xctx *),xv_math_model_hierarchy(xctx *);
void xv_model_palette_override(int),xv_model_hierarchy_override(int);
void __real_xv_model_palette_override(int),__real_xv_model_hierarchy_override(int);
void __wrap_xv_model_palette_override(int value)
{
    assert(phase==2||phase==4);if(phase==2)assert(value==1&&!guards);
    override_calls++;__real_xv_model_palette_override(value);
}
void __wrap_xv_model_hierarchy_override(int value)
{
    assert(phase==2||phase==4);if(phase==2)assert(value==1&&!guards);
    override_calls++;__real_xv_model_hierarchy_override(value);
}
void xv_object_math_report_check(void) { assert(phase==2||phase==4); }
int xv_object_math_lock(void) {assert(phase==3||phase==4);guards++;return 0;}
void xv_object_math_unlock(int *token) {assert(!*token);}
void xk_os_log(const char *fmt,...) {(void)fmt;}
#include "model_batches_startup.inc"

static int probe_palette(void)
{
    memset(g_xram,0,4<<20);
    unsigned model=0x12000,nodes=0x20000,pose=0x22000,sp=0x60000;
    X_M32(model+0xb8)=1;X_M32(model+0xbc)=nodes;
    const float identity[13]={1,1,0,0,0,1,0,0,0,1,0,0,0};
    memcpy(X_G(nodes+0x68),identity,sizeof identity);memcpy(X_G(pose),identity,sizeof identity);
    xctx c={0};c.r[4]=sp;c.r[5]=model;c.r[7]=pose;c.preempt=100;c.fcw=0x27f;
    return xv_math_model_palette(&c);
}
static int probe_hierarchy(void)
{
    memset(g_xram,0,4<<20);
    unsigned model=0x30000,nodes=0x31000,poses=0x32000,matrices=0x34000,sp=0x50000;
    X_M32(model+0xb8)=8;X_M32(model+0xbc)=nodes;
    X_M32(sp+0x24)=matrices;X_M32(sp+0x28)=poses;X_M32(sp+0x2c)=model;
    X_M32(sp+0x10)=2;X_M16(sp+0x178)=0;X_M16(sp+0x17a)=1;
    for(unsigned n=0;n<8;n++) {
        X_M16(nodes+n*156+0x20)=n==7?0xffff:n+1;X_M16(nodes+n*156+0x22)=0xffff;
        X_M16(nodes+n*156+0x24)=n?n-1:0xffff;
        X_MF32(poses+n*32+12)=1;X_MF32(poses+n*32+28)=1;
    }
    X_M32(0x1f0a68)=0;X_M32(0x1f0a78)=0x3f800000;X_M32(0x1f0b04)=0x40000000;
    X_MF32(matrices)=X_MF32(matrices+4)=X_MF32(matrices+20)=X_MF32(matrices+36)=1;
    xctx c={0};c.r[4]=sp;c.r[0]=1;c.preempt=100;c.fcw=0x27f;
    return xv_math_model_hierarchy(&c);
}
static unsigned long environment_hash(void)
{
    unsigned long h=5381;
    for(char **p=environ;*p;p++)for(const char *s=*p;;s++) {h=h*33+(unsigned char)*s;if(!*s)break;}
    return h;
}
int main(int argc,char **argv)
{
    assert(argc==5);int want_p=atoi(argv[1]),want_h=atoi(argv[2]);
    int configured_p=atoi(argv[3]),configured_h=atoi(argv[4]);
    phase=1;xv_load_settings();
    /* Fixture simulates dashboard saving a different config before its real
     * final loader call; startup must select from this final environment. */
    assert(!rename("dashboard.cfg","ux0:data/xita/xita.cfg"));
    xv_load_settings();phase=2;
    unsigned long before=environment_hash();
#if XV_MODEL_BATCHES_TRIAL
    xv_model_batches_trial_startup();assert(override_calls==2);
#else
    assert(!override_calls);
#endif
    assert(environment_hash()==before);
    phase=3;g_xram=calloc(1,4<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    assert(g_xram&&g_xpt);for(unsigned i=0;i<1024;i++)g_xpt[i]=i*4096;
    assert(probe_palette()==want_p);assert(probe_hierarchy()==want_h);
    /* Existing APIs retain availability gating and negative reset semantics. */
    phase=4;xv_model_palette_override(0);xv_model_hierarchy_override(0);
    assert(!probe_palette()&&!probe_hierarchy());
    xv_model_palette_override(-1);xv_model_hierarchy_override(-1);
    assert(probe_palette()==configured_p);assert(probe_hierarchy()==configured_h);
    assert(environment_hash()==before);free(g_xram);free(g_xpt);
    puts("PASS startup overrides precede guest work; exact config/environment retained; reset semantics");
}
