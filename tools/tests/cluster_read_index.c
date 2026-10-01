#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static int allocation_fails;
static void *test_calloc(size_t n,size_t s) {return allocation_fails?NULL:calloc(n,s);}
#define XV_READ_INDEX_CALLOC test_calloc
#include "kernel/xk_cluster_read_index.h"
static unsigned seed=19;
static unsigned random_word(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void)
{
    XvClusterReadIndex index={0};
    unsigned sizes[]={1,31,32,33,824,8192},clusters[]={1,7,32,65,256};
    unsigned (*deps)[8]=calloc(8192,sizeof *deps),*kinds=calloc(8192,sizeof *kinds);
    assert(deps&&kinds);
    for(unsigned a=0;a<6;a++)for(unsigned b=0;b<5;b++){
        unsigned count=sizes[a],nc=clusters[b];assert(xv_read_index_init(&index,count));
        for(unsigned id=0;id<count;id++){
            kinds[id]=id%11==0?0:id%13==0?2:1;
            memset(deps[id],0,sizeof deps[id]);
            for(unsigned j=0;j<4;j++){unsigned c=random_word()%nc;deps[id][c>>5]|=1u<<(c&31);}
            xv_read_index_add(&index,id,kinds[id],deps[id]);
            xv_read_index_add(&index,id,kinds[id],deps[id]); /* repeated/shared IDs */
        }
        for(unsigned trial=0;trial<40;trial++){
            uint32_t changed[8]={0},selected[XV_READ_INDEX_WORDS];unsigned start=trial%nc;
            for(unsigned j=0;j<trial%9;j++){unsigned c=random_word()%nc;changed[c>>5]|=1u<<(c&31);}
            xv_read_index_select(&index,start,changed,nc,selected);
            for(unsigned id=0;id<count;id++){
                unsigned expected=kinds[id]!=1||(deps[id][start/32]&(1u<<(start&31)));
                for(unsigned j=0;j<(nc+31)/32;j++)expected|=deps[id][j]&changed[j];
                assert(!!(selected[id/32]&(1u<<(id&31)))==!!expected);
            }
            for(unsigned id=count;id<index.words*32;id++)assert(!(selected[id/32]&(1u<<(id&31))));
        }
    }
    allocation_fails=1;assert(!xv_read_index_init(&index,824));assert(!index.rows&&!index.count&&!index.words);
    allocation_fails=0;assert(!xv_read_index_init(&index,0));assert(!xv_read_index_init(&index,8193));
    xv_read_index_clear(&index);free(deps);free(kinds);
    puts("PASS: exact ordered selection across 30 size/cluster combinations, shared/global/start dependencies, boundary bits and allocation failure");
}
