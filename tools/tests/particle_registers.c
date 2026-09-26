/* Synthetic differential fixture: full arena/context and callee observations.
 * Callees deliberately change scratch registers, flags, x87 slots and outputs,
 * while preserving their known stack effects. No claim about real collisions. */
#include "xv_x86rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
__thread uint32_t *xv_host_page_table;
#endif
void particle_reference(xctx *); void particle_candidate(xctx *);
#define SIZE (4u<<20)
static unsigned seed=1, scenario, calls, preempts;
static uint64_t trace;
static unsigned rnd(void) { seed=seed*1664525u+1013904223u; return seed; }
static void hash(const void *ptr,size_t n)
{ const unsigned char *p=ptr; while(n--) trace=(trace^*p++)*1099511628211ull; }
void x_guest_read_pages(void *out,uint32_t a,size_t n)
{ unsigned char *p=out; while(n) {size_t k=4096-(a&4095);if(k>n)k=n;memcpy(p,X_G(a),k);a+=k;p+=k;n-=k;} }
void x_guest_write_pages(uint32_t a,const void *in,size_t n)
{ const unsigned char *p=in;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(X_G(a),p,k);a+=k;p+=k;n-=k;} }
void xv_preempt(xctx *c) { ++preempts; c->preempt=10; }
static void wf(uint32_t a,float f) { x_guest_write(a,&f,4); }
static void observe(xctx *c,unsigned id)
{
    ++calls; hash(&id,sizeof id); hash(c,sizeof *c);
    unsigned char args[24];x_guest_read(args,c->r[4],sizeof args);hash(args,sizeof args);
    /* Preserve TOP, but every scratch slot and status may change at a call. */
    for(unsigned i=0;i<8;i++)c->st[i]=(double)(i+scenario%17)*0.125;
    c->fsw=(uint16_t)(scenario*29u);c->r[0]=scenario&1u;
    c->r[1]=0x40010200;c->r[2]=0x40010240;
    c->f_kind=XK_LOGIC;c->f_op1=0;c->f_op2=0;c->f_res=scenario&1u;c->f_bits=32;
}
void f_000571F0(xctx *c)
{
    uint32_t out=c->r[7];observe(c,0x571f0);
    for(unsigned i=0;i<3;i++)wf(out+4*i,(float)(i+1)*0.125f);
    c->r[4]+=12;
}
void f_00057810(xctx *c)
{
    uint32_t out=X_M32(c->r[4]+8);observe(c,0x57810);
    for(unsigned i=0;i<3;i++)wf(out+4*i,(float)(i+1)*0.125f);
    c->r[4]+=16;
}
void f_001721B0(xctx *c)
{
    uint32_t out=X_M32(c->r[4]+20);observe(c,0x1721b0);
    unsigned char zero[68]={0};x_guest_write(out,zero,sizeof zero);
    X_M16(out)=scenario%3;X_M32(out+12)=UINT32_MAX;
    wf(out+4,0.5f);for(unsigned i=0;i<3;i++)wf(out+24+4*i,0.25f*(i+1));
    wf(out+44,1.0f);c->r[0]=(scenario>>1)&1;c->r[4]+=24;
}
int main(int argc,char **argv)
{
    unsigned count=argc>1?strtoul(argv[1],0,0):1000;
    g_xram=calloc(1,SIZE);g_img_base=g_xram;g_xpt=malloc((1u<<20)*4);
    uint8_t *before=malloc(SIZE),*expected=malloc(SIZE);
    assert(g_xram&&g_xpt&&before&&expected);
    for(unsigned i=0;i<(1u<<20);i++)g_xpt[i]=(i&1023u)*4096u;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
    xv_host_page_table=g_xpt;
#endif
    unsigned total_calls=0;
    for(scenario=0;scenario<count;scenario++) {
        memset(g_xram,0,SIZE);
        xctx initial={0};initial.r[4]=0xd003e000;initial.fsp=scenario&7;initial.fcw=0x37f;
        initial.fsw=(uint16_t)rnd();initial.preempt=scenario&1?0:100;
        for(unsigned i=0;i<8;i++)initial.st[i]=(double)(int32_t)rnd()/65536;
        for(unsigned a=0x1f0000;a<0x280000;a+=4)wf(a,0.5f);
        wf(0x1f0a68,0);wf(0x1f0a64,1);
        uint32_t rec=0x40010000, pos=0x40012ffc, vec=0x40014000;
        for(unsigned a=0x10000;a<0x16000;a+=4)wf(a,(float)((int)(rnd()%101)-50)/16);
        X_M32(rec)=scenario&63;
        uint32_t args[11]={0x12345678,scenario&7,rec,0x40015000,vec,pos,
            scenario&1?0x40015100:0,scenario&2?0x40015200:0,scenario&4?0x40015300:0,0,0};
        x_guest_write(initial.r[4],args,sizeof args);
        wf(initial.r[4]+36,0.125f);wf(initial.r[4]+40,scenario%11?1.0f/30.0f:0.0f);
        memcpy(before,g_xram,SIZE);xctx ref=initial;calls=preempts=0;trace=1469598103934665603ull;
        particle_reference(&ref);unsigned rc=calls,rp=preempts;uint64_t rt=trace;memcpy(expected,g_xram,SIZE);
        memcpy(g_xram,before,SIZE);xctx got=initial;calls=preempts=0;trace=1469598103934665603ull;
        particle_candidate(&got);
        if(memcmp(&ref,&got,sizeof ref)||memcmp(expected,g_xram,SIZE)||rc!=calls||rp!=preempts||rt!=trace) {
            fprintf(stderr,"case %u mismatch: ctx %d memory %d calls %u/%u preempts %u/%u trace %llx/%llx\n",scenario,
                memcmp(&ref,&got,sizeof ref)!=0,memcmp(expected,g_xram,SIZE)!=0,rc,calls,rp,preempts,
                (unsigned long long)rt,(unsigned long long)trace);return 1;
        }
        total_calls+=calls;
    }
    printf("%u particle lowering cases passed; %u synthetic callee observations\n",count,total_calls);
    free(expected);free(before);free(g_xpt);free(g_xram);return 0;
}
