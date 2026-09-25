#include <stdio.h>
#include <stdlib.h>
#include <fenv.h>
#include <time.h>
#include "xv_x86rt.h"
#include "xk_point_location.h"
#ifdef PL_TEST_HOOK
#include "xk_point_location_hook.h"
void xk_os_log(const char *fmt,...) { fputs(fmt,stderr); }
#endif
#define BYTES (4u*1024u*1024u)
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
__thread uint32_t *xv_host_page_table;
#endif
static unsigned calls, trace;
void xv_preempt(xctx *c) {
    const unsigned char *p=(const unsigned char *)c;
    for(unsigned i=0;i<sizeof *c;i++) trace=(trace^p[i])*16777619u;
    ++calls; c->preempt=7;
    /* A resumed tick may change input memory; the native must reload it. */
    X_M8(c->r[2]) ^= 0x11u;
}
void x_guest_read_pages(void *d,uint32_t a,size_t n) {
    unsigned char *p=d;
    while(n--) *p++=X_M8(a++);
}
void x_guest_write_pages(uint32_t a,const void *d,size_t n) {
    const unsigned char *p=d;
    while(n--) X_M8(a++)=*p++;
}
void xv_trap(xctx *c,uint32_t e) { (void)c;(void)e;abort(); }
void f_0017A8B0(xctx *);
static unsigned rng=0x17a8b0;
static unsigned rnd(void) { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }
static void putf(uint32_t a,unsigned word) { x_guest_write(a,&word,4); }
static __attribute__((noinline)) void candidate(xctx *c) {
#ifdef PL_TEST_HOOK
    xv_point_location_hook(c,f_0017A8B0);
#else
    xv_point_location_body(c);
#endif
}
static double now(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9; }
int main(int argc,char **argv) {
    (void)argv;
#ifdef PL_TEST_HOOK
    pl_mode=1;
#endif
    g_xram=calloc(1,BYTES);g_img_base=g_xram;g_xpt=malloc(1024*4);
    unsigned char *saved=malloc(BYTES),*expected=malloc(BYTES);
    for(unsigned i=0;i<1024;i++) g_xpt[i]=i*4096;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
    xv_host_page_table=malloc(1024*4);memcpy(xv_host_page_table,g_xpt,1024*4);
#endif
    if(argc>1) {
        const uint32_t node=0x20000,plane=0x60000,point=0x70000,root=0x10000;
        X_M32(root+4)=node;X_M32(root+16)=plane;
        for(unsigned i=0;i<32;i++) {
            X_M32(node+12*i)=i;
            X_M32(node+12*i+4)=X_M32(node+12*i+8)=i==31?0x80000003u:i+1;
            for(unsigned j=0;j<4;j++) putf(plane+16*i+4*j,0x3f000000u+j*0x10000u);
        }
        for(unsigned j=0;j<3;j++) putf(point+4*j,0x3f000000u);
        void (*fn[2])(xctx *)={f_0017A8B0,candidate};
        for(unsigned k=0;k<2;k++) {
            xctx c;memset(&c,0,sizeof c);c.fcw=0x37f;c.preempt=1000000000;
            double start=now();
            for(unsigned i=0;i<200000;i++) {c.r[0]=0;c.r[1]=root;c.r[2]=point;c.r[4]=0x90000;fn[k](&c);}
            printf("%s %.1f ns/call leaf %u\n",k?"candidate":"reference",(now()-start)*1e9/200000,c.r[0]);
        }
        return 0;
    }
    for(unsigned test=0;test<4096;test++) {
        const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
        if(fesetround(modes[test%4])) abort();
        /* Shuffle data pages; float sources deliberately cross page boundaries. */
        for(unsigned i=32;i<96;i++) X_PT[i]=(32+(i-32+test)%64)*4096;
        uint32_t node=0x20000,plane=0x60000+(test%4),point=0x70ffe;
        uint32_t root=0x10000;
        if(test%11==0) point=0x8fff8;
        X_M32(root+4)=node;X_M32(root+16)=plane;
        unsigned depth=1+test%63;
        for(unsigned i=0;i<depth;i++) {
            X_M32(node+12*i)=i;
            unsigned child=i+1==depth ? (test%3 ? 0x80000003u : 0xffffffffu) : i+1;
            X_M32(node+12*i+4)=child;X_M32(node+12*i+8)=child^((i+1==depth && test%3)?2u:0u);
            for(unsigned j=0;j<4;j++) putf(plane+16*i+4*j,rnd());
        }
        for(unsigned j=0;j<3;j++) putf(point+4*j,rnd());
        putf(0x1f0a68,(test%7)?0:rnd());
        xctx c;memset(&c,0,sizeof c);
        for(unsigned j=0;j<8;j++) { c.r[j]=rnd();c.st[j]=(double)(int)rnd(); }
        c.r[0]=0;c.r[1]=root;c.r[2]=point;c.r[4]=0x90000;
        c.fsp=test%8;c.fsw=rnd();c.fcw=0x37f;c.preempt=1+test%13;
#ifdef PL_TEST_HOOK
        c.preempt=1<<24; /* replay verifier deliberately defers scheduling */
#endif
        memcpy(saved,g_xram,BYTES);xctx ref=c;calls=0;trace=0;feclearexcept(FE_ALL_EXCEPT);f_0017A8B0(&ref);int refenv=fetestexcept(FE_ALL_EXCEPT);unsigned refcalls=calls,reftrace=trace;memcpy(expected,g_xram,BYTES);
        memcpy(g_xram,saved,BYTES);calls=0;trace=0;feclearexcept(FE_ALL_EXCEPT);candidate(&c);int env=fetestexcept(FE_ALL_EXCEPT);
        if(memcmp(&c,&ref,sizeof c)||memcmp(g_xram,expected,BYTES)||calls!=refcalls||trace!=reftrace||env!=refenv) {
            fprintf(stderr,"mismatch case %u preempts %u/%u\n",test,calls,refcalls);
            for(unsigned k=0;k<sizeof c;k++) if(((unsigned char *)&c)[k]!=((unsigned char *)&ref)[k]) fprintf(stderr,"context byte %u: %02x/%02x\n",k,((unsigned char *)&c)[k],((unsigned char *)&ref)[k]);
            return 1;
        }
    }
#ifdef PL_TEST_HOOK
    if(pl_verified!=4096||pl_mismatch) {fprintf(stderr,"mismatch case verifier counters %u/%u\n",pl_verified,pl_mismatch);return 1;}
#endif
    puts("PASS 4096 full-context/memory/preemption-trace/FP-exception comparisons");return 0;
}
