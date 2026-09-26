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
void effect_reference(xctx *); void effect_candidate(xctx *);
#define SIZE (4u<<20)
static unsigned seed=1, scenario, calls, preempts, conversions, iterations;
static uint64_t trace;
static unsigned steps;
static unsigned guard_misses, guard_ip;
void xv_x87reg_miss(xctx *c, uint32_t ip)
{ (void)c; ++guard_misses; guard_ip=ip; }
void effect_test_step(unsigned ip)
{ if (++steps > 100000) { fprintf(stderr,"step limit scenario=%u ip=%x calls=%u\n",scenario,ip,calls); abort(); } }
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
/* Synthetic contracts: side effects are observed at every call, and all x87
 * slots/status change. These are NOT implementations of game callees. */
static void mock(xctx *c,unsigned id,unsigned pop,int delta)
{
    observe(c,id);
    c->fsp=(c->fsp-(unsigned)delta)&7;
    if(id==0x1d150) c->r[0]=(conversions++&1)?1:2;
    if(id==0x110e50) c->r[0]=(iterations++ == 0)?0x40014000:0;
    if(id==0x110d30 || id==0x111530) c->st[c->fsp]=0.5;
    c->r[4]+=pop;
}
#define MOCK(addr,pop,delta) void f_##addr(xctx *c){mock(c,0x##addr,pop,delta);}
MOCK(0001D150,4,-1) MOCK(00049600,8,0) MOCK(00047520,8,0)
MOCK(0004B330,8,0) MOCK(0004B820,12,0) MOCK(00040EF0,32,0)
MOCK(0004B720,8,0) MOCK(00058B20,12,0) MOCK(0008C9D0,4,0)
MOCK(0008E970,12,0) MOCK(000B0CB0,8,0) MOCK(000B6560,4,0)
MOCK(00111530,16,1) MOCK(00110D30,12,1) MOCK(0010E8B0,4,0)
MOCK(00110E50,8,0) MOCK(00111950,40,0) MOCK(001260F0,12,0)
MOCK(0014AE40,4,0) MOCK(001731D0,24,0) MOCK(00172DE0,4,0)
MOCK(001721B0,24,0)
void xv_trap(xctx *c,uint32_t ip)
{ (void)c; fprintf(stderr,"unexpected trap %x scenario %u\n",ip,scenario); abort(); }
void xv_call(xctx *c,uint32_t target)
{
    uint32_t a=c->r[1],b=c->r[2];
    mock(c,target,8,scenario&8?1:0); /* also exercise a failed call-depth guard */
    for(unsigned i=0;i<3;i++){wf(a+4*i,0.125f*(i+1));wf(b+4*i,0.25f*(i+1));}
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
    unsigned total_calls=0, fallback_cases=0, register_cases=0, fallback_tops=0;
    for(scenario=0;scenario<count;scenario++) {
        memset(g_xram,0,SIZE);
        /* Keep entry TOP independent of the low bits selecting effect paths. */
        xctx initial={0};initial.r[4]=0xd003e000;initial.fsp=(scenario>>7)&7;initial.fcw=0x37f;
        initial.fsw=(uint16_t)rnd();initial.preempt=scenario&1?0:100;
        for(unsigned i=0;i<8;i++)initial.st[i]=(double)(int32_t)rnd()/65536;
        for(unsigned a=0x1f0000;a<0x280000;a+=4)wf(a,0.5f);
        wf(0x1f0a68,0);wf(0x1f0a64,1);
        uint32_t rec=0x40010000, tags=0x40011000, tag=0x40012000;
        uint32_t events=0x40013000, parts=0x40015000;
        X_M32(0x39ce24)=tags; X_M32(tags+20)=tag;
        X_M32(rec+4)=0; X_M16(rec+0x4e)=0;
        X_M32(tag+0x38)=events; X_M32(tag+0x28)=1;
        X_M32(events+0x38)=1; X_M32(events+0x3c)=parts;
        X_M16(parts+8)=0; X_M16(parts+2)=0;
        X_M8(rec+0xdc)=1; X_M16(parts+0x68)=1;
        for(unsigned a=0x10010;a<0x10050;a+=4)wf(0x40000000+a,0.5f);
        X_M16(rec+0x4e)=0;
        wf(rec+0x50,0.25f);wf(rec+0x54,scenario%3?0.5f:0);
        wf(rec+0x58,0.125f);
        for(unsigned a=0x70;a<0xb0;a+=4)wf(parts+a,(float)(rnd()%17)/16);
        X_M32(parts+0xe0)=scenario&0x1ff;X_M32(parts+0xe4)=(scenario>>1)&0x1ff;
        X_M32(parts)=scenario%8;
        uint32_t args[2]={0x12345678,rec}; x_guest_write(initial.r[4],args,sizeof args);
        memcpy(before,g_xram,SIZE);xctx ref=initial;calls=preempts=conversions=iterations=steps=0;trace=1469598103934665603ull;
        guard_misses=0;
        effect_reference(&ref);assert(!guard_misses);
        unsigned rc=calls,rp=preempts;uint64_t rt=trace;memcpy(expected,g_xram,SIZE);
        memcpy(g_xram,before,SIZE);xctx got=initial;calls=preempts=conversions=iterations=steps=0;trace=1469598103934665603ull;
        effect_candidate(&got);
        if(guard_misses) { ++fallback_cases; fallback_tops|=1u<<initial.fsp; }
        else ++register_cases;
        if(memcmp(&ref,&got,sizeof ref)||memcmp(expected,g_xram,SIZE)||rc!=calls||rp!=preempts||rt!=trace) {
            fprintf(stderr,"case %u mismatch: ctx %d memory %d calls %u/%u preempts %u/%u trace %llx/%llx\n",scenario,
                memcmp(&ref,&got,sizeof ref)!=0,memcmp(expected,g_xram,SIZE)!=0,rc,calls,rp,preempts,
                (unsigned long long)rt,(unsigned long long)trace);return 1;
        }
        total_calls+=calls;
    }
    assert(total_calls > count);
    /* A passing comparison must not silently omit the recovery path. */
    if(count>=1000) assert(fallback_cases && register_cases && fallback_tops==255);
    printf("%u effect lowering cases passed; %u synthetic callee observations\n",count,total_calls);
    printf("guard coverage: fallback=%u no-fallback=%u entry-TOP-mask=%02x last-IP=%08x\n",
           fallback_cases,register_cases,fallback_tops,guard_ip);
    free(expected);free(before);free(g_xpt);free(g_xram);return 0;
}
