#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* Bounded transpose of the exact read dependencies. Row 256 is unconditional.
 * This selects records only; callers still validate their mappings and bytes. */
enum { XV_READ_INDEX_MAX=8192, XV_READ_INDEX_WORDS=256 };
typedef struct { uint32_t *rows; unsigned count,words; } XvClusterReadIndex;
static inline void xv_read_index_clear(XvClusterReadIndex *index)
{ free(index->rows);memset(index,0,sizeof *index); }
#ifndef XV_READ_INDEX_CALLOC
#define XV_READ_INDEX_CALLOC calloc
#endif
static inline int xv_read_index_init(XvClusterReadIndex *index,unsigned count)
{
    xv_read_index_clear(index);
    if(!count||count>XV_READ_INDEX_MAX)return 0;
    unsigned words=(count+31)/32;
    index->rows=XV_READ_INDEX_CALLOC(257u*words,sizeof(uint32_t));
    if(!index->rows)return 0;
    index->count=count;index->words=words;return 1;
}
static inline void xv_read_index_add(XvClusterReadIndex *index,unsigned id,
                                    unsigned kind,const uint32_t dependencies[8])
{
    if(!index->rows||id>=index->count)abort();
    unsigned word=id>>5;uint32_t bit=1u<<(id&31);
    if(kind!=1){index->rows[256u*index->words+word]|=bit;return;}
    for(unsigned i=0;i<8;i++)for(uint32_t bits=dependencies[i];bits;bits&=bits-1){
        unsigned cluster=i*32u+(unsigned)__builtin_ctz(bits);
        index->rows[cluster*index->words+word]|=bit;
    }
}
static inline void xv_read_index_select(const XvClusterReadIndex *index,
    unsigned start,const uint32_t changed[8],unsigned cluster_count,
    uint32_t selected[XV_READ_INDEX_WORDS])
{
    if(!index->rows||!cluster_count||cluster_count>256||start>=cluster_count)abort();
    memcpy(selected,index->rows+256u*index->words,index->words*sizeof(uint32_t));
    unsigned n=(cluster_count+31)/32;
    for(unsigned i=0;i<n;i++){
        uint32_t bits=changed[i];
        if(i==start/32)bits|=1u<<(start&31);
        /* Match the scalar predicate's whole final dependency word. */
        while(bits){
            unsigned cluster=i*32u+(unsigned)__builtin_ctz(bits);bits&=bits-1;
            const uint32_t *row=index->rows+cluster*index->words;
            for(unsigned j=0;j<index->words;j++)selected[j]|=row[j];
        }
    }
}
