#include <stdio.h>
#include <stdlib.h>
#include <fenv.h>
#include "xv_x86rt.h"
#include "xk_point_location.h"
#define BYTES (4u*1024u*1024u)
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
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
int main(void) {
    g_xram=calloc(1,BYTES);g_img_base=g_xram;g_xpt=malloc(1024*4);
    unsigned char *saved=malloc(BYTES),*expected=malloc(BYTES);
    for(unsigned i=0;i<1024;i++) g_xpt[i]=i*4096;
    for(unsigned test=0;test<4096;test++) {
        /* Shuffle data pages; float sources deliberately cross page boundaries. */
        for(unsigned i=32;i<96;i++) g_xpt[i]=(32+(i-32+test)%64)*4096;
        uint32_t node=0x20000,plane=0x60000+(test%4),point=0x70ffe;
        uint32_t root=0x10000;
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
        memcpy(saved,g_xram,BYTES);xctx ref=c;calls=0;trace=0;feclearexcept(FE_ALL_EXCEPT);f_0017A8B0(&ref);int refenv=fetestexcept(FE_ALL_EXCEPT);unsigned refcalls=calls,reftrace=trace;memcpy(expected,g_xram,BYTES);
        memcpy(g_xram,saved,BYTES);calls=0;trace=0;feclearexcept(FE_ALL_EXCEPT);xv_point_location_body(&c);int env=fetestexcept(FE_ALL_EXCEPT);
        if(memcmp(&c,&ref,sizeof c)||memcmp(g_xram,expected,BYTES)||calls!=refcalls||trace!=reftrace||env!=refenv) {
            fprintf(stderr,"mismatch case %u preempts %u/%u\n",test,calls,refcalls);
            for(unsigned k=0;k<sizeof c;k++) if(((unsigned char *)&c)[k]!=((unsigned char *)&ref)[k]) fprintf(stderr,"context byte %u: %02x/%02x\n",k,((unsigned char *)&c)[k],((unsigned char *)&ref)[k]);
            return 1;
        }
    }
    puts("PASS 4096 full-context/memory/preemption-trace/FP-exception comparisons");return 0;
}
