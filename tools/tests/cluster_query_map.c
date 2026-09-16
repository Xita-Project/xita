/* Owned-map numerical oracle. The reference reads the original BSP bytes;
 * the candidate reads a separately decoded, compact immutable snapshot.
 * No live game hook, snapshot lifetime or guest-state publication is modeled. */
#include "xv_x86rt.h"
#include "kernel/xk_cluster_query.h"
#include <fenv.h>
#include <stdlib.h>
#include <string.h>

enum { LOW_BYTES=4<<20, SP=0x2b0000 };
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static unsigned yields;
const size_t xv_test_map_layout[]={sizeof(XvCluster),sizeof(XvPortal),sizeof(XvPoint),
    sizeof(XvPlane),sizeof(XvClusterGeometry),sizeof(XvClusterInput),sizeof(XvClusterResult)};
void f_00056670(xctx *);
void __wrap_xv_preempt(xctx *c) { yields++; c->preempt=1000000; }

void xv_test_map_free(void)
{
    free(g_xram);free(g_xpt);
    g_xram=g_img_base=NULL;g_xpt=NULL;
}

int xv_test_map_load(const uint8_t *data,uint32_t size,uint32_t address,
                     uint32_t root,const uint8_t *axes)
{
    uint32_t offset=address&4095u;
    if(!data||!axes||!size||size>64u*1024*1024||address<LOW_BYTES||
       address>UINT32_MAX-size||root<address||root-address>size||
       size-(root-address)<0x160)return 0;
    uint32_t mapped=(offset+size+4095u)&~4095u;
    xv_test_map_free();
    g_xram=calloc(1,2*LOW_BYTES+mapped);g_xpt=calloc(1<<20,4);
    if(!g_xram||!g_xpt){xv_test_map_free();return 0;}
    g_img_base=g_xram+LOW_BYTES+mapped;
    for(unsigned i=0;i<LOW_BYTES/4096;i++)g_xpt[i]=(i^1u)*4096;
    for(unsigned i=0;i<mapped/4096;i++)g_xpt[(address>>12)+i]=LOW_BYTES+i*4096;
    memcpy(g_xram+LOW_BYTES+offset,data,size);
    X_IMG32(0x39be58)=root;
    X_IMG32(0x39be50)=X_M32(root+0xb4);
    x_guest_write(0x1eaf30,axes,24);
    uint32_t zero=0;x_guest_write(0x1f0a68,&zero,4);
    return 1;
}

/* 0 exact numerical result; 1 conservative decline; negative = mismatch.
 * stats: original count, backedges, scheduler yields. */
int xv_test_map_query(const XvClusterGeometry *geometry,const XvClusterInput *in,
                     unsigned round,XvClusterResult *out,uint32_t stats[3])
{
    static const int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    if(round>=4||!g_xram||!in||!out||!stats)return -1;
    fenv_t initial;fegetenv(&initial);fesetround(rounds[round]);feclearexcept(FE_ALL_EXCEPT);
    xctx c={0};
    for(unsigned i=0;i<8;i++){c.r[i]=0x13570000+i*13;c.st[i]=i+.25;}
    c.r[0]=SP+64;c.r[4]=SP;c.fsp=3;c.fcw=0x37f;c.preempt=in->budget;
    X_M16(SP+68)=(uint16_t)in->start;X_M32(SP+12)=SP+80;
    x_guest_write(SP+80,in->center,12);x_guest_write(SP+16,&in->radius,4);
    X_M32(SP+8)=0;X_IMG32(0x2d2fac)=in->epoch;X_IMG8(0x2d2fa9)=0;
    x_guest_write(0x2d2fb0,in->visited,1024);yields=0;
    f_00056670(&c);
    stats[0]=(uint16_t)c.r[0];stats[1]=in->budget-(unsigned)c.preempt;stats[2]=yields;
    feclearexcept(FE_ALL_EXCEPT);
    int admitted=xv_cluster_query_direct(geometry,in,out),result=0;
    if(!admitted)result=1;
    else if(yields||out->backedges!=stats[1])result=-2;
    else if(out->count!=stats[0])result=-3;
    else if(out->epoch!=X_IMG32(0x2d2fac))result=-4;
    else{
        for(unsigned i=0;i<out->count&&i<64;i++)
            if(out->clusters[i]!=X_M16(SP-128+i*2)){result=-5;break;}
        for(unsigned i=0;i<256&&!result;i++){
            uint32_t expected=out->changed[i>>5]&(1u<<(i&31))?out->epoch:in->visited[i];
            if(expected!=X_M32(0x2d2fb0+i*4))result=-6;
        }
    }
    fesetenv(&initial);return result;
}
