#include "kernel/xk_surface_scan.h"
#include <stddef.h>
#include <stdlib.h>
#ifndef TEST_ARM
#include <assert.h>
#include <stdio.h>
#endif

enum { ARENA=1<<20, PAGES=ARENA/4096 };
uint8_t *g_xram,*g_img_base;
uint32_t *g_xpt;
static xctx context;
xctx *const arm_context_ptr=&context;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),
 offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
unsigned scan_entries,scan_batches,scan_yields,scan_mutation;
void original_surface_scan(xctx *,uint8_t *,const uint32_t *);
void candidate_surface_scan(xctx *,uint8_t *,const uint32_t *);
static void put(uint32_t a,uint32_t value){memcpy(X_G(a),&value,4);}
static uint32_t get(uint32_t a){uint32_t value;memcpy(&value,X_G(a),4);return value;}

void __wrap_xv_preempt(xctx *c)
{
    scan_yields++;
    c->preempt=5;
    if(scan_yields!=1)return;
    switch(scan_mutation){
    case 1:c->r[1]-=2;break;
    case 2:put(c->r[4]+0x14,c->r[3]+8);break;
    case 3:c->r[3]+=12;break;
    case 4:put(0x7014,get(c->r[4]+0x14));c->r[4]=0x7000;break;
    case 5:{
        unsigned page=c->r[3]>>12,old=g_xpt[page];
        /* Relocate the live page, preserving a bound that may alias it. The
         * physical overhang also preserves the lift's unaligned reads. */
        memcpy(g_xram+0x90000,g_xram+old,4100);g_xpt[page]=0x90000;break;
    }
    case 6:put(c->r[3]+4,c->r[1]);break;
    }
}

void arm_prepare(unsigned n,unsigned variant,unsigned budget)
{
    for(unsigned i=0;i<PAGES;i++)g_xpt[i]=(i^1)*4096;
    memset(g_xram,0x33,ARENA);memset(&context,0xa5,sizeof context);
    unsigned thresholds[]={1,0,0x7fffffff,0x80000000,0x80000001};
    unsigned threshold=thresholds[(variant/8)%5];
    uint32_t cursor=0x20000+((variant%8)*4);
    if(variant&64)cursor=0x20ff0;
    if(variant&128)cursor++;
    if(variant&256)cursor=UINT32_MAX-3;
    context.r[2]=cursor;context.r[3]=cursor;context.r[1]=threshold;context.r[4]=0x6000;
    if(variant&512)context.r[4]=0x6fe9; /* unaligned bound load */
    context.preempt=budget;
    for(unsigned i=1;i<=n+1;i++)put(cursor+4*i,i<=n?threshold-1:threshold);
    put(context.r[4]+0x14,cursor+4*(n+(variant&1?1:2)));
    /* A bound stored inside the scanned page is still a RAM alias. */
    if(variant&1024){context.r[4]=cursor+4*n-0x14;put(context.r[4]+0x14,cursor+4*(n+1));}
    scan_mutation=(variant/2048)%7;scan_entries=scan_batches=scan_yields=0;
}
void arm_original(void){original_surface_scan(&context,g_xram,g_xpt);}
void arm_candidate(void){candidate_surface_scan(&context,g_xram,g_xpt);}
void test_boot(void){}

#ifndef TEST_ARM
int main(void)
{
    g_xram=malloc(ARENA);g_xpt=calloc(1<<20,4);
    unsigned char *before=malloc(ARENA),*expected=malloc(ARENA);
    uint32_t initial_pages[PAGES],expected_pages[PAGES];
    unsigned cases=0,batches=0,yields=0;
    unsigned sizes[]={0,1,2,3,15,127,128,129,255,257,511};
    for(unsigned k=0;k<1540;k++){
        unsigned n=sizes[k%11],variant=(k*103)%14336,budget=(unsigned[]){1,2,3,7,128,10000}[(k/11)%6];
        if(getenv("SURFACE_CASE_TRACE"))fprintf(stderr,"case %u size %u variant %u budget %u\n",k,n,variant,budget);
        arm_prepare(n,variant,budget);
        xctx before_ctx=context;memcpy(before,g_xram,ARENA);memcpy(initial_pages,g_xpt,sizeof initial_pages);
        arm_original();xctx expected_ctx=context;
        memcpy(expected,g_xram,ARENA);memcpy(expected_pages,g_xpt,sizeof expected_pages);
        unsigned expected_yields=scan_yields;
        context=before_ctx;memcpy(g_xram,before,ARENA);memcpy(g_xpt,initial_pages,sizeof initial_pages);
        scan_entries=scan_batches=scan_yields=0;arm_candidate();
        assert(!memcmp(&context,&expected_ctx,sizeof context));
        assert(!memcmp(g_xram,expected,ARENA));assert(!memcmp(g_xpt,expected_pages,sizeof expected_pages));
        assert(scan_yields==expected_yields);batches+=scan_batches;yields+=scan_yields;cases++;
    }
    assert(batches&&yields);
    printf("PASS %u full context/arena/page-table comparisons, %u admitted batches, %u matching yields\n",cases,batches,yields);
    free(before);free(expected);free(g_xram);free(g_xpt);
}
#endif
