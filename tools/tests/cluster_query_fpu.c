/* Independent numerical and complete x87-state oracle. GP/scratch publication
 * is deliberately outside this fixture and remains an integration requirement. */
#define main unused_numerical_fixture_main
#include "cluster_query.c"
#undef main

int main(void)
{
    g_xram=calloc(1,ARENA);g_img_base=g_xram+RAM;g_xpt=calloc(1<<20,4);
    unsigned char *original_memory=malloc(ARENA);
    assert(g_xram&&g_xpt&&original_memory);
    for(unsigned i=0;i<RAM/4096;i++)g_xpt[i]=(i^1u)*4096;
    unsigned sizes[]={1,2,7,31,65,128,256};
    int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    unsigned comparisons=0,declines=0;
    for(unsigned round=0;round<4;round++)for(unsigned k=0;k<CASES;k++){
        fesetround(rounds[round]);XvClusterInput in;xctx original;
        prepare(k,sizes[k%7],(k/7)%8,&in,&original);
        if(k&1)for(unsigned i=0;i<8;i++){
            uint64_t bits=UINT64_C(0x7ff8123456780000)+k+i;memcpy(&original.st[i],&bits,8);
        }
        if(k%23==0){in.radius=INFINITY;fp32(SP+16,in.radius);}
        else if(k%29==0){in.radius=NAN;fp32(SP+16,in.radius);}
        if(k%31==0){in.budget=1;original.preempt=1;}
        XvClusterFpu fp={.entry_fsp=original.fsp,.fsw=original.fsw};
        for(unsigned i=0;i<8;i++)memcpy(&fp.slots[i],&original.st[(original.fsp+i)&7],8);
        feclearexcept(FE_ALL_EXCEPT);feraiseexcept(FE_DIVBYZERO);
        f_00056670(&original);int expected_flags=fetestexcept(FE_ALL_EXCEPT);
        memcpy(original_memory,g_xram,ARENA);
        feclearexcept(FE_ALL_EXCEPT);feraiseexcept(FE_DIVBYZERO);
        XvClusterResult result;
        if(!xv_cluster_query_fpu(&geometry,&in,&result,&fp)){declines++;continue;}
        require(!yields,"accepted query does not cross scheduler boundary");
        require(expected_flags==fetestexcept(FE_ALL_EXCEPT),"native FP exception flags");
        require(fp.entry_fsp==original.fsp&&fp.fsw==original.fsw,"FSP and full FSW");
        for(unsigned i=0;i<8;i++)
            if(memcmp(&fp.slots[i],&original.st[(original.fsp+i)&7],8)){
                fprintf(stderr,"case %u round %u relative FP slot %u expected %.17g actual %.17g\n",
                        k,round,i,original.st[(original.fsp+i)&7],fp.slots[i]);abort();
            }
        require(result.count==(uint16_t)original.r[0]&&result.epoch==X_IMG32(0x2d2fac),"numerical result");
        require(result.backedges==in.budget-(unsigned)original.preempt,"backedges");
        for(unsigned i=0;i<result.count&&i<64;i++)require(result.clusters[i]==X_M16(SP-128+2*i),"ordered clusters");
        for(unsigned i=0;i<256;i++){
            uint32_t stamp=result.changed[i>>5]&(1u<<(i&31))?result.epoch:in.visited[i];
            require(stamp==X_M32(0x2d2fb0+i*4),"visited result");
        }
        require(!memcmp(original_memory,g_xram,ARENA),"candidate writes no guest memory");
        comparisons++;
    }
    require(comparisons>CASES*3,"stateful query coverage");
    printf("PASS %u exact numerical/x87/native-exception comparisons, %u conservative declines; GP/scratch replay still unproved\n",comparisons,declines);
    free(original_memory);free(g_xram);free(g_xpt);return 0;
}
