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
void object_walk_reference(xctx *); void object_walk_candidate(xctx *);
#define SIZE (4u<<20)
static unsigned seed=1, scenario, calls, preempts;
static uint64_t trace;
static unsigned steps;
static unsigned guard_misses, guard_ip, mode, depth, max_depth;
static unsigned covered[8], callee_tops[8];
void xv_x87reg_miss(xctx *c, uint32_t ip)
{ (void)c; ++guard_misses; guard_ip=ip; }
void object_walk_step(unsigned ip)
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
    ++calls;hash(&id,sizeof id);hash(c,sizeof *c);
    unsigned char args[24];x_guest_read(args,c->r[4],sizeof args);hash(args,sizeof args);
    for(unsigned i=0;i<8;i++)c->st[i]=(double)(i+scenario%17)*0.125;
    c->fsw=(uint16_t)(scenario*29u);c->r[0]=scenario&1u;
    c->r[1]=0x40010200;c->r[2]=0x40010240;
    c->f_kind=XK_LOGIC;c->f_op1=0;c->f_op2=0;c->f_res=scenario&1u;c->f_bits=32;
}
static void mock(xctx *c,unsigned id,unsigned pop,unsigned index)
{
    uint32_t sp=c->r[4],a=c->r[0],cx=c->r[1],bx=c->r[3];
    callee_tops[index]|=1u<<c->fsp;observe(c,id);covered[index]++;
    if(id==0x172de0) {
        X_M32(cx+4)=0x40017000;X_M32(cx+12)=0x40018000;
        c->r[0]=(scenario>>1)&1;
    }
    if(id==0x1731d0) {
        uint32_t out=X_M32(sp+20);
        for(unsigned j=0;j<0x30;j+=4)X_M32(out+j)=0;
        wf(out+8,scenario%5==0?2.0f:0.125f);X_M32(out+20)=scenario&16?0xffffffffu:0;
        c->r[0]=(scenario>>2)&1;
    }
    if(id==0x81900) { for(unsigned j=0;j<32;j+=4)X_M32(a+j)=0; c->r[0]=(scenario>>2)&1; }
    if(id==0x81a10) { for(unsigned j=0;j<20;j+=4)wf(bx+j,0.125f);c->r[0]=(scenario>>3)&1; }
    if(id==0xb6560)for(unsigned j=0;j<12;j+=4)wf(a+j,0.25f);
    c->r[4]+=pop;
}
#define MOCK(addr,pop,index) void f_##addr(xctx *c){mock(c,0x##addr,pop,index);}
MOCK(000B0CB0,8,0) MOCK(00172DE0,4,1) MOCK(001731D0,24,2)
MOCK(000B6560,4,3) MOCK(000118F0,4,4) MOCK(00012340,4,5)
MOCK(00081900,8,6) MOCK(00081A10,12,7)
void f_00171AF0(xctx *c)
{
    if(++depth>8)abort();if(depth>max_depth)max_depth=depth;
    if(mode)object_walk_candidate(c);else object_walk_reference(c);
    --depth;
}
void xv_trap(xctx *c,uint32_t ip) { (void)c;fprintf(stderr,"unexpected trap %x\n",ip);abort(); }
int main(int argc,char **argv)
{
    unsigned count=argc>1?strtoul(argv[1],0,0):1000;
    g_xram=calloc(1,SIZE);g_img_base=g_xram;g_xpt=malloc((1u<<20)*4);
    uint8_t *before=malloc(SIZE),*expected=malloc(SIZE);assert(g_xram&&g_xpt&&before&&expected);
    for(unsigned i=0;i<(1u<<20);i++)g_xpt[i]=(i&1023u)*4096;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
    xv_host_page_table=g_xpt;
#endif
    unsigned total_calls=0, tops=0, deepest=0;
    for(scenario=0;scenario<count;scenario++) {
        memset(g_xram,0,SIZE);xctx initial={0};
        for(unsigned i=0;i<8;i++){initial.r[i]=rnd();initial.st[i]=(double)(int32_t)rnd()/65536;}
        initial.r[4]=0xd003e000;initial.r[1]=0;initial.r[2]=scenario&32?0x400fff:0xfff;
        initial.fsp=(scenario>>7)&7;initial.fcw=0x37f;initial.fsw=(uint16_t)rnd();initial.preempt=scenario&1?0:100;
        uint32_t table=0x40011000,entries=0x40012000,result=0x40019000;
        X_M32(0x2fc6ac)=table;X_M32(table+0x34)=entries;
        for(unsigned i=0;i<5;i++) {
            uint32_t obj=0x40013000+i*0x200;X_M32(entries+i*12+8)=obj;
            X_M8(obj+4)=(scenario%7==0 && i==1)?1:0;
            X_M16(obj+0x64)=scenario&32?1:i%4;
            wf(obj+0x5c,(float)(scenario%7)*0.25f);
            X_M32(obj+0xc4)=(i==2||i==4)?0xffffffffu:i+1;
            X_M32(obj+0xc8)=(i==0 && scenario&64)?3:0xffffffffu;
        }
        wf(result+0x14,scenario%11?1.0f:0.0f);
        uint32_t args[]={0x12345678,0x4001a000,0x4001b000,0x4001c000,scenario%13?0xffffffffu:1,result};
        x_guest_write(initial.r[4],args,sizeof args);memcpy(before,g_xram,SIZE);
        xctx ref=initial;mode=0;calls=preempts=steps=guard_misses=depth=max_depth=0;trace=1469598103934665603ull;
        f_00171AF0(&ref);assert(!guard_misses);unsigned rc=calls,rp=preempts;uint64_t rt=trace;memcpy(expected,g_xram,SIZE);
        memcpy(g_xram,before,SIZE);xctx got=initial;mode=1;calls=preempts=steps=guard_misses=depth=max_depth=0;trace=1469598103934665603ull;
        f_00171AF0(&got);
        if(memcmp(&ref,&got,sizeof ref)||memcmp(expected,g_xram,SIZE)||rc!=calls||rp!=preempts||rt!=trace||guard_misses) {
            fprintf(stderr,"case %u mismatch ctx=%d memory=%d calls=%u/%u trace=%llx/%llx\n",scenario,
                memcmp(&ref,&got,sizeof ref)!=0,memcmp(expected,g_xram,SIZE)!=0,rc,calls,
                (unsigned long long)rt,(unsigned long long)trace);return 1;
        }
        total_calls+=calls;tops|=1u<<initial.fsp;if(max_depth>deepest)deepest=max_depth;
    }
    printf("%u object-walk lowering cases passed, %u callee observations, TOP-mask=%02x depth=%u\n",count,total_calls,tops,deepest);
    printf("callee coverage:");for(unsigned i=0;i<8;i++){printf(" %u",covered[i]);if(count>=1000)assert(covered[i] && callee_tops[i]==255);}puts("");
    if(count>=1000)assert(tops==255 && deepest>=2);
    free(expected);free(before);free(g_xpt);free(g_xram);return 0;
}
