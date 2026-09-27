#include "xv_x86rt.h"
#include <stddef.h>
uint8_t *g_xram,*g_img_base; uint32_t *g_xpt;
static xctx context;
xctx *arm_context_ptr=&context;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
unsigned mask_yields,mask_mutation;
xctx mask_trace[512];
void original(xctx*);void candidate(xctx*);
static void put(uint32_t a,uint32_t v){memcpy(X_G(a),&v,4);}
void xv_preempt(xctx *c){
    if(mask_yields>=512)__builtin_trap();
    mask_trace[mask_yields++]=*c;c->preempt=7;
    if(mask_yields!=1)return;
    switch(mask_mutation){
    case 1:put(0x8000,0x80004001u);break;
    case 2:memcpy(g_xram+0x18000,X_G(0x8000),4096);X_PT[8]=0x18000;break;
    case 3:put(0x70f8,1);break;
    case 4:c->r[2]++;c->r[6]+=6;break;
    case 5:c->f_cf=1;c->f_of=1;c->f_cf_override=1;c->scratch^=0x12345678;break;
    case 6:memcpy(X_G(0x4000+(c->r[4]&4095)),X_G(c->r[4]),64);c->r[4]=0x4000+(c->r[4]&4095);break;
    }
}
/* Keep the live table deliberately different from the bound thread table. */
void arm_bind(uint32_t *pages){
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
    static uint32_t live_pages[2048];
    g_xpt=live_pages;
    __asm__ volatile("mcr p15, 0, %0, c13, c0, 2" :: "r"(pages) : "memory");
#else
    g_xpt=pages;
#endif
}
void arm_prepare(unsigned k){
    for(unsigned i=0;i<2048;i++)X_PT[i]=(i^1)*4096;
    memset(g_xram,0,8u<<20);memset(&context,0xa5,sizeof context);
    context.r[4]=0x6000;context.preempt=(unsigned[]){1,7,10000}[(k/64)%3];
    X_IMG32(0x39be58)=0x7000;put(0x70f8,(unsigned[]){0,1,31,32,33,64,65,128}[(k/8)%8]);
    put(0x70fc,0x10000);put(0x6008,0x20000);put(0x600c,0x8000);put(0x6010,0x30000);
    uint32_t mask=(uint32_t[]){0,1,0x80000000u,0xffffffffu,0xaaaaaaaa,0x10001,0x80004001,0x40000000}[k%8];
    for(unsigned j=0;j<4;j++)put(0x8000+4*j,mask);
    for(unsigned j=0;j<1024;j++)*(uint8_t*)X_G(0x10000+j)=(uint8_t)(j*31);
    mask_mutation=k%7;mask_yields=0;memset(mask_trace,0,sizeof mask_trace);
}
void arm_original(void){original(&context);}
void arm_candidate(void){candidate(&context);}
void test_boot(void){}
