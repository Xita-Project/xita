/* Test against the original full numerical prefix, including its complete
 * context and scratch. Only shared epoch/visited values change between runs.
 * This is NOT a typed guest-state reconstruction or a live publication hook. */
#define main unused_numerical_fixture_main
#include "cluster_query.c"
#undef main

int main(void)
{
    g_xram=calloc(1,ARENA);g_img_base=g_xram+RAM;g_xpt=calloc(1<<20,4);
    unsigned char *initial=malloc(ARENA),*original_output=malloc(ARENA),*current_output=malloc(ARENA);
    assert(g_xram&&g_xpt&&initial&&original_output&&current_output);
    for(unsigned i=0;i<RAM/4096;i++)g_xpt[i]=(i^1u)*4096;
    unsigned sizes[]={1,2,7,31,65,128,256};
    int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    unsigned comparisons=0,advanced=0,unchanged=0,conflicts=0,wraps=0;
    for(unsigned round=0;round<4;round++)for(unsigned k=0;k<256;k++){
        fesetround(rounds[round]);XvClusterInput in;xctx entry;
        unsigned n=sizes[k%7];prepare(k,n,(k/7)%8,&in,&entry);
        XvClusterResult result;require(xv_cluster_query_direct(&geometry,&in,&result),"ordinary typed result");
        memcpy(initial,g_xram,ARENA);xctx before=entry,after=entry;
        feclearexcept(FE_ALL_EXCEPT);feraiseexcept(FE_DIVBYZERO);
        f_00056670(&before);int flags=fetestexcept(FE_ALL_EXCEPT);
        memcpy(original_output,g_xram,ARENA);
        uint32_t current_epoch=k%3?in.epoch+1000u:UINT32_MAX,current_next=current_epoch+1u;
        uint32_t current_visited[256];
        for(unsigned i=0;i<256;i++){
            current_visited[i]=in.visited[i]==in.epoch+1u?current_next:0xabc10000u+k*256+i;
            if(in.visited[i]!=in.epoch+1u&&current_visited[i]==current_next)current_visited[i]^=1;
        }
        XvClusterResult old=result;
        int status=xv_cluster_query_rebase(&in,&result,n,current_epoch,current_visited);
        require(status!=XV_CLUSTER_REBASE_CONFLICT,"compatible epoch rebase");
        require(result.count==old.count&&!memcmp(result.clusters,old.clusters,sizeof result.clusters)&&
                !memcmp(result.changed,old.changed,sizeof result.changed)&&result.backedges==old.backedges,
                "rebase preserves traversal and budget");
        if(status==XV_CLUSTER_REBASE_EPOCH){advanced++;wraps+=current_next==0;}
        else unchanged++;
        /* Reference execution starts with the same private context/stack and
         * geometry, but with the new shared epoch and arbitrary inactive stamps. */
        memcpy(g_xram,initial,ARENA);X_IMG32(0x2d2fac)=current_epoch;
        put(0x2d2fb0,current_visited,sizeof current_visited);
        feclearexcept(FE_ALL_EXCEPT);feraiseexcept(FE_DIVBYZERO);
        f_00056670(&after);
        require(!memcmp(&before,&after,sizeof before),"full context invariant under compatible rebase");
        require(flags==fetestexcept(FE_ALL_EXCEPT),"native FP flags invariant");
        memcpy(current_output,g_xram,ARENA);
        /* The original private result, with only its epoch/visited publication
         * rebased, must match the entire current reference result byte-for-byte. */
        memcpy(g_xram,original_output,ARENA);
        X_IMG32(0x2d2fac)=status==XV_CLUSTER_REBASE_EPOCH?result.epoch:current_epoch;
        put(0x2d2fb0,current_visited,sizeof current_visited);
        if(status==XV_CLUSTER_REBASE_EPOCH)
            for(unsigned i=0;i<256;i++)
                if(result.changed[i>>5]&(1u<<(i&31)))w32(0x2d2fb0+i*4,result.epoch);
        require(!memcmp(g_xram,current_output,ARENA),"full original memory after rebased publication");
        if(status==XV_CLUSTER_REBASE_EPOCH){
            unsigned id=k%n;
            current_visited[id]=current_visited[id]==current_next?(current_next^1u):current_next;
            result=old;XvClusterResult saved=result;
            require(xv_cluster_query_rebase(&in,&result,n,current_epoch,current_visited)==XV_CLUSTER_REBASE_CONFLICT,
                    "changed active visited set declines");
            require(!memcmp(&result,&saved,sizeof result),"conflict preserves private result");conflicts++;
        }
        comparisons++;
    }
    require(advanced&&unchanged&&conflicts&&wraps,"rebase branch coverage");
    printf("PASS %u full original-context/memory rebase comparisons: %u epoch advances, %u no-epoch paths, %u conflicts, %u wraps\n",
           comparisons,advanced,unchanged,conflicts,wraps);
    free(initial);free(original_output);free(current_output);free(g_xram);free(g_xpt);return 0;
}
