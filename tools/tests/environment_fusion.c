/* Synthetic compiler-fusion experiment, not whole-game validation. Covers
 * direct environment leaves and default vectors. The dynamic plane/material
 * branch is deliberately rejected by the external-callee stub below. */
#include "xv_x86rt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
__thread uint32_t *xv_host_page_table;
#endif
void reference_00057810(xctx *); void fused_00057810(xctx *);
#define SIZE (4u<<20)
void x_guest_read_pages(void *out,uint32_t a,size_t n) {
    unsigned char *p=out; while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(p,X_G(a),k);a+=k;p+=k;n-=k;}
}
void x_guest_write_pages(uint32_t a,const void *in,size_t n) {
    const unsigned char *p=in;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(X_G(a),p,k);a+=k;p+=k;n-=k;}
}
void f_000579D0(xctx *c) { (void)c;fputs("uncovered dynamic environment branch\n",stderr);abort(); }
void f_00057110(xctx *c) { (void)c;fputs("uncovered active environment branch\n",stderr);abort(); }
void xv_preempt(xctx *c) { (void)c;fputs("unexpected back-edge\n",stderr);abort(); }
static void wf(uint32_t a,float v) { x_guest_write(a,&v,4); }
static xctx setup(unsigned i) {
    memset(g_xram,0,SIZE); xctx c={0};
    for(unsigned k=0;k<8;k++){c.r[k]=0x78900000u+k;c.st[k]=(double)(k+i)*0.125;}
    c.r[4]=0xd003e000;c.r[0]=0x40010000;c.fsp=i&7;c.fsw=i*37;c.fcw=0x37f;
    c.f_kind=XK_SUB;c.f_op1=i;c.f_op2=i+1;c.f_res=-1;c.f_bits=32;c.preempt=100;
    X_M16(c.r[0]+4)=i%4==0?0xffff:0;
    X_M32(0x39be58)=0x40020000;X_M32(0x40020138)=0x40030000;
    X_M16(0x40030002)=i%4==1?0xffff:0;X_M16(0x40030008)=0xffff;
    X_M32(0x40020188)=0x40031000;X_M16(0x40031024)=0xffff;
    X_M32(0x206f90)=0x40032000;
    wf(0x40032000,(float)(i%31)/16);wf(0x40032004,-0.25f);wf(0x40032008,0.5f);
    uint32_t args[]={0x12345678,0x40033000,i&1?0x40034ffc:0x40034000,i&15};
    x_guest_write(c.r[4],args,sizeof args);return c;
}
static double bench(void (*fn)(xctx *),xctx initial,unsigned n) {
    struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
    volatile unsigned sum=0;
    for(unsigned i=0;i<n;i++){xctx c=initial;fn(&c);sum+=c.r[0];}
    clock_gettime(CLOCK_MONOTONIC,&b);(void)sum;
    return ((b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec)/n;
}
int main(void) {
    g_xram=calloc(1,SIZE);g_img_base=g_xram;g_xpt=malloc((1u<<20)*4);
    unsigned char *before=malloc(SIZE),*expected=malloc(SIZE);
    assert(g_xram&&g_xpt&&before&&expected);
    for(unsigned i=0;i<(1u<<20);i++)g_xpt[i]=(i&1023u)*4096u;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
    xv_host_page_table=g_xpt;
#endif
    for(unsigned i=0;i<1000;i++) {
        xctx c=setup(i),a=c,b=c;memcpy(before,g_xram,SIZE);
        reference_00057810(&a);memcpy(expected,g_xram,SIZE);memcpy(g_xram,before,SIZE);
        fused_00057810(&b);
        if(memcmp(&a,&b,sizeof a)||memcmp(expected,g_xram,SIZE)) {fprintf(stderr,"case %u differs\n",i);return 1;}
    }
    puts("1000 direct-leaf/default-vector cases: full context and arena match");
    for(unsigned i=0;i<4;i++) {
        xctx c=setup(i);
        for(unsigned r=0;r<4;r++) {
            double a,b;
            if(r&1){b=bench(fused_00057810,c,100000);a=bench(reference_00057810,c,100000);}
            else {a=bench(reference_00057810,c,100000);b=bench(fused_00057810,c,100000);}
            printf("scenario %u repetition %u reference %.1f fused %.1f ns/call (includes context reset)\n",i,r,a,b);
        }
    }
    free(expected);free(before);free(g_xpt);free(g_xram);return 0;
}
