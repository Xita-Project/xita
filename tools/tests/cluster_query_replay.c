/* Full-prefix context and memory oracle for the typed private replay. The
 * original list/allocation tail and real concurrent publication are separate. */
#define main unused_numerical_fixture_main
#include "cluster_query.c"
#undef main
#include "kernel/xk_cluster_query_replay.h"
#ifdef CLUSTER_TAIL
void f_query_whole(xctx *),f_query_tail(xctx *);
enum { LISTS=0x110000,HEADS=0x111000,POOL0=0x112000,POOL1=0x113000,
       NODES0=0x114000,NODES1=0x118000,LIGHT=0x11c000 };
static void lists(xctx *c,unsigned k)
{
    unsigned capacities[]={0,1,8,1024};
    static const unsigned char zero[12288];
    c->r[7]=LISTS;w32(SP+4,0x80000000+k);w32(SP+8,LIGHT);w32(LIGHT,UINT32_MAX);
    w32(LISTS,HEADS);w32(LISTS+4,POOL0);w32(LISTS+8,POOL1);
    for(unsigned i=0;i<256;i++)w32(HEADS+i*4,UINT32_MAX);
    for(unsigned i=0;i<2;i++){
        unsigned p=i?POOL1:POOL0,nodes=i?NODES1:NODES0;
        put(p,zero,0x38);put(nodes,zero,sizeof zero);
        w16(p+0x20,capacities[(k/7+i)%4]);w16(p+0x22,12);
        w16(p+0x32,0x8001);w32(p+0x34,nodes);
    }
}
#endif

int main(void)
{
    g_xram=calloc(1,ARENA);g_img_base=g_xram+RAM;g_xpt=calloc(1<<20,4);
    unsigned char *initial=malloc(ARENA),*expected=malloc(ARENA);
    XvClusterReplay *replay=malloc(sizeof *replay);
    assert(g_xram&&g_xpt&&initial&&expected&&replay);
    for(unsigned i=0;i<RAM/4096;i++)g_xpt[i]=(i^1u)*4096;
    unsigned sizes[]={1,2,7,31,65,128,256};
    int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    unsigned comparisons=0,declines=0;
    for(unsigned round=0;round<4;round++)for(unsigned k=0;k<CASES;k++){
        fesetround(rounds[round]);XvClusterInput in;xctx entry;
        prepare(k,sizes[k%7],(k/7)%8,&in,&entry);
#ifdef CLUSTER_TAIL
        lists(&entry,k);
#endif
        entry.f_cf=k%3;entry.f_of=k%5;entry.df=k&1;entry.eip_hint=0xabc56789;
        if(k&1)for(unsigned i=0;i<8;i++){
            uint64_t bits=UINT64_C(0x7ff8123456780000)+k+i;memcpy(&entry.st[i],&bits,8);
        }
        if(k%23==0){in.radius=INFINITY;fp32(SP+16,in.radius);}
        else if(k%29==0){in.radius=NAN;fp32(SP+16,in.radius);}
        if(k%31==0){in.budget=1;entry.preempt=1;}
        uint32_t adj[256],planes[MAX_PORTALS];
        for(unsigned i=0;i<geometry.cluster_count;i++)adj[i]=X_M32(CLUSTERS+i*104+0x60);
        for(unsigned i=0;i<geometry.portal_count;i++)planes[i]=X_M32(PORTALS+i*64+4);
        XvClusterReplayLayout layout={BSP,CLUSTERS,PORTALS,PLANES2,adj,planes,SP+80,0};
#ifdef CLUSTER_TAIL
        layout.head_address=LIGHT;
#endif
        memcpy(initial,g_xram,ARENA);xctx original=entry;
        feclearexcept(FE_ALL_EXCEPT);feraiseexcept(FE_DIVBYZERO);
#ifdef CLUSTER_TAIL
        f_query_whole(&original);
#else
        f_00056670(&original);
#endif
        int expected_flags=fetestexcept(FE_ALL_EXCEPT);
        memcpy(expected,g_xram,ARENA);memcpy(g_xram,initial,ARENA);
        feclearexcept(FE_ALL_EXCEPT);feraiseexcept(FE_DIVBYZERO);
        XvClusterResult result;
        if(!xv_cluster_query_replay(&geometry,&in,&layout,&entry,&result,replay)){
            require(!memcmp(initial,g_xram,ARENA),"decline writes no guest memory");declines++;continue;
        }
        require(!yields,"accepted query does not yield");
        require(expected_flags==fetestexcept(FE_ALL_EXCEPT),"complete native exception flags");
#ifndef CLUSTER_TAIL
        if(memcmp(&original,&replay->context,sizeof original)){
            const uint32_t *a=(void*)&original,*b=(void*)&replay->context;
            for(unsigned i=0;i<sizeof original/4;i++)if(a[i]!=b[i])
                fprintf(stderr,"case %u round %u context offset %u original %08x candidate %08x\n",k,round,i*4,a[i],b[i]);
            abort();
        }
#endif
        require(!memcmp(initial,g_xram,ARENA),"accepted query writes no guest memory");
        /* Modeled publication after successful completion. This fixture has
         * disjoint stable mappings and no other writer; it is not a live gate. */
        for(unsigned i=0;i<XV_CLUSTER_SCRATCH/2;i++)
            if(replay->dirty[i/32]&(1u<<(i%32)))put(SP-XV_CLUSTER_SCRATCH+i*2,replay->scratch+i*2,2);
        if(result.epoch!=in.epoch){
            X_IMG32(0x2d2fac)=result.epoch;X_IMG8(0x2d2fa9)=0;
            for(unsigned i=0;i<256;i++)
                if(result.changed[i>>5]&(1u<<(i&31)))w32(0x2d2fb0+i*4,result.epoch);
        }
#ifdef CLUSTER_TAIL
        f_query_tail(&replay->context);
        require(!memcmp(&original,&replay->context,sizeof original),"full context after original allocation/list tail");
        require(expected_flags==fetestexcept(FE_ALL_EXCEPT),"native flags after original tail");
        require(original.r[4]==SP+20,"original ret-16 stack convention");
#endif
        if(memcmp(expected,g_xram,ARENA)){
            unsigned differences=0;
            for(unsigned i=0;i<ARENA;i++)if(expected[i]!=g_xram[i]){
                if(differences++<20)fprintf(stderr,"case %u round %u physical %08x original %02x candidate %02x\n",k,round,i,expected[i],g_xram[i]);
            }
            fprintf(stderr,"%u differing bytes\n",differences);abort();
        }
        comparisons++;
    }
    require(comparisons>CASES*3,"full-prefix coverage");
    printf("PASS %u complete context/memory/native-exception comparisons; %u conservative declines; ",comparisons,declines);
#ifdef CLUSTER_TAIL
    puts("original allocator/list tail included; no live publication proof");
#else
    puts("prefix only; no live publication or allocator-tail proof");
#endif
    free(initial);free(expected);free(replay);free(g_xram);free(g_xpt);return 0;
}
