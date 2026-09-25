#include "kernel/xk_surface_mask_scan.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
enum {SIZE=8<<20};
uint8_t *g_xram,*g_img_base; uint32_t *g_xpt;
unsigned batches,yields,mutation;
static xctx trace[1024],expected_trace[1024];
void original(xctx*); void candidate(xctx*);
static void put(uint32_t a,uint32_t v){memcpy(X_G(a),&v,4);}
void xv_preempt(xctx *c){
    assert(yields<1024); trace[yields]=*c; yields++; c->preempt=7;
    if(yields==1) {
        if(mutation==1) put(0x8000,0x80004001u);
        if(mutation==2) {memcpy(g_xram+0x9000,g_xram+0x8000,4096);g_xpt[8]=0x9000;}
        if(mutation==3) put(0x70f8,1);
    }
}
int main(void){
    g_xram=malloc(SIZE);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    uint8_t *initial=malloc(SIZE),*expected=malloc(SIZE);
    for(unsigned i=0;i<SIZE/4096;i++)g_xpt[i]=i*4096;
    unsigned admitted=0,total=0;
    for(unsigned k=0;k<768;k++){
        g_xpt[8]=0x8000;memset(g_xram,0,SIZE);xctx c;memset(&c,0xa5,sizeof c);
        c.r[4]=0x6000;c.preempt=(int[]){1,2,3,7,64,10000}[k%6];
        put(0x39be58,0x7000);put(0x70f8,(k/6)%97);
        put(0x70fc,0x10000);put(0x6008,0x20000);
        put(0x600c,0x8000);put(0x6010,0x30000);
        uint32_t mask=(uint32_t[]){0,1,0x80000000u,0xffffffffu,0xaaaaaaaa,0x10001,0x80004001,0x40000000}[k/96];
        for(unsigned j=0;j<4;j++)put(0x8000+4*j,mask);
        for(unsigned j=0;j<1024;j++)g_xram[0x10000+j]=(uint8_t)(j*31);
        mutation=(k/48)%4;memcpy(initial,g_xram,SIZE);xctx before=c;
        yields=0;original(&c);xctx want=c;unsigned ny=yields;memcpy(expected_trace,trace,ny*sizeof(xctx));memcpy(expected,g_xram,SIZE);
        g_xpt[8]=0x8000;memcpy(g_xram,initial,SIZE);c=before;yields=0;batches=0;candidate(&c);
        if(memcmp(&want,&c,sizeof c)||memcmp(expected,g_xram,SIZE)||ny!=yields||memcmp(trace,expected_trace,ny*sizeof(xctx))){fprintf(stderr,"FAIL case %u yields %u/%u\n",k,ny,yields);return 1;}
        admitted+=batches;total+=ny;
    }
    /* Admission rejects aliases even when distinct guest pages share storage. */
    for(unsigned variant=0;variant<3;variant++) {
        memset(g_xram,0,SIZE);g_xpt[8]=0x8000;
        xctx c;memset(&c,0,sizeof c);c.r[4]=0x6000;c.preempt=64;
        uint8_t *root_slot=g_xram+0x39be58;
        put(0x39be58,0x7000);put(0x70f8,64);put(0x601c,0x8000);
        if(variant==0){root_slot=g_xram+0x6018;put(0xf8,64);}
        if(variant==1){put(0x39be58,0x5f20);put(0x6018,4);c.r[1]=4;}
        if(variant==2){g_xpt[8]=0x6000;put(0x601c,0x8018);}
        xctx before=c;memcpy(initial,g_xram,SIZE);
        assert(!xv_surface_mask_zero_run(&c,g_xram,g_xpt,root_slot));
        assert(!memcmp(&c,&before,sizeof c)&&!memcmp(initial,g_xram,SIZE));
    }
    assert(admitted);printf("PASS 768 full context/memory comparisons; %u batches, %u yields\n",admitted,total);
    if(getenv("MASK_BENCH")) {
        uint32_t masks[]={0,1,0x80000000u,0xffffffffu,0xaaaaaaaau,0x10001u};
        for(unsigned m=0;m<6;m++) {
            g_xpt[8]=0x8000;memset(g_xram,0,SIZE); put(0x39be58,0x7000);put(0x70f8,128);put(0x70fc,0x10000);
            put(0x6008,0x20000);put(0x600c,0x8000);put(0x6010,0x30000);
            for(unsigned j=0;j<4;j++)put(0x8000+4*j,masks[m]);
            xctx start;memset(&start,0,sizeof start);start.r[4]=0x6000;start.preempt=1000000;
            for(unsigned mode=0;mode<2;mode++) {
                struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
                for(unsigned i=0;i<50000;i++){put(0x6008,0x20000);put(0x600c,0x8000);put(0x6010,0x30000);xctx c=start;if(mode)candidate(&c);else original(&c);}
                clock_gettime(CLOCK_MONOTONIC,&b);
                printf("cost mask %08x mode %u %.1f ns/call\n",masks[m],mode,
                    ((b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec)/50000.0);
            }
        }
    }
    free(initial);free(expected);free(g_xram);free(g_xpt);
}
