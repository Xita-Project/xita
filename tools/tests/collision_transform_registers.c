/* Differential leaf-transform fixtures: full context, aliases, page edges and exceptional floats. */
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
void reference_000B6210(xctx *); void candidate_000B6210(xctx *);
void reference_000B5E40(xctx *); void candidate_000B5E40(xctx *);
void reference_000B5DF0(xctx *); void candidate_000B5DF0(xctx *);
#define SIZE (4u<<20)
void x_guest_read_pages(void *out,uint32_t a,size_t n) {
    unsigned char *p=out; while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(p,X_G(a),k);a+=k;p+=k;n-=k;}
}
void x_guest_write_pages(uint32_t a,const void *in,size_t n) {
    const unsigned char *p=in;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(X_G(a),p,k);a+=k;p+=k;n-=k;}
}
/* Shared REP STOSD stand-in: checks the transform's exact primitive contract.
 * Runtime watch callbacks are outside this lowering test. */
void x_str_stos(xctx *c,unsigned sz,int mode) {
    assert(sz==4 && mode==X_STR_REP);
    while(c->r[1]){uint32_t v=c->r[0];x_guest_write(c->r[7],&v,4);
        c->r[7]+=c->df ? (uint32_t)-4 : 4;c->r[1]--;}
}
void xv_preempt(xctx *c) { (void)c;fputs("unexpected back-edge\n",stderr);abort(); }
static void wf(uint32_t a,float v) { x_guest_write(a,&v,4); }
static xctx setup(unsigned i,unsigned which) {
    memset(g_xram,0,SIZE); xctx c={0};
    for(unsigned k=0;k<8;k++){c.r[k]=0x78900000u+k;c.st[k]=(double)(k+i)*0.125;}
    c.r[4]=0xd003e000;c.fsp=(i/9)&7;c.fsw=i*37;c.fcw=0x37f|(((i/72)&3)<<10);c.df=(i/36)&1;
    c.f_kind=XK_SUB;c.f_op1=i;c.f_op2=i+1;c.f_res=-1;c.f_bits=32;c.preempt=100;
    uint32_t matrix=0x40018000,vec=0x4001a000,out=0x40022000;
    switch(i%9){
    case 1:out=matrix;break;case 2:out=matrix+4;break;case 3:out=vec;break;
    case 4:matrix+=0xffc;break;case 5:vec+=0xffc;break;case 6:out+=0xffc;break;
    case 7:out=c.r[4]-8;break;case 8:out=matrix+0x400000;break;
    }
    for(unsigned k=0;k<13;k++)wf(matrix+4*k,(float)((int)((i*7+k*13)%101)-50)/16);
    for(unsigned k=0;k<3;k++)wf(vec+4*k,(float)((int)((i*3+k*17)%73)-36)/8);
    static const uint32_t edge[]={0x3f800000,0x40000000,0,0x80000000,1,0x80000001,
        0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0x7f801234};
    X_M32(matrix)=edge[(i/9)%12];
    if(i>=864)X_M32((which?vec:matrix)+4*((i/108)%3))=edge[(i/72)%12];
    wf(0x1f0a68,0);wf(0x1f0a78,1);
    X_M32(c.r[4])=0x12345678;c.r[0]=out;c.r[1]=matrix;c.r[2]=vec;return c;
}
static double bench(void (*fn)(xctx *),xctx initial,unsigned n) {
    struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
    volatile unsigned sum=0;
    for(unsigned i=0;i<n;i++){xctx c=initial;fn(&c);sum+=c.r[0];}
    clock_gettime(CLOCK_MONOTONIC,&b);(void)sum;
    return ((b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec)/n;
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IOLBF,0);
    unsigned first=argc>1&&!strcmp(argv[1],"vector")?1:0;
    unsigned last=argc>1&&!strcmp(argv[1],"inverse")?1:2;
    if(argc>1&&!strcmp(argv[1],"rotation")){first=2;last=3;}
    else if(argc<=1||!strcmp(argv[1],"all"))last=3;
    g_xram=calloc(1,SIZE);g_img_base=g_xram;g_xpt=malloc((1u<<20)*4);
    unsigned char *before=malloc(SIZE),*expected=malloc(SIZE);
    assert(g_xram&&g_xpt&&before&&expected);
    for(unsigned i=0;i<(1u<<20);i++)g_xpt[i]=(i&1023u)*4096u;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
    xv_host_page_table=g_xpt;
#endif
    void (*ref[3])(xctx *)={reference_000B6210,reference_000B5E40,reference_000B5DF0};
    void (*cand[3])(xctx *)={candidate_000B6210,candidate_000B5E40,candidate_000B5DF0};
    for(unsigned which=first;which<last;which++) {
        for(unsigned i=0;i<1728;i++) {
            xctx c=setup(i,which),a=c,b=c;memcpy(before,g_xram,SIZE);
            ref[which](&a);memcpy(expected,g_xram,SIZE);memcpy(g_xram,before,SIZE);
            cand[which](&b);
            if(memcmp(&a,&b,sizeof a)||memcmp(expected,g_xram,SIZE)) {
                fprintf(stderr,"function %u case %u differs: context %d memory %d\n",which,i,
                    memcmp(&a,&b,sizeof a)!=0,memcmp(expected,g_xram,SIZE)!=0);
                for(unsigned k=0;k<8;k++){uint64_t aa,bb;memcpy(&aa,&a.st[k],8);memcpy(&bb,&b.st[k],8);
                    if(aa!=bb)fprintf(stderr,"st%u %016llx / %016llx\n",k,(unsigned long long)aa,(unsigned long long)bb);}
                for(unsigned k=0,n=0;k<SIZE&&n<12;k++)if(expected[k]!=g_xram[k]){
                    fprintf(stderr,"memory %08x %02x / %02x\n",k,expected[k],g_xram[k]);n++;}
                free(expected);free(before);free(g_xpt);free(g_xram);return 1;
            }
        }
        printf("function %u: 1728 full-context/memory cases passed\n",which);
        xctx c=setup(0,which);
        for(unsigned r=0;r<4;r++) {
            double a,b;
            if(r&1){b=bench(cand[which],c,100000);a=bench(ref[which],c,100000);}
            else {a=bench(ref[which],c,100000);b=bench(cand[which],c,100000);}
            printf("function %u repetition %u reference %.1f registers %.1f ns/call (includes context reset)\n",which,r,a,b);
        }
    }
    free(expected);free(before);free(g_xpt);free(g_xram);return 0;
}
